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

// ==============================================================================
// ASTG GPU TRANSPORT & COMPACT VISIBILITY CANDIDATE DATA STRUCTURES (MILESTONE 1 - R1 & R2)
// ==============================================================================

// 48-byte GPU ASTG Transport Node (16-byte aligned)
// Matches HLSL StructuredBuffer<ASTGGPUNode> layout
#ifdef __cplusplus
struct alignas(16) ASTGGPUNode {
#else
typedef struct ASTGGPUNode {
#endif
    // Vector 0 (16 bytes, offset 0..15)
    float pos_x;                  // Offset 0
    float pos_y;                  // Offset 4
    float pos_z;                  // Offset 8
    uint32_t active_flags;        // Offset 12: bit 0: is_active, bit 1: is_bounce1, bit 2: is_stitched, bits 3-31: reserved

    // Vector 1 (16 bytes, offset 16..31)
    float normal_x;               // Offset 16
    float normal_y;               // Offset 20
    float normal_z;               // Offset 24
    uint32_t generation;          // Offset 28: Monotonically incrementing node mutation generation

    // Vector 2 (16 bytes, offset 32..47)
    float albedo_r;               // Offset 32: Diffuse albedo / path transfer R
    float albedo_g;               // Offset 36: Diffuse albedo / path transfer G
    float albedo_b;               // Offset 40: Diffuse albedo / path transfer B
    uint32_t chunk_id;            // Offset 44: Destruction chunk ID (0xFFFFFFFF = static geometry)
#ifdef __cplusplus
};
#else
} ASTGGPUNode;
#endif

// 32-byte GPU ASTG DAG Edge (16-byte aligned)
// Matches HLSL StructuredBuffer<ASTGGPUDAGEdge> layout
#ifdef __cplusplus
struct alignas(16) ASTGGPUDAGEdge {
#else
typedef struct ASTGGPUDAGEdge {
#endif
    // Vector 0 (16 bytes, offset 0..15)
    uint32_t source_node_id;       // Offset 0: Parent node index in g_nodes
    uint32_t dest_node_id;         // Offset 4: Child node index in g_nodes
    uint32_t generation;           // Offset 8: Edge mutation / repair generation
    uint32_t edge_state;           // Offset 12: 0=ACTIVE, 1=INVALID_STATIC, 2=OCCLUDED_DYNAMIC

    // Vector 1 (16 bytes, offset 16..31)
    uint32_t source_light_id;      // Offset 16: Emitting light ID for filtering
    uint32_t angular_cell_id;      // Offset 20: Octahedral angular cell ID (0..63)
    uint32_t destruction_chunk_id; // Offset 24: Associated chunk ID (0xFFFFFFFF = static)
    uint32_t flags;                // Offset 28: Bit 0: is_active, Bit 1: is_stitch, Bits 2-3: bounce_depth
#ifdef __cplusplus
};
#else
} ASTGGPUDAGEdge;
#endif

// 16-byte Compact Visibility Candidate Record (R2)
// `occluder_index` is a dense GPU-buffer index. `object_id` remains the
// stable ASTG group identity and is validated by the shader before use.
struct ASTGGPUVisibilityCandidate {
    uint32_t edge_id;               // Offset 0: Index into g_edges
    uint32_t object_id;             // Offset 4: Dynamic occluder group ID (or 0xFFFFFFFF if static/general)
    uint32_t transport_generation;  // Offset 8: Host DAG transport generation at query time
    uint32_t occluder_index;        // Offset 12: Dense index into g_occluders (or 0xFFFFFFFF)
};
typedef struct ASTGGPUVisibilityCandidate ASTGGPUVisibilityCandidate;

// 12-byte Compact Visibility Result / Changed-State Return
struct ASTGEdgeVisibilityResult {
    uint32_t edge_id;               // Offset 0: Edge ID
    uint32_t visibility_state;      // Offset 4: 0 = VISIBLE, 1 = BLOCKED, 2 = INVALID_GENERATION, 3 = NO_QUERY_REQUIRED
    uint32_t generation;           // Offset 8: Edge generation observed during traversal
};
typedef struct ASTGEdgeVisibilityResult ASTGEdgeVisibilityResult;

// 32-byte Traversal Performance & Culling Counters
struct ASTGVisibilityCounters {
    uint32_t edges_considered;      // Total candidates dispatched
    uint32_t generation_rejected;   // Candidates rejected by stale generation or invalid edge
    uint32_t angular_rejected;      // Candidates rejected by angular footprint
    uint32_t broadphase_rejected;   // Candidates rejected by AABB slab test
    uint32_t rayquery_candidates;   // Surviving candidates submitted to hardware RT
    uint32_t rayquery_blocked;      // RayQuery blocked hits
    uint32_t rayquery_visible;      // RayQuery unobstructed hits
    uint32_t changed_state_count;   // Edges with mutated visibility state
};
typedef struct ASTGVisibilityCounters ASTGVisibilityCounters;

