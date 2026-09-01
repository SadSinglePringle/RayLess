// ==============================================================================
// ASTG PART K: GPU DYNAMIC SURFACE RECEIVER PROBE RUNTIME SHADERS
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

struct ASTGBoneTransformGPU {
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;
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

struct ASTGReceiverClusterGPU {
    float world_center_x, world_center_y, world_center_z;
    float radius;
    float normal_axis_x, normal_axis_y, normal_axis_z;
    float cos_normal_half_angle;
    uint probe_offset;
    uint probe_count;
    uint bone_id;
    uint generation;
};

struct ASTGDynamicSurfaceProbeGPU {
    float local_pos_x, local_pos_y, local_pos_z;
    uint bone_id;
    float local_norm_x, local_norm_y, local_norm_z;
    uint cluster_id;
    float world_pos_x, world_pos_y, world_pos_z;
    uint group_id;
    float world_norm_x, world_norm_y, world_norm_z;
    uint generation;
    float irradiance_r, irradiance_g, irradiance_b;
    uint last_visibility_mask;
};

struct ASTGProbeLightWorkGPU {
    uint probe_id;
    uint actual_light_id;
    uint packed_light_index;
    uint dependency_generation;
};

struct ASTGProbeLightContributionGPU {
    uint probe_id;
    uint actual_light_id;
    uint visibility;
    uint dependency_generation;
    float irradiance_r;
    float irradiance_g;
    float irradiance_b;
    float pad;
};

struct ASTGPartKConstants {
    uint total_bones;
    uint total_clusters;
    uint total_probes;
    uint total_lights;
    uint current_generation;
    uint is_skeletal;
    uint dynamic_occlusion_mode;
    uint max_work_items;
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

// Global Bindings
ConstantBuffer<ASTGPartKConstants>              g_constants         : register(b0);

StructuredBuffer<RTXSourceAngularFrame>         g_light_frames      : register(t0);
StructuredBuffer<ASTGBoneTransformGPU>          g_bone_transforms   : register(t1);
StructuredBuffer<ASTGLightB0RangeGPU>           g_light_ranges      : register(t2);
StructuredBuffer<uint>                          g_cluster_probe_indices : register(t3);

RWStructuredBuffer<ASTGBoneBoundGPU>            g_bone_bounds       : register(u0);
RWStructuredBuffer<ASTGReceiverClusterGPU>      g_receiver_clusters : register(u1);
RWStructuredBuffer<ASTGDynamicSurfaceProbeGPU>  g_surface_probes    : register(u2);
RWStructuredBuffer<ASTGProbeLightWorkGPU>       g_probe_work_items  : register(u3);
RWByteAddressBuffer                             g_work_counter      : register(u4);
RWStructuredBuffer<uint>                        g_telemetry         : register(u5);
RWStructuredBuffer<ASTGProbeLightContributionGPU> g_contributions   : register(u6);
RWByteAddressBuffer                             g_indirect_args     : register(u7);
RWByteAddressBuffer                             g_probe_irradiance_accum : register(u8);

void InterlockedAddFloat(RWByteAddressBuffer buf, uint byte_offset, float value) {
    if (abs(value) < 1e-7f) return;
    uint prev = buf.Load(byte_offset);
    [allow_uav_condition]
    for (uint i = 0; i < 64; ++i) {
        float next_f = asfloat(prev) + value;
        uint orig;
        buf.InterlockedCompareExchange(byte_offset, prev, asuint(next_f), orig);
        if (orig == prev) break;
        prev = orig;
    }
}

// Helper: Matrix-vector multiply
float3 TransformPoint(float4 row0, float4 row1, float4 row2, float4 row3, float3 p) {
    return float3(
        row0.x * p.x + row0.y * p.y + row0.z * p.z + row0.w,
        row1.x * p.x + row1.y * p.y + row1.z * p.z + row1.w,
        row2.x * p.x + row2.y * p.y + row2.z * p.z + row2.w
    );
}

float3 TransformVector(float4 row0, float4 row1, float4 row2, float3 v) {
    return float3(
        row0.x * v.x + row0.y * v.y + row0.z * v.z,
        row1.x * v.x + row1.y * v.y + row1.z * v.z,
        row2.x * v.x + row2.y * v.y + row2.z * v.z
    );
}

bool SegmentIntersectsAABB(float3 p0, float3 p1, float3 boxMin, float3 boxMax) {
    float3 d = p1 - p0;
    float tmin = 0.0f;
    float tmax = 1.0f;

    [unroll]
    for (int i = 0; i < 3; ++i) {
        float org = (i == 0) ? p0.x : ((i == 1) ? p0.y : p0.z);
        float dir = (i == 0) ? d.x : ((i == 1) ? d.y : d.z);
        float bm = (i == 0) ? boxMin.x : ((i == 1) ? boxMin.y : boxMin.z);
        float bx = (i == 0) ? boxMax.x : ((i == 1) ? boxMax.y : boxMax.z);

        if (abs(dir) < 1e-7f) {
            if (org < bm || org > bx) return false;
        } else {
            float ood = 1.0f / dir;
            float t1 = (bm - org) * ood;
            float t2 = (bx - org) * ood;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tmin = max(tmin, t1);
            tmax = min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    return tmin <= tmax && tmax >= 0.0f && tmin <= 1.0f;
}

// Pass K1: Transform Bone Bounds using Absolute Matrix Extents
[numthreads(64, 1, 1)]
void CSTransformBoneBounds(uint3 id : SV_DispatchThreadID) {
    uint bone_idx = id.x;
    if (bone_idx >= g_constants.total_bones) return;

    ASTGBoneBoundGPU bb = g_bone_bounds[bone_idx];
    ASTGBoneTransformGPU bt = g_bone_transforms[bb.bone_id];

    float3 local_min = float3(bb.local_min_x, bb.local_min_y, bb.local_min_z);
    float3 local_max = float3(bb.local_max_x, bb.local_max_y, bb.local_max_z);
    float3 local_center = (local_min + local_max) * 0.5f;
    float3 local_ext = (local_max - local_min) * 0.5f;

    float3 world_center = TransformPoint(bt.row0, bt.row1, bt.row2, bt.row3, local_center);

    // Absolute matrix extents: abs(M) * local_ext
    float3 world_ext = float3(
        abs(bt.row0.x) * local_ext.x + abs(bt.row0.y) * local_ext.y + abs(bt.row0.z) * local_ext.z,
        abs(bt.row1.x) * local_ext.x + abs(bt.row1.y) * local_ext.y + abs(bt.row1.z) * local_ext.z,
        abs(bt.row2.x) * local_ext.x + abs(bt.row2.y) * local_ext.y + abs(bt.row2.z) * local_ext.z
    );

    float3 world_min = world_center - world_ext;
    float3 world_max = world_center + world_ext;

    bb.world_min_x = world_min.x; bb.world_min_y = world_min.y; bb.world_min_z = world_min.z;
    bb.world_max_x = world_max.x; bb.world_max_y = world_max.y; bb.world_max_z = world_max.z;

    g_bone_bounds[bone_idx] = bb;
    InterlockedAdd(g_telemetry[13], 1); // K1 Bones transformed
}

// Pass K2: Transform Surface Probes with Inverse-Transpose Normal Matrix
[numthreads(64, 1, 1)]
void CSTransformSurfaceProbes(uint3 id : SV_DispatchThreadID) {
    uint probe_idx = id.x;
    if (probe_idx >= g_constants.total_probes) return;

    ASTGDynamicSurfaceProbeGPU pr = g_surface_probes[probe_idx];
    ASTGBoneTransformGPU bt = g_bone_transforms[pr.bone_id];

    float3 local_pos = float3(pr.local_pos_x, pr.local_pos_y, pr.local_pos_z);
    float3 local_norm = float3(pr.local_norm_x, pr.local_norm_y, pr.local_norm_z);

    float3 world_pos = TransformPoint(bt.row0, bt.row1, bt.row2, bt.row3, local_pos);

    // Exact inverse-transpose normal transformation: transpose(inverse(M)) * local_norm
    float3 r0 = bt.row0.xyz;
    float3 r1 = bt.row1.xyz;
    float3 r2 = bt.row2.xyz;

    float3 c0 = cross(r1, r2);
    float3 c1 = cross(r2, r0);
    float3 c2 = cross(r0, r1);
    float det = dot(r0, c0);
    float3 world_norm;
    if (abs(det) > 1e-6f) {
        float invDet = 1.0f / det;
        world_norm = float3(
            dot(c0, local_norm),
            dot(c1, local_norm),
            dot(c2, local_norm)
        ) * invDet;
    } else {
        // Singular matrix fallback
        world_norm = float3(0, 1, 0);
    }
    float norm_len = length(world_norm);
    if (norm_len > 1e-5f) world_norm /= norm_len;
    else world_norm = float3(0, 1, 0);

    pr.world_pos_x = world_pos.x; pr.world_pos_y = world_pos.y; pr.world_pos_z = world_pos.z;
    pr.world_norm_x = world_norm.x; pr.world_norm_y = world_norm.y; pr.world_norm_z = world_norm.z;
    pr.generation = g_constants.current_generation;

    // Reset direct irradiance accumulator for fresh lighting evaluation
    pr.irradiance_r = 0.0f;
    pr.irradiance_g = 0.0f;
    pr.irradiance_b = 0.0f;

    g_surface_probes[probe_idx] = pr;
    InterlockedAdd(g_telemetry[14], 1); // K2 Probes transformed
}

// Pass K3: Reduce & Rebuild Receiver Clusters from Current Transformed Probes
[numthreads(64, 1, 1)]
void CSTransformReceiverClusters(uint3 id : SV_DispatchThreadID) {
    uint cluster_idx = id.x;
    if (cluster_idx >= g_constants.total_clusters) return;

    ASTGReceiverClusterGPU cl = g_receiver_clusters[cluster_idx];
    uint count = cl.probe_count;

    if (count > 0) {
        float3 center = float3(0, 0, 0);
        float3 norm_sum = float3(0, 0, 0);
        uint valid_count = 0;

        for (uint p = 0; p < count; ++p) {
            uint p_idx = g_cluster_probe_indices[cl.probe_offset + p];
            if (p_idx < g_constants.total_probes) {
                ASTGDynamicSurfaceProbeGPU pr = g_surface_probes[p_idx];
                center += float3(pr.world_pos_x, pr.world_pos_y, pr.world_pos_z);
                norm_sum += float3(pr.world_norm_x, pr.world_norm_y, pr.world_norm_z);
                valid_count++;
            } else {
                InterlockedAdd(g_telemetry[24], 1); // Invalid probe index error
            }
        }
        if (valid_count > 0) {
            center /= float(valid_count);
        }
        float norm_len = length(norm_sum);
        if (norm_len > 1e-4f) norm_sum /= norm_len;
        else norm_sum = float3(0, 1, 0);

        float max_r = 0.1f;
        for (uint i = 0; i < count; ++i) {
            uint p_idx = g_cluster_probe_indices[cl.probe_offset + i];
            if (p_idx < g_constants.total_probes) {
                ASTGDynamicSurfaceProbeGPU pr = g_surface_probes[p_idx];
                float3 ppos = float3(pr.world_pos_x, pr.world_pos_y, pr.world_pos_z);
                max_r = max(max_r, length(ppos - center));
            }
        }

        cl.world_center_x = center.x; cl.world_center_y = center.y; cl.world_center_z = center.z;
        cl.radius = max_r;
        cl.normal_axis_x = norm_sum.x; cl.normal_axis_y = norm_sum.y; cl.normal_axis_z = norm_sum.z;
        cl.cos_normal_half_angle = 0.5f; // ~60 deg cone
        cl.generation = g_constants.current_generation;
    }

    g_receiver_clusters[cluster_idx] = cl;
    InterlockedAdd(g_telemetry[15], 1); // K3 Clusters rebuilt
}

// Pass K4: Cull Receiver Hierarchy & Emit Compact Probe-Light Work Queue
[numthreads(64, 1, 1)]
void CSCullReceiverHierarchy(uint3 id : SV_DispatchThreadID) {
    uint cluster_idx = id.x;
    if (cluster_idx >= g_constants.total_clusters) return;

    ASTGReceiverClusterGPU cl = g_receiver_clusters[cluster_idx];
    float3 cluster_center = float3(cl.world_center_x, cl.world_center_y, cl.world_center_z);
    float3 cluster_norm = float3(cl.normal_axis_x, cl.normal_axis_y, cl.normal_axis_z);

    for (uint lid = 0; lid < g_constants.total_lights; ++lid) {
        RTXSourceAngularFrame lf = g_light_frames[lid];
        float3 light_pos = float3(lf.origin_x, lf.origin_y, lf.origin_z);

        float3 to_light = light_pos - cluster_center;
        float dist = length(to_light);

        // Light range test
        if (dist > lf.range && lf.range > 0.0f) {
            InterlockedAdd(g_telemetry[16], 1); // K4 culled
            continue;
        }

        // Normal cone backface test
        if (dist > 1e-4f) {
            float3 to_l_dir = to_light / dist;
            float cos_angle = dot(cluster_norm, to_l_dir);
            if (cos_angle < -0.2f) {
                InterlockedAdd(g_telemetry[16], 1); // K4 culled
                continue;
            }
        }

        // Expand member probes into compact work queue using g_cluster_probe_indices
        uint p_count = cl.probe_count;
        for (uint p = 0; p < p_count; ++p) {
            uint p_idx = g_cluster_probe_indices[cl.probe_offset + p];
            if (p_idx < g_constants.total_probes) {
                uint work_idx;
                g_work_counter.InterlockedAdd(0, 1, work_idx);

                if (work_idx < g_constants.max_work_items) {
                    ASTGProbeLightWorkGPU item;
                    item.probe_id = p_idx;
                    item.actual_light_id = lf.light_id;
                    item.packed_light_index = lid;
                    item.dependency_generation = g_constants.current_generation;
                    g_probe_work_items[work_idx] = item;
                    InterlockedAdd(g_telemetry[17], 1); // K4 work emitted
                } else {
                    InterlockedAdd(g_telemetry[18], 1); // K4 work overflow
                }
            } else {
                InterlockedAdd(g_telemetry[24], 1); // Invalid probe index error
            }
        }
    }
}

// Pass K4.5: Build Indirect Dispatch Arguments for K5
[numthreads(1, 1, 1)]
void CSBuildReceiverDispatchArgs(uint3 id : SV_DispatchThreadID) {
    uint workCount = g_work_counter.Load(0);
    if (workCount > g_constants.max_work_items) workCount = g_constants.max_work_items;
    uint threadGroupsX = (workCount + 63) / 64;
    g_indirect_args.Store(0, threadGroupsX);
    g_indirect_args.Store(4, 1);
    g_indirect_args.Store(8, 1);
}

// Pass K5: Evaluate Receiver Visibility from Compact Work Queue
[numthreads(64, 1, 1)]
void CSEvaluateReceiverVisibility(uint3 id : SV_DispatchThreadID) {
    uint work_idx = id.x;
    uint total_work = g_work_counter.Load(0);
    if (work_idx >= total_work || work_idx >= g_constants.max_work_items) return;

    InterlockedAdd(g_telemetry[19], 1); // K5 work consumed

    ASTGProbeLightWorkGPU work = g_probe_work_items[work_idx];
    ASTGDynamicSurfaceProbeGPU pr = g_surface_probes[work.probe_id];
    RTXSourceAngularFrame lf = g_light_frames[work.packed_light_index];
    ASTGLightB0RangeGPU lr = g_light_ranges[work.packed_light_index];

    float3 probe_pos = float3(pr.world_pos_x, pr.world_pos_y, pr.world_pos_z);
    float3 probe_norm = float3(pr.world_norm_x, pr.world_norm_y, pr.world_norm_z);
    float3 light_pos = float3(lf.origin_x, lf.origin_y, lf.origin_z);

    float3 to_light = light_pos - probe_pos;
    float dist = length(to_light);
    float3 to_l_dir = (dist > 1e-5f) ? (to_light / dist) : float3(0, 1, 0);

    float n_dot_l = max(0.0f, dot(probe_norm, to_l_dir));
    bool visible = false;

    if (dist > 1e-4f && (lf.range <= 0.0f || dist <= lf.range) && n_dot_l > 0.001f) {
        InterlockedAdd(g_telemetry[20], 1); // K5 visibility tests
        float3 ray_origin = probe_pos + probe_norm * 0.01f;

        // AABB proxy mode occlusion
        bool is_occluded = false;
        for (uint b = 0; b < g_constants.total_bones; ++b) {
            ASTGBoneBoundGPU bb = g_bone_bounds[b];
            if (g_constants.is_skeletal == 0) {
                if (bb.group_id == pr.group_id) continue;
            } else {
                if (bb.group_id == pr.group_id && bb.bone_id == pr.bone_id) continue;
            }

            float3 bmin = float3(bb.world_min_x, bb.world_min_y, bb.world_min_z);
            float3 bmax = float3(bb.world_max_x, bb.world_max_y, bb.world_max_z);
            if (SegmentIntersectsAABB(ray_origin, light_pos, bmin, bmax)) {
                is_occluded = true;
                break;
            }
        }
        visible = !is_occluded;
    }

    ASTGProbeLightContributionGPU contrib;
    contrib.probe_id = work.probe_id;
    contrib.actual_light_id = work.actual_light_id;
    contrib.visibility = visible ? 1 : 0;
    contrib.dependency_generation = work.dependency_generation;

    if (visible) {
        InterlockedAdd(g_telemetry[21], 1); // K5 visible results
        float light_int = lr.intensity;
        float attenuation = light_int * n_dot_l / (dist * dist + 0.1f);
        contrib.irradiance_r = lr.color_r * attenuation;
        contrib.irradiance_g = lr.color_g * attenuation;
        contrib.irradiance_b = lr.color_b * attenuation;

        // Scalable atomic float accumulation into probe buffer (16-byte stride per probe)
        InterlockedAddFloat(g_probe_irradiance_accum, work.probe_id * 16 + 0, contrib.irradiance_r);
        InterlockedAddFloat(g_probe_irradiance_accum, work.probe_id * 16 + 4, contrib.irradiance_g);
        InterlockedAddFloat(g_probe_irradiance_accum, work.probe_id * 16 + 8, contrib.irradiance_b);
    } else {
        contrib.irradiance_r = 0.0f;
        contrib.irradiance_g = 0.0f;
        contrib.irradiance_b = 0.0f;
    }
    contrib.pad = 0.0f;

    g_contributions[work_idx] = contrib;
}

// Pass K6: Gather and Finalize Multi-Light Irradiance per Probe (O(probes) scale)
[numthreads(64, 1, 1)]
void CSAccumulateReceiverIrradiance(uint3 id : SV_DispatchThreadID) {
    uint probe_idx = id.x;
    if (probe_idx >= g_constants.total_probes) return;

    float r = asfloat(g_probe_irradiance_accum.Load(probe_idx * 16 + 0));
    float g = asfloat(g_probe_irradiance_accum.Load(probe_idx * 16 + 4));
    float b = asfloat(g_probe_irradiance_accum.Load(probe_idx * 16 + 8));

    g_surface_probes[probe_idx].irradiance_r = r;
    g_surface_probes[probe_idx].irradiance_g = g;
    g_surface_probes[probe_idx].irradiance_b = b;

    InterlockedAdd(g_telemetry[22], 1); // K6 contributions reduced
}
