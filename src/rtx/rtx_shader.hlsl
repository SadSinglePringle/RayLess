// ==============================================================================
// DXR 1.1 HLSL RAY QUERY COMPUTE SHADER (NVIDIA RT CORES)
// Executes hardware BVH traversal and ray-triangle intersection on RT Cores.
// ==============================================================================

struct ASTGRay {
    float3 origin;
    float t_min;
    float3 dir;
    float t_max;
    uint source_light_id;
    uint transport_node_id;
    uint angular_cell_id;
    uint flags;
};

struct PrimitiveMetadata {
    uint mesh_id;
    uint surface_cluster_id;
    uint destruction_chunk_id;
    uint material_id;
};

struct ASTGRayHit {
    uint hit;
    float distance;
    uint instance_id;
    uint primitive_id;

    float barycentric_u;
    float barycentric_v;

    uint mesh_id;
    uint surface_cluster_id;
    uint destruction_chunk_id;
    uint material_id;

    float normal_x;
    float normal_y;
    float normal_z;
    float pos_x;
    float pos_y;
    float pos_z;
    float pad[2];
};

struct RTXVertex {
    float3 position;
    float3 normal;
};

// Acceleration Structure built into RTX Hardware BVH
RaytracingAccelerationStructure g_tlas : register(t0);

// Structured Buffers
StructuredBuffer<ASTGRay> g_rays : register(t1);
StructuredBuffer<RTXVertex> g_vertices : register(t2);
StructuredBuffer<uint> g_indices : register(t3);
StructuredBuffer<PrimitiveMetadata> g_primitive_metadata : register(t4);

// Output Hits
RWStructuredBuffer<ASTGRayHit> g_hits : register(u0);

cbuffer RayBatchConstants : register(b0) {
    uint g_ray_count;
    uint g_destroyed_chunk_mask;
    uint g_total_primitives;
    uint g_pad1;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint ray_idx = dispatchThreadId.x;
    if (ray_idx >= g_ray_count) {
        return;
    }

    ASTGRay in_ray = g_rays[ray_idx];

    RayDesc ray;
    ray.Origin = in_ray.origin;
    ray.Direction = in_ray.dir;
    ray.TMin = max(0.0001f, in_ray.t_min);
    ray.TMax = in_ray.t_max > 0.0f ? in_ray.t_max : 1000.0f;

    // Direct invocation of Hardware RT Cores via DXR 1.1 RayQuery
    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(
        g_tlas,
        RAY_FLAG_NONE,
        0xFF, // Instance Inclusion Mask
        ray
    );

    // Hardware RT Cores traverse BVH
    while (q.Proceed()) {
        // Candidate intersection testing if needed
    }

    ASTGRayHit out_hit;
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        float hit_t = q.CommittedRayT();
        uint prim_id = q.CommittedPrimitiveIndex();
        uint instance_id = q.CommittedInstanceID();
        float2 bary = q.CommittedTriangleBarycentrics();

        out_hit.hit = 1;
        out_hit.distance = hit_t;
        out_hit.instance_id = instance_id;
        out_hit.primitive_id = prim_id;
        out_hit.barycentric_u = bary.x;
        out_hit.barycentric_v = bary.y;

        float3 hit_pos = in_ray.origin + in_ray.dir * hit_t;
        out_hit.pos_x = hit_pos.x;
        out_hit.pos_y = hit_pos.y;
        out_hit.pos_z = hit_pos.z;

        // Fetch primitive metadata
        if (prim_id < g_total_primitives) {
            PrimitiveMetadata meta = g_primitive_metadata[prim_id];
            out_hit.mesh_id = meta.mesh_id;
            out_hit.surface_cluster_id = meta.surface_cluster_id;
            out_hit.destruction_chunk_id = meta.destruction_chunk_id;
            out_hit.material_id = meta.material_id;
        } else {
            out_hit.mesh_id = instance_id;
            out_hit.surface_cluster_id = 0;
            out_hit.destruction_chunk_id = 0;
            out_hit.material_id = 0;
        }

        // Fetch triangle vertices and compute interpolated normal
        uint i0 = g_indices[prim_id * 3 + 0];
        uint i1 = g_indices[prim_id * 3 + 1];
        uint i2 = g_indices[prim_id * 3 + 2];

        float3 n0 = g_vertices[i0].normal;
        float3 n1 = g_vertices[i1].normal;
        float3 n2 = g_vertices[i2].normal;

        float u = bary.x;
        float v = bary.y;
        float w = 1.0f - u - v;
        float3 interp_norm = normalize(n0 * w + n1 * u + n2 * v);
        out_hit.normal_x = interp_norm.x;
        out_hit.normal_y = interp_norm.y;
        out_hit.normal_z = interp_norm.z;
    } else {
        out_hit.hit = 0;
        out_hit.distance = -1.0f;
        out_hit.instance_id = 0xFFFFFFFF;
        out_hit.primitive_id = 0xFFFFFFFF;
        out_hit.barycentric_u = 0.0f;
        out_hit.barycentric_v = 0.0f;
        out_hit.mesh_id = 0xFFFFFFFF;
        out_hit.surface_cluster_id = 0xFFFFFFFF;
        out_hit.destruction_chunk_id = 0xFFFFFFFF;
        out_hit.material_id = 0xFFFFFFFF;
        out_hit.normal_x = 0.0f;
        out_hit.normal_y = 1.0f;
        out_hit.normal_z = 0.0f;
        out_hit.pos_x = 0.0f;
        out_hit.pos_y = 0.0f;
        out_hit.pos_z = 0.0f;
    }

    out_hit.pad[0] = 0.0f;
    out_hit.pad[1] = 0.0f;

    g_hits[ray_idx] = out_hit;
}
