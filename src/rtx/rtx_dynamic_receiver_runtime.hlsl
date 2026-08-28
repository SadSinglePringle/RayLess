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
    uint light_id;
    uint group_id;
    uint dependency_generation;
};

struct ASTGPartKConstants {
    uint total_bones;
    uint total_clusters;
    uint total_probes;
    uint total_lights;
    uint current_generation;
    uint is_skeletal;
    uint dynamic_occlusion_mode;
    uint pad;
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

RWStructuredBuffer<ASTGBoneBoundGPU>            g_bone_bounds       : register(u0);
RWStructuredBuffer<ASTGReceiverClusterGPU>      g_receiver_clusters : register(u1);
RWStructuredBuffer<ASTGDynamicSurfaceProbeGPU>  g_surface_probes    : register(u2);
RWStructuredBuffer<ASTGProbeLightWorkGPU>       g_probe_work_items  : register(u3);
RWByteAddressBuffer                             g_work_counter      : register(u4);
RWStructuredBuffer<uint>                        g_telemetry         : register(u5);

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
    InterlockedAdd(g_telemetry[0], 1); // Bones transformed
}

// Pass K2: Transform Receiver Clusters
[numthreads(64, 1, 1)]
void CSTransformReceiverClusters(uint3 id : SV_DispatchThreadID) {
    uint cluster_idx = id.x;
    if (cluster_idx >= g_constants.total_clusters) return;

    ASTGReceiverClusterGPU cl = g_receiver_clusters[cluster_idx];
    ASTGBoneTransformGPU bt = g_bone_transforms[cl.bone_id];

    // Compute cluster center from member surface probes
    float3 center = float3(0, 0, 0);
    float3 norm_sum = float3(0, 0, 0);
    uint count = cl.probe_count;

    if (count > 0) {
        for (uint p = 0; p < count; ++p) {
            uint p_idx = cl.probe_offset + p;
            if (p_idx < g_constants.total_probes) {
                ASTGDynamicSurfaceProbeGPU pr = g_surface_probes[p_idx];
                center += float3(pr.world_pos_x, pr.world_pos_y, pr.world_pos_z);
                norm_sum += float3(pr.world_norm_x, pr.world_norm_y, pr.world_norm_z);
            }
        }
        center /= float(count);
        float norm_len = length(norm_sum);
        if (norm_len > 1e-4f) norm_sum /= norm_len;
        else norm_sum = float3(0, 1, 0);

        float max_r = 0.1f;
        for (uint i = 0; i < count; ++i) {
            uint p_idx = cl.probe_offset + i;
            if (p_idx < g_constants.total_probes) {
                ASTGDynamicSurfaceProbeGPU pr = g_surface_probes[p_idx];
                float3 ppos = float3(pr.world_pos_x, pr.world_pos_y, pr.world_pos_z);
                max_r = max(max_r, length(ppos - center));
            }
        }

        cl.world_center_x = center.x; cl.world_center_y = center.y; cl.world_center_z = center.z;
        cl.radius = max_r;
        cl.normal_axis_x = norm_sum.x; cl.normal_axis_y = norm_sum.y; cl.normal_axis_z = norm_sum.z;
        cl.cos_normal_half_angle = 0.5f; // ~60 deg coverage
        cl.generation = g_constants.current_generation;
    }

    g_receiver_clusters[cluster_idx] = cl;
    InterlockedAdd(g_telemetry[1], 1); // Clusters transformed
}

// Pass K3: Transform Surface Probes
[numthreads(64, 1, 1)]
void CSTransformSurfaceProbes(uint3 id : SV_DispatchThreadID) {
    uint probe_idx = id.x;
    if (probe_idx >= g_constants.total_probes) return;

    ASTGDynamicSurfaceProbeGPU pr = g_surface_probes[probe_idx];
    ASTGBoneTransformGPU bt = g_bone_transforms[pr.bone_id];

    float3 local_pos = float3(pr.local_pos_x, pr.local_pos_y, pr.local_pos_z);
    float3 local_norm = float3(pr.local_norm_x, pr.local_norm_y, pr.local_norm_z);

    float3 world_pos = TransformPoint(bt.row0, bt.row1, bt.row2, bt.row3, local_pos);
    float3 world_norm = TransformVector(bt.row0, bt.row1, bt.row2, local_norm);
    float norm_len = length(world_norm);
    if (norm_len > 1e-4f) world_norm /= norm_len;
    else world_norm = float3(0, 1, 0);

    pr.world_pos_x = world_pos.x; pr.world_pos_y = world_pos.y; pr.world_pos_z = world_pos.z;
    pr.world_norm_x = world_norm.x; pr.world_norm_y = world_norm.y; pr.world_norm_z = world_norm.z;
    pr.generation = g_constants.current_generation;

    // Reset direct irradiance accumulator for fresh lighting evaluation
    pr.irradiance_r = 0.0f;
    pr.irradiance_g = 0.0f;
    pr.irradiance_b = 0.0f;

    g_surface_probes[probe_idx] = pr;
    InterlockedAdd(g_telemetry[2], 1); // Probes transformed
}

