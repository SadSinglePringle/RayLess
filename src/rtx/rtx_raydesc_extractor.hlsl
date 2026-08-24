// ==============================================================================
// RAYLESS GPU-DRIVEN ASTG RAYDESC EXTRACTOR (CHALLENGER 2 HARNESS)
// Purpose: Captures exact GPU register-synthesized RayDesc values for empirical validation
// ==============================================================================

struct ASTGGPUNode {
    float3 position;
    uint   active_flags;
    float3 normal;
    uint   generation;
    float3 diffuse_albedo;
    uint   chunk_id;
};

struct ASTGGPUDAGEdge {
    uint source_node_id;
    uint dest_node_id;
    uint generation;
    uint edge_state;
    uint source_light_id;
    uint angular_cell_id;
    uint destruction_chunk_id;
    uint flags;
};

struct ASTGGPUVisibilityCandidate {
    uint edge_id;
    uint object_id;
    uint transport_generation;
};

struct SynthesizedRayDescOutput {
    float3 origin;
    float  t_min;
    float3 direction;
    float  t_max;
    uint   is_degenerate;
    uint   is_stale;
    float  raw_dist;
    uint   pad;
};

cbuffer TransportConstants : register(b0) {
    uint g_candidate_count;
    uint g_total_nodes;
    uint g_total_edges;
    uint g_current_scene_generation;
    uint g_dynamic_occlusion_mode;
    uint g_destroyed_chunk_mask;
    uint g_flags;
    uint g_pad;
};

StructuredBuffer<ASTGGPUNode>                g_nodes      : register(t1);
StructuredBuffer<ASTGGPUDAGEdge>            g_edges      : register(t2);
StructuredBuffer<ASTGGPUVisibilityCandidate> g_candidates : register(t3);
RWStructuredBuffer<SynthesizedRayDescOutput> g_ray_descs  : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint idx = dispatchThreadId.x;
    if (idx >= g_candidate_count) return;

    ASTGGPUVisibilityCandidate cand = g_candidates[idx];
    SynthesizedRayDescOutput out_ray;
    out_ray.origin = float3(0, 0, 0);
    out_ray.t_min = 0.0f;
    out_ray.direction = float3(0, 0, 0);
    out_ray.t_max = 0.0f;
    out_ray.is_degenerate = 0;
    out_ray.is_stale = 0;
    out_ray.raw_dist = 0.0f;
    out_ray.pad = 0;

    if (cand.edge_id >= g_total_edges) {
        out_ray.is_stale = 1;
        g_ray_descs[idx] = out_ray;
        return;
    }

    ASTGGPUDAGEdge edge = g_edges[cand.edge_id];
    if (edge.generation != cand.transport_generation || (edge.flags & 0x1) == 0 || edge.edge_state == 1) {
        out_ray.is_stale = 1;
        g_ray_descs[idx] = out_ray;
        return;
    }

    if (edge.source_node_id >= g_total_nodes || edge.dest_node_id >= g_total_nodes) {
        out_ray.is_stale = 1;
        g_ray_descs[idx] = out_ray;
        return;
    }

    ASTGGPUNode src_node = g_nodes[edge.source_node_id];
    ASTGGPUNode dst_node = g_nodes[edge.dest_node_id];

    if ((src_node.active_flags & 0x1) == 0 || (dst_node.active_flags & 0x1) == 0) {
        out_ray.is_stale = 1;
        g_ray_descs[idx] = out_ray;
        return;
    }

    float3 p_src = src_node.position;
    float3 p_dst = dst_node.position;
    float3 n_src = src_node.normal;

    float3 delta = p_dst - p_src;
    float dist = length(delta);
    out_ray.raw_dist = dist;

    if (dist < 1e-4f) {
        out_ray.is_degenerate = 1;
        g_ray_descs[idx] = out_ray;
        return;
    }

    float3 dir = delta / dist;
    float3 origin = p_src;
    if (dot(n_src, n_src) > 0.1f) {
        origin += n_src * 0.005f;
    }
    origin += dir * 0.001f;

    out_ray.origin = origin;
    out_ray.direction = dir;
    out_ray.t_min = 0.001f;
    out_ray.t_max = max(0.001f, dist - 0.02f);
    out_ray.is_degenerate = 0;
    out_ray.is_stale = 0;

    g_ray_descs[idx] = out_ray;
}
