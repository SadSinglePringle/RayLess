// ==============================================================================
// ASTG PART J: GPU CONTINUOUS ANGULAR B0 RUNTIME SHADERS
// ==============================================================================

#define HLSL_COMPUTE 1

struct RTXSourceAngularFrame {
    float origin_x, origin_y, origin_z;
    uint light_id;
    float forward_x, forward_y, forward_z;
    uint light_type;
    float right_x, right_y, right_z;
    float range;
    float up_x, up_y, up_z;
    uint generation;
};

struct ASTGB0DirectionRecord {
    float dir_local_x, dir_local_y, dir_local_z;
    uint source_light_id;
    float theta;
    float phi;
    uint transport_node_id;
    uint flags;
    float hit_dist;
    uint generation;
    uint retained_receiver_id;
    float solid_angle;
};

struct ASTGB0AngularBVHNode {
    float cone_axis_x, cone_axis_y, cone_axis_z;
    float cos_half_angle;
    uint left_child;
    uint right_child;
    uint record_count;
    uint light_id;
};

struct ASTGB0AngularFootprint {
    float cone_axis_x, cone_axis_y, cone_axis_z;
    float cos_half_angle;
    float sin_half_angle;
    float min_dist;
    float max_dist;
    uint flags;
};

struct ASTGLightB0RangeGPU {
    uint record_offset;
    uint record_count;
    uint bvh_offset;
    uint bvh_node_count;
    float color_r;
    float color_g;
    float color_b;
    float intensity;
};

struct ASTGChangedGroupLightPairGPU {
    uint group_id;
    uint light_id;
    uint first_bound;
    uint bound_count;
    uint record_offset;
    uint record_count;
    uint bvh_root_index;
    uint generation;
    uint pair_index;
    uint membership_word_offset;
    uint pad[2];
};

struct ASTGBoneBoundGPU {
    float local_min_x, local_min_y, local_min_z;
    uint bone_id;
    float local_max_x, local_max_y, local_max_z;
    uint group_id;
    float world_min_x, world_min_y, world_min_z;
    uint cluster_offset;
    float world_max_x, world_max_y, world_max_z;
    uint cluster_count;
};

struct ASTGB0PersistentState {
    uint blocker_count;
    uint last_generation;
};

struct ASTGB0TransitionRecord {
    uint light_id;
    uint b0_record_id;
    uint transport_node_id;
    uint new_visibility_state; // 0 = unblocked, 1 = blocked
    uint new_blocker_count;
    uint generation;
    uint pad[2];
};

struct ASTGPartJConstants {
    uint pair_count;
    uint total_bounds;
    uint total_records;
    uint total_bvh_nodes;
    uint current_generation;
    uint dynamic_occlusion_mode;
    uint use_dxr_geometry;
    uint flags;
};

// Global Bindings
ConstantBuffer<ASTGPartJConstants> g_constants : register(b0);

StructuredBuffer<RTXSourceAngularFrame>         g_light_frames      : register(t0);
StructuredBuffer<ASTGLightB0RangeGPU>           g_light_ranges      : register(t1);
StructuredBuffer<ASTGB0DirectionRecord>         g_b0_records        : register(t2);
StructuredBuffer<ASTGB0AngularBVHNode>          g_b0_bvh_nodes      : register(t3);
StructuredBuffer<float3>                        g_b0_hit_positions  : register(t4);
StructuredBuffer<ASTGChangedGroupLightPairGPU>  g_changed_pairs     : register(t5);
StructuredBuffer<ASTGBoneBoundGPU>              g_bone_bounds       : register(t6);

RWStructuredBuffer<ASTGB0AngularFootprint>      g_footprints        : register(u0);
RWStructuredBuffer<uint>                        g_current_membership: register(u1);
RWStructuredBuffer<uint>                        g_previous_membership:register(u2);
RWStructuredBuffer<ASTGB0PersistentState>       g_persistent_states : register(u3);
RWStructuredBuffer<ASTGB0TransitionRecord>      g_transitions       : register(u4);
RWByteAddressBuffer                             g_transition_counter: register(u5);
RWStructuredBuffer<uint>                        g_telemetry         : register(u6);

