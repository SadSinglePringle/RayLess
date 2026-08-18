#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 3D Vector for C-ABI interop
struct RTXVector3 {
    float x, y, z;
};

// Vertex with Position and Normal
struct RTXVertex {
    float px, py, pz;
    float nx, ny, nz;
};

// Unified Ray representation consumed identically by Software & Hardware RT Backends
struct ASTGRay {
    float origin_x, origin_y, origin_z;
    float t_min;

    float dir_x, dir_y, dir_z;
    float t_max;

    uint32_t source_light_id;
    uint32_t transport_node_id;
    uint32_t angular_cell_id;
    uint32_t flags;
};
typedef struct ASTGRay RTXRay;

// Primitive metadata table entry on GPU
struct PrimitiveMetadata {
    uint32_t mesh_id;
    uint32_t surface_cluster_id;
    uint32_t destruction_chunk_id;
    uint32_t material_id;
};

// Unified Hit representation returned by Hardware & Software Backends
struct ASTGRayHit {
    uint32_t hit;
    float distance;

    uint32_t instance_id;
    uint32_t primitive_id;

    float barycentric_u;
    float barycentric_v;

    uint32_t mesh_id;
    uint32_t surface_cluster_id;
    uint32_t destruction_chunk_id;
    uint32_t material_id;

    float normal_x;
    float normal_y;
    float normal_z;
    float pos_x;
    float pos_y;
    float pos_z;
    float pad[2];
};
typedef struct ASTGRayHit RTXHit;

// Traversal performance metrics recorded via GPU Timestamps
struct RTGPUTimings {
    double as_update_ms;
    double ray_generation_ms;
    double rt_traversal_ms;
    double hit_processing_ms;
    double total_gpu_ms;
    uint32_t rays_traced;
    uint32_t hits_recorded;
};

// ==============================================================================
// ASTG LATE-BOUND LIGHTING STATE & LAZY PROBE EVALUATION DATA STRUCTURES
// ==============================================================================

// Compact GPU-resident light state (32 bytes aligned)
struct LightState {
    float color_r;
    float color_g;
    float color_b;
    float intensity;
    uint32_t enabled;     // 1 = Active/Emitting, 0 = Disabled
    uint32_t generation;  // Monotonically increments on any late-bound property modification
    uint32_t flags;       // Type / shadow / classification flags
    uint32_t pad1;
};

// Immutable spatial and animation parameters for massive stationary lights (32 bytes)
struct LightStatic {
    float pos_x, pos_y, pos_z;
    float range;
    float dir_x, dir_y, dir_z;
    uint32_t type;         // 0 = Omni, 1 = Spot, 2 = Directional, 3 = Area
    float anim_phase;
    float anim_frequency;
    float base_hue;
    uint32_t flags;
};

// Dynamic runtime light state animated either by CPU or GPU compute shader (32 bytes)
typedef struct LightState LightDynamic;

// Sparse persistent transfer coefficient from a light to a probe
struct ProbeLightContribution {
    uint32_t light_id;
    float transfer_r;
    float transfer_g;
    float transfer_b;
};

// Cached probe irradiance entry with generation signature
struct ProbeCacheEntry {
    float irradiance_r;
    float irradiance_g;
    float irradiance_b;
    uint32_t last_eval_signature; // Signature/hash of contributing light generations
};

// Performance metrics for late-bound GPU probe refresh
struct LateBoundGPUTimings {
    double light_upload_ms;
    double probe_refresh_gpu_ms;
    double readback_ms;
    uint32_t probes_evaluated;
    uint32_t contributions_summed;
};

// VRAM breakdown across subsystems
struct ASTGVRAMBreakdown {
    uint64_t light_static_bytes;
    uint64_t light_dynamic_bytes;
    uint64_t transport_nodes_bytes;
    uint64_t transport_edges_bytes;
    uint64_t contribution_records_bytes;
    uint64_t probe_buffers_bytes;
    uint64_t angular_hierarchy_bytes;
    uint64_t rt_acceleration_structure_bytes;
    uint64_t total_astg_vram_bytes;
};

#ifdef __cplusplus
}
#endif
