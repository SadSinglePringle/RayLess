// ==============================================================================
// RAYLESS GPU-DRIVEN ASTG TRANSPORT & BROADPHASE REJECTION PIPELINE (SM 6.5)
// Milestone 2 (R3): GPU Broadphase Rejection & Candidate Compaction
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

cbuffer TransportConstants : register(b0) {
    uint g_candidate_count;
    uint g_total_nodes;
    uint g_total_edges;
    uint g_current_scene_generation;
    uint g_dynamic_occlusion_mode;
    uint g_occluder_count;
    uint g_destroyed_chunk_mask;
    uint g_flags;
};

// Acceleration Structure
RaytracingAccelerationStructure g_tlas : register(t0);

// Structured Buffers
StructuredBuffer<ASTGGPUNode>                g_nodes      : register(t1);
StructuredBuffer<ASTGGPUDAGEdge>            g_edges      : register(t2);
StructuredBuffer<ASTGGPUVisibilityCandidate> g_candidates : register(t3);
StructuredBuffer<ASTGGPUOccluderAABB>        g_occluders  : register(t4);

// Output & Telemetry Buffers
RWStructuredBuffer<ASTGEdgeVisibilityResult> g_results  : register(u0);
RWStructuredBuffer<uint>                     g_counters : register(u1);

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

