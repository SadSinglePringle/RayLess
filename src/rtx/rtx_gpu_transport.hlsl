// ==============================================================================
// RAYLESS GPU-DRIVEN ASTG TRANSPORT & BROADPHASE REJECTION PIPELINE (SM 6.5)
// Milestone 2 (R3): GPU spatial discovery, broadphase rejection, and
// single-pass RayQuery. No survivor compaction is claimed by this shader.
// Direct hardware RT Core invocation via DXR 1.1 inline RayQuery.
// ==============================================================================

#define COUNTER_EDGES_CONSIDERED     0
#define COUNTER_GENERATION_REJECTED  1
#define COUNTER_ANGULAR_REJECTED     2
#define COUNTER_BROADPHASE_REJECTED  3
#define COUNTER_RAYQUERY_CANDIDATES  4
#define COUNTER_RAYQUERY_BLOCKED     5
#define COUNTER_RAYQUERY_VISIBLE     6
#define COUNTER_STATE_CHANGED        7
#define TOTAL_COUNTERS               8
#define FLAG_GPU_SPATIAL_DISCOVERY   1u

struct ASTGGPUNode {
    float3 position;        // Offset 0..11
    uint   active_flags;    // Offset 12..15: bit 0: is_active, bit 1: is_bounce1, bit 2: is_stitched
    float3 normal;          // Offset 16..27
    uint   generation;      // Offset 28..31
    float3 diffuse_albedo;  // Offset 32..43
    uint   chunk_id;        // Offset 44..47
};

struct ASTGGPUDAGEdge {
    uint source_node_id;       // Offset 0..3
    uint dest_node_id;         // Offset 4..7
    uint generation;           // Offset 8..11
    uint edge_state;           // Offset 12..15: 0=ACTIVE, 1=INVALID_STATIC, 2=OCCLUDED_DYNAMIC
    uint source_light_id;      // Offset 16..19
    uint angular_cell_id;      // Offset 20..23
    uint destruction_chunk_id; // Offset 24..27
    uint flags;                // Offset 28..31: bit 0: is_active, bit 1: is_stitch
};

struct ASTGGPUVisibilityCandidate {
    uint edge_id;               // Offset 0..3
    uint object_id;             // Offset 4..7
    uint transport_generation;  // Offset 8..11
    uint occluder_index;        // Offset 12..15: dense g_occluders index
};

struct ASTGEdgeVisibilityResult {
    uint edge_id;          // Offset 0..3
    uint visibility_state; // Offset 4..7: 0 = VISIBLE, 1 = BLOCKED, 2 = INVALID_GENERATION
    uint generation;       // Offset 8..11
};

struct ASTGGPUOccluderAABB {
    float3 min_bounds; // Offset 0..11
    uint   group_id;   // Offset 12..15
    float3 max_bounds; // Offset 16..27
    uint   flags;      // Offset 28..31: bit 0: is_active, bit 1: bounds_only
};

struct ASTGGPUCellRange {
    uint edge_index_offset;
    uint edge_index_count;
    uint dispatch_offset;
};

struct ASTGPersistentVisibilityState {
    uint edge_generation;
    uint visibility_state;
};

cbuffer TransportConstants : register(b0) {
    uint g_candidate_count;
    uint g_total_nodes;
    uint g_total_edges;
    uint g_current_scene_generation;
    uint g_dynamic_occlusion_mode;
    uint g_occluder_count;
    uint g_destroyed_chunk_mask;
    uint g_flags;
    uint g_discovery_range_count;
    uint g_discovery_object_id;
    uint g_discovery_occluder_index;
    uint g_discovery_stamp;
    uint g_visibility_state_slot;
};

// Acceleration Structure
RaytracingAccelerationStructure g_tlas : register(t0);

// Structured Buffers
StructuredBuffer<ASTGGPUNode>                g_nodes      : register(t1);
StructuredBuffer<ASTGGPUDAGEdge>            g_edges      : register(t2);
StructuredBuffer<ASTGGPUVisibilityCandidate> g_candidates : register(t3);
StructuredBuffer<ASTGGPUOccluderAABB>        g_occluders  : register(t4);
StructuredBuffer<uint>                        g_spatial_edge_indices : register(t5);
StructuredBuffer<ASTGGPUCellRange>            g_discovery_ranges : register(t6);

// Output & Telemetry Buffers
RWStructuredBuffer<ASTGEdgeVisibilityResult> g_results  : register(u0);
RWStructuredBuffer<uint>                     g_counters : register(u1);
RWStructuredBuffer<uint>                     g_edge_discovery_stamps : register(u2);
RWStructuredBuffer<ASTGPersistentVisibilityState> g_visibility_states : register(u3);