// Helper: Segment intersects AABB exact slab test
bool SegmentIntersectsAABB(float3 p0, float3 p1, float3 boxMin, float3 boxMax) {
    float3 d = p1 - p0;
    float tmin = 0.0f;
    float tmax = 1.0f;

    [unroll]
    for (int i = 0; i < 3; ++i) {
        float origin = (i == 0) ? p0.x : ((i == 1) ? p0.y : p0.z);
        float dir = (i == 0) ? d.x : ((i == 1) ? d.y : d.z);
        float bmin = (i == 0) ? boxMin.x : ((i == 1) ? boxMin.y : boxMin.z);
        float bmax = (i == 0) ? boxMax.x : ((i == 1) ? boxMax.y : boxMax.z);

        if (abs(dir) < 1e-7f) {
            if (origin < bmin || origin > bmax) return false;
        } else {
            float ood = 1.0f / dir;
            float t1 = (bmin - origin) * ood;
            float t2 = (bmax - origin) * ood;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tmin = max(tmin, t1);
            tmax = min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    return tmin <= tmax && tmax >= 0.0f && tmin <= 1.0f;
}

// Pass J1: Conservative Bounding-Sphere Cone Footprint Projection
[numthreads(64, 1, 1)]
void CSProjectDynamicBounds(uint3 id : SV_DispatchThreadID) {
    uint bound_index = id.x;
    if (bound_index >= g_constants.total_bounds) return;

    ASTGBoneBoundGPU b = g_bone_bounds[bound_index];
    float3 bmin = float3(b.world_min_x, b.world_min_y, b.world_min_z);
    float3 bmax = float3(b.world_max_x, b.world_max_y, b.world_max_z);

    float3 center = (bmin + bmax) * 0.5f;
    float3 ext = (bmax - bmin) * 0.5f;
    float radius = length(ext);

    // Telemetry: bounds projected
    InterlockedAdd(g_telemetry[0], 1);

    // Project against each changed group/light pair that references this bound
    for (uint p = 0; p < g_constants.pair_count; ++p) {
        ASTGChangedGroupLightPairGPU pair = g_changed_pairs[p];
        if (b.group_id != pair.group_id) continue;
        if (bound_index < pair.first_bound || bound_index >= pair.first_bound + pair.bound_count) continue;

        RTXSourceAngularFrame frame = g_light_frames[pair.light_id];
        float3 light_pos = float3(frame.origin_x, frame.origin_y, frame.origin_z);
        float3 to_center = center - light_pos;
        float dist = length(to_center);

        ASTGB0AngularFootprint fp;
        if (dist <= radius || dist < 1e-4f) {
            // Light inside bounding sphere -> conservative full spherical coverage
            fp.cone_axis_x = 0.0f; fp.cone_axis_y = 1.0f; fp.cone_axis_z = 0.0f;
            fp.cos_half_angle = -1.0f;
            fp.sin_half_angle = 0.0f;
            fp.min_dist = 0.0f;
            fp.max_dist = dist + radius;
            fp.flags = 0x1; // Full coverage
        } else {
            float3 world_axis = to_center / dist;
            float3 f_right = float3(frame.right_x, frame.right_y, frame.right_z);
            float3 f_up = float3(frame.up_x, frame.up_y, frame.up_z);
            float3 f_fwd = float3(frame.forward_x, frame.forward_y, frame.forward_z);

            float3 local_axis = float3(
                dot(world_axis, f_right),
                dot(world_axis, f_up),
                dot(world_axis, f_fwd)
            );
            float local_len = length(local_axis);
            if (local_len > 1e-5f) local_axis /= local_len;
            else local_axis = float3(0, 0, 1);

            float sin_half = saturate(radius / dist);
            float cos_half = sqrt(max(0.0f, 1.0f - sin_half * sin_half));

            fp.cone_axis_x = local_axis.x;
            fp.cone_axis_y = local_axis.y;
            fp.cone_axis_z = local_axis.z;
            fp.cos_half_angle = cos_half;
            fp.sin_half_angle = sin_half;
            fp.min_dist = max(0.0f, dist - radius);
            fp.max_dist = dist + radius;
            fp.flags = 0x0;
        }

        uint fp_index = pair.pair_index * 64 + (bound_index - pair.first_bound);
        g_footprints[fp_index] = fp;
    }
}

// Pass J2: B0 Angular BVH Traversal & Exact Candidate Visibility
[numthreads(64, 1, 1)]
void CSTraverseB0AngularBVH(uint3 id : SV_DispatchThreadID) {
    uint pair_idx = id.x;
    if (pair_idx >= g_constants.pair_count) return;

    ASTGChangedGroupLightPairGPU pair = g_changed_pairs[pair_idx];
    RTXSourceAngularFrame frame = g_light_frames[pair.light_id];
    float3 light_pos = float3(frame.origin_x, frame.origin_y, frame.origin_z);

    // Traverse BVH for each bound in this group/light pair
    for (uint b_rel = 0; b_rel < pair.bound_count; ++b_rel) {
        uint bound_index = pair.first_bound + b_rel;
        ASTGBoneBoundGPU b = g_bone_bounds[bound_index];
        float3 bmin = float3(b.world_min_x, b.world_min_y, b.world_min_z);
        float3 bmax = float3(b.world_max_x, b.world_max_y, b.world_max_z);

        uint fp_index = pair.pair_index * 64 + b_rel;
        ASTGB0AngularFootprint fp = g_footprints[fp_index];

        float3 fp_axis = float3(fp.cone_axis_x, fp.cone_axis_y, fp.cone_axis_z);
        float fp_cos = fp.cos_half_angle;

        // Bounded stack traversal (max depth 32)
        uint stack[32];
        uint stack_ptr = 0;
        stack[stack_ptr++] = pair.bvh_root_index;

        while (stack_ptr > 0) {
            uint node_idx = stack[--stack_ptr];
            if (node_idx >= g_constants.total_bvh_nodes) continue;

            ASTGB0AngularBVHNode node = g_b0_bvh_nodes[node_idx];
            InterlockedAdd(g_telemetry[1], 1); // Nodes visited

            // Check cone overlap
            bool overlap = (fp.flags & 0x1) != 0; // Full sphere always overlaps
            if (!overlap) {
                float3 node_axis = float3(node.cone_axis_x, node.cone_axis_y, node.cone_axis_z);
                float cos_gamma = dot(fp_axis, node_axis);
                float sin_gamma = sqrt(max(0.0f, 1.0f - cos_gamma * cos_gamma));
                float sin_node = sqrt(max(0.0f, 1.0f - node.cos_half_angle * node.cos_half_angle));
                float cos_sum = fp_cos * node.cos_half_angle - fp.sin_half_angle * sin_node;
                overlap = (cos_gamma >= cos_sum) || (cos_gamma + fp.sin_half_angle * sin_node >= fp_cos * node.cos_half_angle);
            }

            if (!overlap) {
                InterlockedAdd(g_telemetry[2], 1); // Node culled
                continue;
            }

            if (node.record_count > 0) {
                // Leaf: test member records
                for (uint r = 0; r < node.record_count; ++r) {
                    uint record_idx = node.left_child + r;
                    if (record_idx >= g_constants.total_records) continue;

                    ASTGB0DirectionRecord rec = g_b0_records[record_idx];
                    float3 rec_dir = float3(rec.dir_local_x, rec.dir_local_y, rec.dir_local_z);

                    // Angular containment
                    bool in_footprint = ((fp.flags & 0x1) != 0) || (dot(rec_dir, fp_axis) >= fp_cos);
                    if (in_footprint) {
                        InterlockedAdd(g_telemetry[3], 1); // Exact tests

                        // Pass J3: Exact candidate visibility
                        float3 hit_pos = g_b0_hit_positions[record_idx];
                        bool blocked = SegmentIntersectsAABB(light_pos, hit_pos, bmin, bmax);

                        if (blocked) {
                            InterlockedAdd(g_telemetry[4], 1); // Exact hits
                            // Set bit in current membership bitset
                            uint local_rec = record_idx - pair.record_offset;
                            uint word_idx = pair.membership_word_offset + (local_rec / 32);
                            uint bit_mask = 1u << (local_rec % 32);
                            InterlockedOr(g_current_membership[word_idx], bit_mask);
                        }
                    }
                }
            } else {
                // Internal node: push children
                if (stack_ptr + 2 <= 32) {
                    if (node.right_child != 0) stack[stack_ptr++] = node.right_child;
                    if (node.left_child != 0) stack[stack_ptr++] = node.left_child;
                } else {
                    // Stack overflow flag
                    InterlockedOr(g_telemetry[5], 0x1);
                }
            }
        }
    }
}

// Pass J4: Apply Membership Deltas & Update Blocker Counts
[numthreads(64, 1, 1)]
void CSApplyB0MembershipDeltas(uint3 id : SV_DispatchThreadID) {
    uint pair_idx = id.x;
    if (pair_idx >= g_constants.pair_count) return;

    ASTGChangedGroupLightPairGPU pair = g_changed_pairs[pair_idx];
    uint word_count = (pair.record_count + 31) / 32;

    for (uint w = 0; w < word_count; ++w) {
        uint word_idx = pair.membership_word_offset + w;
        uint curr = g_current_membership[word_idx];
        uint prev = g_previous_membership[word_idx];

        uint added = curr & ~prev;
        uint removed = prev & ~curr;

        // Process additions
        uint temp_add = added;
        while (temp_add != 0) {
            uint bit = firstbitlow(temp_add);
            temp_add &= ~(1u << bit);

            uint local_rec = w * 32 + bit;
            if (local_rec < pair.record_count) {
                uint global_rec = pair.record_offset + local_rec;
                uint old_count = 0;
                InterlockedAdd(g_persistent_states[global_rec].blocker_count, 1, old_count);

                if (old_count == 0) {
                    // State transitioned to BLOCKED (0 -> 1)
                    uint trans_idx;
                    g_transition_counter.InterlockedAdd(0, 1, trans_idx);

                    if (trans_idx < 65536) {
                        ASTGB0TransitionRecord tr;
                        tr.light_id = pair.light_id;
                        tr.b0_record_id = global_rec;
                        tr.transport_node_id = g_b0_records[global_rec].transport_node_id;
                        tr.new_visibility_state = 1;
                        tr.new_blocker_count = 1;
                        tr.generation = g_constants.current_generation;
                        tr.pad[0] = 0; tr.pad[1] = 0;
                        g_transitions[trans_idx] = tr;
                    }
                    InterlockedAdd(g_telemetry[6], 1); // Transitions
                }
            }
        }

        // Process removals
        uint temp_rem = removed;
        while (temp_rem != 0) {
            uint bit = firstbitlow(temp_rem);
            temp_rem &= ~(1u << bit);

            uint local_rec = w * 32 + bit;
            if (local_rec < pair.record_count) {
                uint global_rec = pair.record_offset + local_rec;
                uint old_count = 0;
                InterlockedAdd(g_persistent_states[global_rec].blocker_count, 0xFFFFFFFF, old_count);

                if (old_count == 1) {
                    // State transitioned to UNBLOCKED (1 -> 0)
                    uint trans_idx;
                    g_transition_counter.InterlockedAdd(0, 1, trans_idx);

                    if (trans_idx < 65536) {
                        ASTGB0TransitionRecord tr;
                        tr.light_id = pair.light_id;
                        tr.b0_record_id = global_rec;
                        tr.transport_node_id = g_b0_records[global_rec].transport_node_id;
                        tr.new_visibility_state = 0;
                        tr.new_blocker_count = 0;
                        tr.generation = g_constants.current_generation;
                        tr.pad[0] = 0; tr.pad[1] = 0;
                        g_transitions[trans_idx] = tr;
                    }
                    InterlockedAdd(g_telemetry[6], 1); // Transitions
                } else if (old_count == 0) {
                    // Underflow error flag
                    InterlockedOr(g_telemetry[5], 0x2);
                }
            }
        }

        // Copy current to previous for temporal persistence
        g_previous_membership[word_idx] = curr;
        // Clear current membership for next frame
        g_current_membership[word_idx] = 0;
    }
}

// Pass J5: Compact Transitions (Optional format pass)
[numthreads(64, 1, 1)]
void CSCompactB0Transitions(uint3 id : SV_DispatchThreadID) {
    // Identity compaction pass for downstream consumers
}