// Ray vs AABB intersection test (P(t) = origin + t*dir, t in [0, max_dist])
bool RayIntersectsAABB(float3 origin, float3 dir, float3 box_min, float3 box_max, float max_dist) {
    const float eps = 1e-7f;
    float tmin = 0.0f;
    float tmax = max_dist;

    [unroll]
    for (int i = 0; i < 3; ++i) {
        float di = dir[i];
        float oi = origin[i];
        float min_i = box_min[i];
        float max_i = box_max[i];

        if (abs(di) < eps) {
            if (oi < min_i || oi > max_i) {
                return false;
            }
        } else {
            float inv_d = 1.0f / di;
            float t1 = (min_i - oi) * inv_d;
            float t2 = (max_i - oi) * inv_d;
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

// Decodes octahedral (u, v) in [0, 1]^2 to 3D unit vector
float3 DecodeOctahedral(float2 uv) {
    float2 p = uv * 2.0f - 1.0f;
    float3 d = float3(p.x, p.y, 1.0f - abs(p.x) - abs(p.y));
    if (d.z < 0.0f) {
        float old_dx = d.x;
        d.x = (1.0f - abs(d.y)) * (old_dx >= 0.0f ? 1.0f : -1.0f);
        d.y = (1.0f - abs(old_dx)) * (d.y >= 0.0f ? 1.0f : -1.0f);
    }
    float len = length(d);
    return (len > 1e-6f) ? (d / len) : float3(0.0f, 1.0f, 0.0f);
}

// Computes 64-bin cell index in [0, 63] (8x8 leaf cells)
uint GetOctahedralCellId(float3 dir) {
    float2 uv = EncodeOctahedral(dir);
    uint cx = min(7u, (uint)(uv.x * 8.0f));
    uint cy = min(7u, (uint)(uv.y * 8.0f));
    return cy * 8u + cx;
}

// Checks if cell_id (0..63) is set in 64-bit mask represented by uint2
bool IsCellInAngularMask(uint2 mask, uint cell_id) {
    if (cell_id < 32u) {
        return (mask.x & (1u << cell_id)) != 0u;
    } else {
        return (mask.y & (1u << (cell_id - 32u))) != 0u;
    }
}

// Projects an AABB from origin point into a 64-bit angular footprint bitmask (uint2)
uint2 QueryBoxFootprintHLSL(float3 light_pos, float3 box_min, float3 box_max) {
    float3 center = (box_min + box_max) * 0.5f;
    float3 to_center = center - light_pos;
    float dist = length(to_center);
    float max_dist = dist * 2.0f + 5.0f;

    // 8 box corners
    float3 corners[8] = {
        float3(box_min.x, box_min.y, box_min.z),
        float3(box_max.x, box_min.y, box_min.z),
        float3(box_min.x, box_max.y, box_min.z),
        float3(box_max.x, box_max.y, box_min.z),
        float3(box_min.x, box_min.y, box_max.z),
        float3(box_max.x, box_min.y, box_max.z),
        float3(box_min.x, box_max.y, box_max.z),
        float3(box_max.x, box_max.y, box_max.z)
    };

    float2 uv_corners[8];
    [unroll]
    for (int k = 0; k < 8; ++k) {
        float3 d = corners[k] - light_pos;
        float len = length(d);
        float3 d_norm = (len > 1e-6f) ? (d / len) : float3(0, 1, 0);
        uv_corners[k] = EncodeOctahedral(d_norm);
    }

    uint2 mask = uint2(0, 0);

    for (uint i = 0; i < 64u; ++i) {
        uint cx = i % 8u;
        uint cy = i / 8u;
        float u_min = float(cx) / 8.0f;
        float u_max = float(cx + 1u) / 8.0f;
        float v_min = float(cy) / 8.0f;
        float v_max = float(cy + 1u) / 8.0f;

        float2 uv_center = float2((u_min + u_max) * 0.5f, (v_min + v_max) * 0.5f);
        float3 dir_center = DecodeOctahedral(uv_center);

        bool cell_hit = false;

        // 1. Cell center ray intersection
        if (RayIntersectsAABB(light_pos, dir_center, box_min, box_max, max_dist)) {
            cell_hit = true;
        }

        // 2. Corner ray intersections
        if (!cell_hit) {
            float3 c0 = DecodeOctahedral(float2(u_min, v_min));
            float3 c1 = DecodeOctahedral(float2(u_max, v_min));
            float3 c2 = DecodeOctahedral(float2(u_min, v_max));
            float3 c3 = DecodeOctahedral(float2(u_max, v_max));
            if (RayIntersectsAABB(light_pos, c0, box_min, box_max, max_dist) ||
                RayIntersectsAABB(light_pos, c1, box_min, box_max, max_dist) ||
                RayIntersectsAABB(light_pos, c2, box_min, box_max, max_dist) ||
                RayIntersectsAABB(light_pos, c3, box_min, box_max, max_dist)) {
                cell_hit = true;
            }
        }

        // 3. Box corner projection into cell UV bounds
        if (!cell_hit) {
            [unroll]
            for (int c = 0; c < 8; ++c) {
                if (uv_corners[c].x >= u_min && uv_corners[c].x <= u_max &&
                    uv_corners[c].y >= v_min && uv_corners[c].y <= v_max) {
                    cell_hit = true;
                    break;
                }
            }
        }

        if (cell_hit) {
            if (i < 32u) {
                mask.x |= (1u << i);
            } else {
                mask.y |= (1u << (i - 32u));
            }
        }
    }

    return mask;
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

    ASTGGPUVisibilityCandidate cand = g_candidates[idx];

    // =========================================================================
    // STAGE 1: 4-TIER MONOTONIC GENERATION & STRUCTURAL VALIDATION
    // =========================================================================
    bool is_gen_invalid = false;
    uint edge_gen = 0;

    if (cand.edge_id >= g_total_edges) {
        is_gen_invalid = true;
    } else {
        ASTGGPUDAGEdge edge = g_edges[cand.edge_id];
        edge_gen = edge.generation;

        if (edge.generation != cand.transport_generation ||
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
        ASTGEdgeVisibilityResult res;
        res.edge_id = cand.edge_id;
        res.visibility_state = 2; // INVALID_GENERATION
        res.generation = edge_gen;
        g_results[idx] = res;
        return;
    }

    ASTGGPUDAGEdge edge = g_edges[cand.edge_id];
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
        ASTGEdgeVisibilityResult res;
        res.edge_id = cand.edge_id;
        res.visibility_state = 0; // VISIBLE (bypasses occlusion)
        res.generation = edge.generation;
        g_results[idx] = res;
        return;
    }

    // =========================================================================
    // STAGE 3: GPU BROADPHASE REJECTION (SEGMENT VS AABB SLAB INTERSECTION)
    // =========================================================================
    bool is_broadphase_rejected = false;

    if (g_dynamic_occlusion_mode != 0 && g_occluder_count > 0) {
        if (cand.object_id != 0xFFFFFFFF) {
            // object_id is the stable ASTG dynamic-group ID, not a dense
            // upload-buffer index. Resolve it explicitly so unordered_map
            // iteration and sparse/reused group IDs remain correct.
            bool found_object = false;
            bool hit_object = false;
            [loop]
            for (uint o = 0; o < g_occluder_count; ++o) {
                ASTGGPUOccluderAABB occ = g_occluders[o];
                if (occ.group_id != cand.object_id || (occ.flags & 0x1) == 0) continue;
                found_object = true;
                hit_object = SegmentIntersectsAABB(p_src, p_dst, occ.min_bounds, occ.max_bounds);
                break;
            }
            if (!found_object || !hit_object) {
                is_broadphase_rejected = true;
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
        ASTGEdgeVisibilityResult res;
        res.edge_id = cand.edge_id;
        res.visibility_state = 3; // NO_QUERY_REQUIRED (slab missed dynamic occluder)
        res.generation = edge.generation;
        g_results[idx] = res;
        return;
    }

    // =========================================================================
    // STAGE 4: SURVIVING CANDIDATES -> SM 6.5 WAVE COMPACTION & DXR RAYQUERY
    // =========================================================================
    WaveInterlockedAdd(COUNTER_EDGES_CONSIDERED, true);
    WaveInterlockedAdd(COUNTER_RAYQUERY_CANDIDATES, true);

    // SM 6.5 Wave-level rank calculation for surviving candidates (0 intra-wave atomics)
    uint4 survive_ballot = WaveActiveBallot(true);
    uint lane_compact_rank = WavePrefixCountBits(true);
    uint wave_survivor_total = WaveActiveCountBits(true);

    if (dist < 1e-4f) {
        WaveInterlockedAdd(COUNTER_RAYQUERY_VISIBLE, true);
        ASTGEdgeVisibilityResult res;
        res.edge_id = cand.edge_id;
        res.visibility_state = 0; // VISIBLE
        res.generation = edge.generation;
        g_results[idx] = res;
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

    ASTGEdgeVisibilityResult res;
    res.edge_id = cand.edge_id;
    res.visibility_state = is_blocked ? 1 : 0;
    res.generation = edge.generation;
    g_results[idx] = res;
}