// Persistent visibility state is scoped to a stable dynamic-object slot and
// edge ID. Generation is stored separately so edge-ID reuse cannot inherit an
// old object's visibility decision.
struct ASTGPersistentVisibilityState {
    uint32_t edge_generation;
    uint32_t visibility_state; // 0 = visible, 1 = blocked; zero generation = invalid
};
typedef struct ASTGPersistentVisibilityState ASTGPersistentVisibilityState;

// 32-byte GPU ASTG Dynamic Occluder AABB (16-byte aligned)
// Matches HLSL StructuredBuffer<ASTGGPUOccluderAABB> layout (Milestone 2 - R3)
#ifdef __cplusplus
struct alignas(16) ASTGGPUOccluderAABB {
#else
typedef struct ASTGGPUOccluderAABB {
#endif
    // Vector 0 (16 bytes, offset 0..15)
    float min_x;           // Offset 0: AABB min X
    float min_y;           // Offset 4: AABB min Y
    float min_z;           // Offset 8: AABB min Z
    uint32_t group_id;     // Offset 12: Associated Dynamic Group ID (matches Candidate::object_id)

    // Vector 1 (16 bytes, offset 16..31)
    float max_x;           // Offset 16: AABB max X
    float max_y;           // Offset 20: AABB max Y
    float max_z;           // Offset 24: AABB max Z
    uint32_t flags;        // Offset 28: Bit 0: is_active, Bit 1: bounds_only, Bit 2: is_skeletal, Bits 3-31: reserved
#ifdef __cplusplus
};
#else
} ASTGGPUOccluderAABB;
#endif

// A contiguous range of edge IDs belonging to one exact spatial cell. The
// CPU uploads only ranges overlapping a changed object's swept AABB; edge IDs
// remain resident on the GPU and are discovered there.
struct ASTGGPUCellRange {
    uint32_t edge_index_offset;
    uint32_t edge_index_count;
    uint32_t dispatch_offset; // Prefix offset in this dispatch's edge-reference stream
};
typedef struct ASTGGPUCellRange ASTGGPUCellRange;

// 32-byte Constant Buffer for GPU Transport Traversal
struct TransportConstants {
    uint32_t candidate_count;           // Offset 0: Total candidate queries in batch
    uint32_t total_nodes;               // Offset 4: Size of g_nodes buffer
    uint32_t total_edges;               // Offset 8: Size of g_edges buffer
    uint32_t current_scene_generation;  // Offset 12: Host DAG mutation generation
    uint32_t dynamic_occlusion_mode;    // Offset 16: 0=NONE, 1=DAG_ALL_BOUNCES, 2=ANGULAR_B0_DAG_B1
    uint32_t occluder_count;            // Offset 20: Number of active dynamic occluders in g_occluders
    uint32_t destroyed_chunk_mask;      // Offset 24: Bitmask of destroyed chunks
    uint32_t flags;                     // Offset 28: Pipeline control flags (bit 0 = GPU spatial discovery)
    uint32_t discovery_range_count;     // Offset 32
    uint32_t discovery_object_id;       // Offset 36: stable group ID
    uint32_t discovery_occluder_index;  // Offset 40: dense GPU occluder index
    uint32_t discovery_stamp;           // Offset 44: dedup stamp for this object update
    uint32_t visibility_state_slot;     // Offset 48: stable dynamic-object state slice
};
typedef struct TransportConstants TransportConstants;

#ifdef __cplusplus
}

#include <cstddef>
// Static Assertions for ASTGGPUVisibilityCandidate (16 bytes)
static_assert(sizeof(ASTGGPUVisibilityCandidate) == 16, "ASTGGPUVisibilityCandidate size must be exactly 16 bytes");
static_assert(alignof(ASTGGPUVisibilityCandidate) == 4, "ASTGGPUVisibilityCandidate alignment must be 4 bytes");
static_assert(offsetof(ASTGGPUVisibilityCandidate, edge_id) == 0, "ASTGGPUVisibilityCandidate::edge_id offset != 0");
static_assert(offsetof(ASTGGPUVisibilityCandidate, object_id) == 4, "ASTGGPUVisibilityCandidate::object_id offset != 4");
static_assert(offsetof(ASTGGPUVisibilityCandidate, occluder_index) == 12, "ASTGGPUVisibilityCandidate::occluder_index offset != 12");
static_assert(offsetof(ASTGGPUVisibilityCandidate, transport_generation) == 8, "ASTGGPUVisibilityCandidate::transport_generation offset != 8");
static_assert(sizeof(ASTGGPUCellRange) == 12, "ASTGGPUCellRange size must be exactly 12 bytes");

