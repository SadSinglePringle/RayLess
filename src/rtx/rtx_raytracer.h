#pragma once
#include "rtx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ASTG_RTX_EXPORTS
#define RTX_API __declspec(dllexport)
#else
#define RTX_API __declspec(dllimport)
#endif

// Initializes Direct3D 12 device and verifies NVIDIA RT Core hardware support (DXR 1.1)
RTX_API bool rtx_init();

// Builds BLAS and TLAS GPU Acceleration Structures with Primitive Metadata
RTX_API bool rtx_build_acceleration_structures(
    const RTXVertex* vertices,
    int32_t vertex_count,
    const uint32_t* indices,
    int32_t index_count,
    const PrimitiveMetadata* metadata,
    int32_t metadata_count
);

// Builds partitioned BLAS / TLAS with per-chunk instance masking
RTX_API bool rtx_build_partitioned_as(
    const RTXVertex* vertices,
    int32_t vertex_count,
    const uint32_t* indices,
    int32_t index_count,
    const PrimitiveMetadata* metadata,
    int32_t metadata_count,
    const int32_t* chunk_ids,
    int32_t chunk_count
);

// Dispatches rays directly to Hardware RT Cores via DXR 1.1 RayQuery compute shader
RTX_API int32_t rtx_trace_rays_batch(
    const ASTGRay* rays,
    ASTGRayHit* hits,
    int32_t ray_count
);

// Dispatches rays and retrieves exact GPU timestamp breakdown
RTX_API int32_t rtx_trace_rays_batch_with_timings(
    const ASTGRay* rays,
    ASTGRayHit* hits,
    int32_t ray_count,
    RTGPUTimings* timings
);

// Instantly invalidates/masks a destroyed chunk in the hardware TLAS
RTX_API bool rtx_destroy_chunk(int32_t chunk_id);

// Restores an invalidated chunk in the hardware TLAS
RTX_API bool rtx_restore_chunk(int32_t chunk_id);

// ==============================================================================
// ASTG LATE-BOUND LIGHTING STATE & LAZY PROBE REFRESH GPU APIS
// ==============================================================================

// Fast O(1) single light state update on GPU
RTX_API bool rtx_update_light_state(uint32_t light_id, const LightState* state);

// Batch upload all light states
RTX_API bool rtx_upload_all_light_states(const LightState* states, uint32_t count);

// Uploads sparse probe-to-light persistent transfer coefficients (CSR format)
RTX_API bool rtx_upload_probe_contributions(
    const ProbeLightContribution* contributions,
    uint32_t contribution_count,
    const uint32_t* offsets,
    const uint32_t* counts,
    uint32_t probe_count
);

// Evaluates sparse contributions on GPU for requested probe IDs in a single compute pass
RTX_API bool rtx_lazy_refresh_probes(
    const uint32_t* requested_probe_ids,
    uint32_t requested_count,
    LateBoundGPUTimings* timings
);

// Reads back refreshed probe cache entries to CPU
RTX_API bool rtx_readback_probe_cache(ProbeCacheEntry* out_cache, uint32_t probe_count);

// ==============================================================================
// ASTG MASSIVE STATIONARY LIGHT SYSTEM (UP TO 131,072 LIGHTS)
// ==============================================================================

// Initializes massive persistent stationary lights on GPU (up to 131,072)
RTX_API bool rtx_init_massive_lights(
    const LightStatic* static_lights,
    const LightDynamic* initial_dynamic,
    uint32_t count
);

// Dispatches GPU-driven parallel animation compute shader for up to 131,072 lights
RTX_API bool rtx_dispatch_gpu_light_animation(
    uint32_t light_count,
    float time_sec,
    uint32_t anim_mode,
    uint32_t frame_index,
    double* out_gpu_anim_ms
);

// Retrieves exact VRAM allocation breakdown across all subsystems
RTX_API bool rtx_get_vram_breakdown(ASTGVRAMBreakdown* breakdown);

// Retrieves timings from the most recent GPU ray dispatch
RTX_API bool rtx_get_last_timings(RTGPUTimings* timings);

// Returns active GPU device name
RTX_API const char* rtx_get_device_name();

// Returns true if hardware RT Cores (DXR 1.1 Tier 1.1) are active
RTX_API bool rtx_is_hardware_active();

// ==============================================================================
// ASTG GPU TRANSPORT & COMPACT CANDIDATE EVALUATION APIS (MILESTONE 1 - R1 & R2)
// ==============================================================================

// Uploads a contiguous or partial range of ASTGGPUNodes to the persistent GPU buffer
RTX_API bool rtx_upload_astg_nodes(const ASTGGPUNode* nodes, uint32_t offset, uint32_t count);

// Uploads a contiguous or partial range of ASTGGPUDAGEdges to the persistent GPU buffer
RTX_API bool rtx_upload_astg_edges(const ASTGGPUDAGEdge* edges, uint32_t offset, uint32_t count);

// Updates a dirty range of nodes in the persistent GPU staging buffer
RTX_API bool rtx_update_gpu_nodes_range(const ASTGGPUNode* nodes, uint32_t offset, uint32_t count);

// Updates a dirty range of edges in the persistent GPU staging buffer
RTX_API bool rtx_update_gpu_edges_range(const ASTGGPUDAGEdge* edges, uint32_t offset, uint32_t count);

// Synchronizes pending dirty ranges to persistent GPU transport buffers
RTX_API bool rtx_sync_gpu_transport_buffers();

// Reads back a range of ASTGGPUNodes from the persistent GPU buffer
RTX_API bool rtx_readback_astg_nodes(ASTGGPUNode* out_nodes, uint32_t offset, uint32_t count);

// Reads back a range of ASTGGPUDAGEdges from the persistent GPU buffer
RTX_API bool rtx_readback_astg_edges(ASTGGPUDAGEdge* out_edges, uint32_t offset, uint32_t count);

// Uploads dynamic occluder AABBs to persistent GPU buffer (Milestone 2 - R3)
RTX_API bool rtx_set_dynamic_occluders_gpu(const ASTGGPUOccluderAABB* occluders, uint32_t count);

// Dispatches a batch of compact ASTGGPUVisibilityCandidates to the GPU transport pipeline
RTX_API int32_t rtx_trace_candidates_batch(
    const ASTGGPUVisibilityCandidate* candidates,
    uint32_t candidate_count,
    ASTGEdgeVisibilityResult* out_results,
    ASTGVisibilityCounters* out_counters,
    RTGPUTimings* out_timings
);

// Releases all D3D12 / RT Core resources
RTX_API void rtx_shutdown();

#ifdef __cplusplus
}
#endif