// Pass K4: Cull Receiver Hierarchy (Bone & Cluster Culling) & Expand Probe Work Items
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
        if (dist > lf.range && lf.range > 0.0f) continue;

        // Normal cone backface test
        if (dist > 1e-4f) {
            float3 to_l_dir = to_light / dist;
            float cos_angle = dot(cluster_norm, to_l_dir);
            if (cos_angle < -0.2f) continue; // Behind cluster
        }

        // Expand member probes into work queue
        for (uint p = 0; p < cl.probe_count; ++p) {
            uint p_idx = cl.probe_offset + p;
            if (p_idx < g_constants.total_probes) {
                uint work_idx;
                g_work_counter.InterlockedAdd(0, 1, work_idx);

                if (work_idx < 131072) {
                    ASTGProbeLightWorkGPU item;
                    item.probe_id = p_idx;
                    item.light_id = lid;
                    item.group_id = g_surface_probes[p_idx].group_id;
                    item.dependency_generation = g_constants.current_generation;
                    g_probe_work_items[work_idx] = item;
                }
                InterlockedAdd(g_telemetry[3], 1); // Probe work items scheduled
            }
        }
    }
}

// Pass K5: Evaluate Receiver Visibility & Accumulate Irradiance per probe
[numthreads(64, 1, 1)]
void CSEvaluateReceiverVisibility(uint3 id : SV_DispatchThreadID) {
    uint probe_idx = id.x;
    if (probe_idx >= g_constants.total_probes) return;

    ASTGDynamicSurfaceProbeGPU pr = g_surface_probes[probe_idx];
    float3 probe_pos = float3(pr.world_pos_x, pr.world_pos_y, pr.world_pos_z);
    float3 probe_norm = float3(pr.world_norm_x, pr.world_norm_y, pr.world_norm_z);

    float3 ray_origin = probe_pos + probe_norm * 0.01f;
    float3 accum_irradiance = float3(0.0f, 0.0f, 0.0f);
    uint vis_mask = 0;

    for (uint lid = 0; lid < g_constants.total_lights; ++lid) {
        RTXSourceAngularFrame lf = g_light_frames[lid];
        float3 light_pos = float3(lf.origin_x, lf.origin_y, lf.origin_z);
        float3 to_light = light_pos - ray_origin;
        float dist = length(to_light);

        if (dist <= 1e-4f) continue;
        if (dist > lf.range && lf.range > 0.0f) continue;

        float3 to_l_dir = to_light / dist;
        float n_dot_l = max(0.0f, dot(probe_norm, to_l_dir));
        if (n_dot_l <= 0.001f) continue;

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

            float3 d = light_pos - ray_origin;
            float tmin = 0.0f;
            float tmax = 1.0f;
            bool hit_box = true;

            [unroll]
            for (int i = 0; i < 3; ++i) {
                float org = (i == 0) ? ray_origin.x : ((i == 1) ? ray_origin.y : ray_origin.z);
                float dir = (i == 0) ? d.x : ((i == 1) ? d.y : d.z);
                float bm = (i == 0) ? bmin.x : ((i == 1) ? bmin.y : bmin.z);
                float bx = (i == 0) ? bmax.x : ((i == 1) ? bmax.y : bmax.z);

                if (abs(dir) < 1e-7f) {
                    if (org < bm || org > bx) { hit_box = false; break; }
                } else {
                    float ood = 1.0f / dir;
                    float t1 = (bm - org) * ood;
                    float t2 = (bx - org) * ood;
                    if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
                    tmin = max(tmin, t1);
                    tmax = min(tmax, t2);
                    if (tmin > tmax) { hit_box = false; break; }
                }
            }

            if (hit_box && tmin <= tmax && tmax >= 0.0f && tmin <= 1.0f) {
                is_occluded = true;
                break;
            }
        }

        if (!is_occluded && g_constants.total_probes <= 64) {
            for (uint q = 0; q < g_constants.total_probes; ++q) {
                if (q == probe_idx) continue;
                ASTGDynamicSurfaceProbeGPU q_pr = g_surface_probes[q];
                if (q_pr.group_id != pr.group_id) continue;
                float3 q_pos = float3(q_pr.world_pos_x, q_pr.world_pos_y, q_pr.world_pos_z);
                float3 q_to_light = light_pos - q_pos;
                float q_dist = length(q_to_light);
                if (q_dist < dist - 0.4f) {
                    float3 ab = light_pos - probe_pos;
                    float3 ap = q_pos - probe_pos;
                    float t = dot(ap, ab) / (dist * dist);
                    if (t > 0.05f && t < 0.95f) {
                        float3 proj = probe_pos + ab * t;
                        float d_perp = length(q_pos - proj);
                        if (d_perp < 0.25f) {
                            is_occluded = true;
                            break;
                        }
                    }
                }
            }
        }

        InterlockedAdd(g_telemetry[4], 1); // Visibility rays evaluated

        if (!is_occluded) {
            ASTGLightB0RangeGPU lr = g_light_ranges[lid];
            float3 light_col = float3(lr.color_r, lr.color_g, lr.color_b);
            float light_int = lr.intensity;
            float attenuation = light_int * n_dot_l / (dist * dist + 0.1f);

            accum_irradiance += light_col * attenuation;
            vis_mask |= (1u << (lid % 32));

            InterlockedAdd(g_telemetry[5], 1); // Irradiance accumulated
        }
    }

    g_surface_probes[probe_idx].irradiance_r = accum_irradiance.x;
    g_surface_probes[probe_idx].irradiance_g = accum_irradiance.y;
    g_surface_probes[probe_idx].irradiance_b = accum_irradiance.z;
    g_surface_probes[probe_idx].last_visibility_mask = vis_mask;
}

// Pass K6: Accumulate Irradiance / Indirect Deposition Pass
[numthreads(64, 1, 1)]
void CSAccumulateReceiverIrradiance(uint3 id : SV_DispatchThreadID) {
    // Clustered irradiance reduction / temporal EMA smoothing
}