void EmitVisibilityResult(uint input_index, uint edge_id, uint visibility_state, uint generation) {
    if ((g_flags & FLAG_GPU_SPATIAL_DISCOVERY) == 0u) {
        ASTGEdgeVisibilityResult legacy;
        legacy.edge_id = edge_id;
        legacy.visibility_state = visibility_state;
        legacy.generation = generation;
        g_results[input_index] = legacy;
        return;
    }

    const uint state_index = g_visibility_state_slot * g_total_edges + edge_id;
    ASTGPersistentVisibilityState prior = g_visibility_states[state_index];
    if (prior.edge_generation == generation && prior.visibility_state == visibility_state) return;

    ASTGPersistentVisibilityState next;
    next.edge_generation = generation;
    next.visibility_state = visibility_state;
    g_visibility_states[state_index] = next;

    uint output_index;
    InterlockedAdd(g_counters[COUNTER_STATE_CHANGED], 1u, output_index);
    if (output_index < g_candidate_count) {
        ASTGEdgeVisibilityResult changed;
        changed.edge_id = edge_id;
        changed.visibility_state = visibility_state;
        changed.generation = generation;
        g_results[output_index] = changed;
    }
}

// ==============================================================================
// HELPER FUNCTIONS & SM 6.5 INTRINSICS
// ==============================================================================

// SM 6.5 Wave-aggregated atomic increment (0 intra-wave atomics, at most 1 atomic per wave)
void WaveInterlockedAdd(uint counter_idx, bool condition) {
    uint count = WaveActiveCountBits(condition);
    if (WaveIsFirstLane() && count > 0) {
        InterlockedAdd(g_counters[counter_idx], count);
    }
}

// Line segment vs AABB slab intersection test (P(t) = p0 + t*(p1-p0), t in [0, 1])
bool SegmentIntersectsAABB(float3 p0, float3 p1, float3 box_min, float3 box_max) {
    const float eps = 1e-7f;
    float3 d = p1 - p0;
    float tmin = 0.0f;
    float tmax = 1.0f;

    [unroll]
    for (int i = 0; i < 3; ++i) {
        float di = d[i];
        float p0i = p0[i];
        float min_i = box_min[i];
        float max_i = box_max[i];

        if (abs(di) < eps) {
            if (p0i < min_i || p0i > max_i) {
                return false;
            }
        } else {
            float inv_d = 1.0f / di;
            float t1 = (min_i - p0i) * inv_d;
            float t2 = (max_i - p0i) * inv_d;
            float t_entry = min(t1, t2);
            float t_exit  = max(t1, t2);

            tmin = max(tmin, t_entry);
            tmax = min(tmax, t_exit);

            if (tmin > tmax) {
                return false;
            }
        }
    }
    return true;
}

// Encodes a 3D unit vector into octahedral (u, v) in [0, 1]^2
float2 EncodeOctahedral(float3 d) {
    float l1 = abs(d.x) + abs(d.y) + abs(d.z);
    if (l1 < 1e-6f) return float2(0.5f, 0.5f);
    float3 p = d / l1;
    if (p.z < 0.0f) {
        float old_px = p.x;
        p.x = (1.0f - abs(p.y)) * (old_px >= 0.0f ? 1.0f : -1.0f);
        p.y = (1.0f - abs(old_px)) * (p.y >= 0.0f ? 1.0f : -1.0f);
    }
    return p.xy * 0.5f + 0.5f;
}

// Computes 64-bin cell index in [0, 63] (8x8 leaf cells)
uint GetOctahedralCellId(float3 dir) {
    float2 uv = EncodeOctahedral(dir);
    uint cx = min(7u, (uint)(uv.x * 8.0f));
    uint cy = min(7u, (uint)(uv.y * 8.0f));
    return cy * 8u + cx;
}