// Static Assertions for ASTGGPUNode (48 bytes, 16-byte aligned)
static_assert(sizeof(ASTGGPUNode) == 48, "ASTGGPUNode size must be exactly 48 bytes");
static_assert(alignof(ASTGGPUNode) == 16, "ASTGGPUNode alignment must be 16 bytes");
static_assert(offsetof(ASTGGPUNode, pos_x) == 0, "ASTGGPUNode::pos_x offset != 0");
static_assert(offsetof(ASTGGPUNode, pos_y) == 4, "ASTGGPUNode::pos_y offset != 4");
static_assert(offsetof(ASTGGPUNode, pos_z) == 8, "ASTGGPUNode::pos_z offset != 8");
static_assert(offsetof(ASTGGPUNode, active_flags) == 12, "ASTGGPUNode::active_flags offset != 12");
static_assert(offsetof(ASTGGPUNode, normal_x) == 16, "ASTGGPUNode::normal_x offset != 16");
static_assert(offsetof(ASTGGPUNode, normal_y) == 20, "ASTGGPUNode::normal_y offset != 20");
static_assert(offsetof(ASTGGPUNode, normal_z) == 24, "ASTGGPUNode::normal_z offset != 24");
static_assert(offsetof(ASTGGPUNode, generation) == 28, "ASTGGPUNode::generation offset != 28");
static_assert(offsetof(ASTGGPUNode, albedo_r) == 32, "ASTGGPUNode::albedo_r offset != 32");
static_assert(offsetof(ASTGGPUNode, albedo_g) == 36, "ASTGGPUNode::albedo_g offset != 36");
static_assert(offsetof(ASTGGPUNode, albedo_b) == 40, "ASTGGPUNode::albedo_b offset != 40");
static_assert(offsetof(ASTGGPUNode, chunk_id) == 44, "ASTGGPUNode::chunk_id offset != 44");

// Static Assertions for ASTGGPUDAGEdge (32 bytes, 16-byte aligned)
static_assert(sizeof(ASTGGPUDAGEdge) == 32, "ASTGGPUDAGEdge size must be exactly 32 bytes");
static_assert(alignof(ASTGGPUDAGEdge) == 16, "ASTGGPUDAGEdge alignment must be 16 bytes");
static_assert(offsetof(ASTGGPUDAGEdge, source_node_id) == 0, "ASTGGPUDAGEdge::source_node_id offset != 0");
static_assert(offsetof(ASTGGPUDAGEdge, dest_node_id) == 4, "ASTGGPUDAGEdge::dest_node_id offset != 4");
static_assert(offsetof(ASTGGPUDAGEdge, generation) == 8, "ASTGGPUDAGEdge::generation offset != 8");
static_assert(offsetof(ASTGGPUDAGEdge, edge_state) == 12, "ASTGGPUDAGEdge::edge_state offset != 12");
static_assert(offsetof(ASTGGPUDAGEdge, source_light_id) == 16, "ASTGGPUDAGEdge::source_light_id offset != 16");
static_assert(offsetof(ASTGGPUDAGEdge, angular_cell_id) == 20, "ASTGGPUDAGEdge::angular_cell_id offset != 20");
static_assert(offsetof(ASTGGPUDAGEdge, destruction_chunk_id) == 24, "ASTGGPUDAGEdge::destruction_chunk_id offset != 24");
static_assert(offsetof(ASTGGPUDAGEdge, flags) == 28, "ASTGGPUDAGEdge::flags offset != 28");

// Static Assertions for ASTGEdgeVisibilityResult (12 bytes)
static_assert(sizeof(ASTGEdgeVisibilityResult) == 12, "ASTGEdgeVisibilityResult size must be exactly 12 bytes");
static_assert(alignof(ASTGEdgeVisibilityResult) == 4, "ASTGEdgeVisibilityResult alignment must be 4 bytes");
static_assert(offsetof(ASTGEdgeVisibilityResult, edge_id) == 0, "ASTGEdgeVisibilityResult::edge_id offset != 0");
static_assert(offsetof(ASTGEdgeVisibilityResult, visibility_state) == 4, "ASTGEdgeVisibilityResult::visibility_state offset != 4");
static_assert(offsetof(ASTGEdgeVisibilityResult, generation) == 8, "ASTGEdgeVisibilityResult::generation offset != 8");

// Static Assertions for ASTGGPUOccluderAABB (32 bytes, 16-byte aligned)
static_assert(sizeof(ASTGGPUOccluderAABB) == 32, "ASTGGPUOccluderAABB size must be exactly 32 bytes");
static_assert(alignof(ASTGGPUOccluderAABB) == 16, "ASTGGPUOccluderAABB alignment must be 16 bytes");
static_assert(offsetof(ASTGGPUOccluderAABB, min_x) == 0, "ASTGGPUOccluderAABB::min_x offset != 0");
static_assert(offsetof(ASTGGPUOccluderAABB, min_y) == 4, "ASTGGPUOccluderAABB::min_y offset != 4");
static_assert(offsetof(ASTGGPUOccluderAABB, min_z) == 8, "ASTGGPUOccluderAABB::min_z offset != 8");
static_assert(offsetof(ASTGGPUOccluderAABB, group_id) == 12, "ASTGGPUOccluderAABB::group_id offset != 12");
static_assert(offsetof(ASTGGPUOccluderAABB, max_x) == 16, "ASTGGPUOccluderAABB::max_x offset != 16");
static_assert(offsetof(ASTGGPUOccluderAABB, max_y) == 20, "ASTGGPUOccluderAABB::max_y offset != 20");
static_assert(offsetof(ASTGGPUOccluderAABB, max_z) == 24, "ASTGGPUOccluderAABB::max_z offset != 24");
static_assert(offsetof(ASTGGPUOccluderAABB, flags) == 28, "ASTGGPUOccluderAABB::flags offset != 28");

// Static Assertions for ASTGVisibilityCounters (32 bytes) and the extended
// spatial-discovery TransportConstants block (48 bytes).
static_assert(sizeof(ASTGVisibilityCounters) == 32, "ASTGVisibilityCounters size must be exactly 32 bytes");
static_assert(offsetof(ASTGVisibilityCounters, edges_considered) == 0, "ASTGVisibilityCounters::edges_considered offset != 0");
static_assert(offsetof(ASTGVisibilityCounters, generation_rejected) == 4, "ASTGVisibilityCounters::generation_rejected offset != 4");
static_assert(offsetof(ASTGVisibilityCounters, angular_rejected) == 8, "ASTGVisibilityCounters::angular_rejected offset != 8");
static_assert(offsetof(ASTGVisibilityCounters, broadphase_rejected) == 12, "ASTGVisibilityCounters::broadphase_rejected offset != 12");
static_assert(offsetof(ASTGVisibilityCounters, rayquery_candidates) == 16, "ASTGVisibilityCounters::rayquery_candidates offset != 16");
static_assert(offsetof(ASTGVisibilityCounters, rayquery_blocked) == 20, "ASTGVisibilityCounters::rayquery_blocked offset != 20");
static_assert(offsetof(ASTGVisibilityCounters, rayquery_visible) == 24, "ASTGVisibilityCounters::rayquery_visible offset != 24");
static_assert(offsetof(ASTGVisibilityCounters, changed_state_count) == 28, "ASTGVisibilityCounters::changed_state_count offset != 28");

static_assert(sizeof(TransportConstants) == 52, "TransportConstants size must be exactly 52 bytes");
static_assert(offsetof(TransportConstants, candidate_count) == 0, "TransportConstants::candidate_count offset != 0");
static_assert(offsetof(TransportConstants, total_nodes) == 4, "TransportConstants::total_nodes offset != 4");
static_assert(offsetof(TransportConstants, total_edges) == 8, "TransportConstants::total_edges offset != 8");
static_assert(offsetof(TransportConstants, current_scene_generation) == 12, "TransportConstants::current_scene_generation offset != 12");
static_assert(offsetof(TransportConstants, dynamic_occlusion_mode) == 16, "TransportConstants::dynamic_occlusion_mode offset != 16");
static_assert(offsetof(TransportConstants, occluder_count) == 20, "TransportConstants::occluder_count offset != 20");
static_assert(offsetof(TransportConstants, destroyed_chunk_mask) == 24, "TransportConstants::destroyed_chunk_mask offset != 24");
static_assert(offsetof(TransportConstants, flags) == 28, "TransportConstants::flags offset != 28");
static_assert(offsetof(TransportConstants, discovery_range_count) == 32, "TransportConstants::discovery_range_count offset != 32");
static_assert(offsetof(TransportConstants, discovery_object_id) == 36, "TransportConstants::discovery_object_id offset != 36");
static_assert(offsetof(TransportConstants, discovery_occluder_index) == 40, "TransportConstants::discovery_occluder_index offset != 40");
static_assert(offsetof(TransportConstants, discovery_stamp) == 44, "TransportConstants::discovery_stamp offset != 44");
static_assert(offsetof(TransportConstants, visibility_state_slot) == 48, "TransportConstants::visibility_state_slot offset != 48");
static_assert(sizeof(ASTGPersistentVisibilityState) == 8, "ASTGPersistentVisibilityState size must be exactly 8 bytes");
#endif