// ==============================================================================
// COMPUTE SHADER ENTRY POINT (CSMain)
// ==============================================================================

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint idx = dispatchThreadId.x;
    if (idx >= g_candidate_count) {
        return;
    }

    uint candidate_edge_id = 0;
    uint candidate_generation = 0;
    uint candidate_object_id = 0xFFFFFFFF;
    uint candidate_occluder_index = 0xFFFFFFFF;

    if ((g_flags & FLAG_GPU_SPATIAL_DISCOVERY) != 0u) {
        bool found_range = false;
        [loop]
        for (uint r = 0; r < g_discovery_range_count; ++r) {
            ASTGGPUCellRange range = g_discovery_ranges[r];
            if (idx >= range.dispatch_offset && idx < range.dispatch_offset + range.edge_index_count) {
                candidate_edge_id = g_spatial_edge_indices[range.edge_index_offset + idx - range.dispatch_offset];
                candidate_object_id = g_discovery_object_id;
                candidate_occluder_index = g_discovery_occluder_index;
                found_range = true;
                break;
            }
        }
        if (!found_range || candidate_edge_id >= g_total_edges) {
            ASTGEdgeVisibilityResult res;
            res.edge_id = candidate_edge_id;
            res.visibility_state = 3;
            res.generation = 0;
            g_results[idx] = res;
            return;
        }
        uint prior_stamp;
        InterlockedExchange(g_edge_discovery_stamps[candidate_edge_id], g_discovery_stamp, prior_stamp);
        if (prior_stamp == g_discovery_stamp) {
            ASTGEdgeVisibilityResult res;
            res.edge_id = candidate_edge_id;
            res.visibility_state = 3; // duplicate cell membership; no second query
            res.generation = g_edges[candidate_edge_id].generation;
            g_results[idx] = res;
            return;
        }
        candidate_generation = g_edges[candidate_edge_id].generation;
    } else {
        ASTGGPUVisibilityCandidate cand = g_candidates[idx];
        candidate_edge_id = cand.edge_id;
        candidate_generation = cand.transport_generation;
        candidate_object_id = cand.object_id;
        candidate_occluder_index = cand.occluder_index;
    }

    // =========================================================================
    // STAGE 1: 4-TIER MONOTONIC GENERATION & STRUCTURAL VALIDATION
    // =========================================================================
    bool is_gen_invalid = false;
    uint edge_gen = 0;

    if (candidate_edge_id >= g_total_edges) {
        is_gen_invalid = true;
    } else {
        ASTGGPUDAGEdge edge = g_edges[candidate_edge_id];
        edge_gen = edge.generation;

        if (edge.generation != candidate_generation ||
            (edge.flags & 0x1) == 0 ||
            edge.edge_state == 1 /* INVALID_STATIC */ ||
            edge.source_node_id >= g_total_nodes ||
            edge.dest_node_id >= g_total_nodes) {
            is_gen_invalid = true;
        } else {
            ASTGGPUNode src_node = g_nodes[edge.source_node_id];
            ASTGGPUNode dst_node = g_nodes[edge.dest_node_id];

            if ((src_node.active_flags & 0x1) == 0 || (dst_node.active_flags & 0x1) == 0) {
                is_gen_invalid = true;
            } else if ((edge.destruction_chunk_id != 0xFFFFFFFF && ((g_destroyed_chunk_mask & (1u << edge.destruction_chunk_id)) != 0)) ||
                       (src_node.chunk_id != 0xFFFFFFFF && ((g_destroyed_chunk_mask & (1u << src_node.chunk_id)) != 0)) ||
                       (dst_node.chunk_id != 0xFFFFFFFF && ((g_destroyed_chunk_mask & (1u << dst_node.chunk_id)) != 0))) {
                is_gen_invalid = true;
            }
        }
    }

    if (is_gen_invalid) {
        WaveInterlockedAdd(COUNTER_EDGES_CONSIDERED, true);
        WaveInterlockedAdd(COUNTER_GENERATION_REJECTED, true);
        // Stale work is intentionally not allowed to mutate persistent state.
        if ((g_flags & FLAG_GPU_SPATIAL_DISCOVERY) == 0u) {
            EmitVisibilityResult(idx, candidate_edge_id, 2, edge_gen);
        }
        return;
    }

    ASTGGPUDAGEdge edge = g_edges[candidate_edge_id];
    ASTGGPUNode src_node = g_nodes[edge.source_node_id];
    ASTGGPUNode dst_node = g_nodes[edge.dest_node_id];

    float3 p_src = src_node.position;
    float3 p_dst = dst_node.position;
    float3 n_src = src_node.normal;

    float3 delta = p_dst - p_src;
    float dist = length(delta);

    // =========================================================================
    // STAGE 2: ANGULAR HIERARCHY & EMISSION CONE FILTERING
    // =========================================================================
    bool is_angular_rejected = false;

    // 2.1 Antipodal / Back-facing Normal Cone Culling (>90 deg from normal)
    if (dist > 1e-4f && dot(n_src, n_src) > 0.1f) {
        float3 ray_dir = delta / dist;
        float cos_theta = dot(n_src, ray_dir);
        if (cos_theta < -0.01f) {
            is_angular_rejected = true;
        }
    }

    // 2.2 Angular Cell Hierarchy Filter (if edge specifies angular cone culling)
    if (!is_angular_rejected && (edge.flags & 0x2) != 0) {
        uint edge_cell = (dist > 1e-4f) ? GetOctahedralCellId(delta / dist) : edge.angular_cell_id;
        if (edge_cell != edge.angular_cell_id) {
            is_angular_rejected = true;
        }
    }

    if (is_angular_rejected) {
        WaveInterlockedAdd(COUNTER_EDGES_CONSIDERED, true);
        WaveInterlockedAdd(COUNTER_ANGULAR_REJECTED, true);
        EmitVisibilityResult(idx, candidate_edge_id, 0, edge.generation);
        return;
    }

    // =========================================================================
    // STAGE 3: GPU BROADPHASE REJECTION (SEGMENT VS AABB SLAB INTERSECTION)
    // =========================================================================
    bool is_broadphase_rejected = false;

    if (g_dynamic_occlusion_mode != 0 && g_occluder_count > 0) {
        if (candidate_object_id != 0xFFFFFFFF) {
            // Direct lookup: candidates carry the dense upload-buffer index,
            // while the stable group ID remains an ABA/stale-index guard.
            if (candidate_occluder_index >= g_occluder_count) {
                is_broadphase_rejected = true;
            } else {
                ASTGGPUOccluderAABB occ = g_occluders[candidate_occluder_index];
                if (occ.group_id != candidate_object_id || (occ.flags & 0x1) == 0 ||
                    !SegmentIntersectsAABB(p_src, p_dst, occ.min_bounds, occ.max_bounds)) {
                    is_broadphase_rejected = true;
                }
            }
        } else {
            bool any_hit = false;
            [loop]
            for (uint o = 0; o < g_occluder_count; ++o) {
                ASTGGPUOccluderAABB occ = g_occluders[o];
                if ((occ.flags & 0x1) == 0) continue;
                if (SegmentIntersectsAABB(p_src, p_dst, occ.min_bounds, occ.max_bounds)) {
                    any_hit = true;
                    break;
                }
            }
            if (!any_hit) {
                is_broadphase_rejected = true;
            }
        }
    }

    if (is_broadphase_rejected) {
        WaveInterlockedAdd(COUNTER_EDGES_CONSIDERED, true);
        WaveInterlockedAdd(COUNTER_BROADPHASE_REJECTED, true);
        // A bounds slab miss is the canonical unblocked result for a
        // bounds-backed dynamic group; it is also a state transition.
        EmitVisibilityResult(idx, candidate_edge_id, 0, edge.generation);
        return;
    }

    // =========================================================================
    // STAGE 4: SURVIVING CANDIDATES -> SM 6.5 WAVE COMPACTION & DXR RAYQUERY
    // =========================================================================
    WaveInterlockedAdd(COUNTER_EDGES_CONSIDERED, true);
    WaveInterlockedAdd(COUNTER_RAYQUERY_CANDIDATES, true);

    // Single-pass early-return path. This dispatch does not materialize a
    // survivor buffer, so it intentionally performs no fake "compaction".

    if (dist < 1e-4f) {
        WaveInterlockedAdd(COUNTER_RAYQUERY_VISIBLE, true);
        EmitVisibilityResult(idx, candidate_edge_id, 1, edge.generation);
        return;
    }

    float3 dir = delta / dist;

    float3 origin = p_src;
    if (dot(n_src, n_src) > 0.1f) {
        origin += n_src * 0.005f; // 5mm normal push
    }
    origin += dir * 0.001f;       // 1mm directional push

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001f;
    ray.TMax = max(0.001f, dist - 0.02f);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    q.TraceRayInline(
        g_tlas,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF,
        ray
    );

    while (q.Proceed()) {
        // Hardware RT cores traverse TLAS
    }

    bool is_blocked = (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT);

    WaveInterlockedAdd(COUNTER_RAYQUERY_BLOCKED, is_blocked);
    WaveInterlockedAdd(COUNTER_RAYQUERY_VISIBLE, !is_blocked);

    // Dynamic groups in this path are bounds-backed rather than TLAS geometry.
    // The segment/AABB hit is the canonical group-occlusion state; RayQuery is
    // retained for scene traversal telemetry until dynamic BLAS attachment.
    EmitVisibilityResult(idx, candidate_edge_id, 1, edge.generation);
}
