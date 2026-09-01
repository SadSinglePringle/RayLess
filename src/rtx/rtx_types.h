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

// ==============================================================================
// ASTG PARTS J/K: GPU-FIRST CONTINUOUS ANGULAR B0 & DYNAMIC RECEIVER HIERARCHY
// ==============================================================================

// 64-byte Source-Local Angular Coordinate Basis (16-byte aligned)
#ifdef __cplusplus
struct alignas(16) RTXSourceAngularFrame {
#else
typedef struct RTXSourceAngularFrame {
#endif
    float origin_x, origin_y, origin_z; // Offset 0..11: Light world position
    uint32_t light_id;                  // Offset 12..15: Emitting light ID
    float forward_x, forward_y, forward_z; // Offset 16..27: Documented source-local zero-angle dir
    uint32_t light_type;                // Offset 28..31: 0=Omni, 1=Spot, 2=Directional, 3=Area
    float right_x, right_y, right_z;    // Offset 32..43: Orthonormal basis vector
    float range;                        // Offset 44..47: Effective emission range
    float up_x, up_y, up_z;             // Offset 48..59: Orthonormal basis vector
    uint32_t generation;                // Offset 60..63: Monotonic frame generation
#ifdef __cplusplus
};
#else
} RTXSourceAngularFrame;
#endif

// 48-byte Continuous B0 Direction Record (16-byte aligned)
#ifdef __cplusplus
struct alignas(16) ASTGB0DirectionRecord {
#else
typedef struct ASTGB0DirectionRecord {
#endif
    float dir_local_x, dir_local_y, dir_local_z; // Offset 0..11: Normalized dir in light-local frame
    uint32_t source_light_id;                    // Offset 12..15: Owning source light ID
    float theta;                                 // Offset 16..19: Continuous elevation angle in [-pi/2, pi/2]
    float phi;                                   // Offset 20..23: Continuous azimuth angle in [-pi, pi]
    uint32_t transport_node_id;                  // Offset 24..27: Associated B0 transport node ID
    uint32_t flags;                              // Offset 28..31: Validity / classification flags
    float hit_dist;                              // Offset 32..35: Euclidean distance to static surface hit
    uint32_t generation;                         // Offset 36..39: Record generation
    uint32_t retained_receiver_id;               // Offset 40..43: Retained receiver or probe ID
    float solid_angle;                           // Offset 44..47: Differential solid angle steradians
#ifdef __cplusplus
};
#else
} ASTGB0DirectionRecord;
#endif

// 32-byte Continuous B0 Angular BVH / Cone Hierarchy Node (16-byte aligned)
#ifdef __cplusplus
struct alignas(16) ASTGB0AngularBVHNode {
#else
typedef struct ASTGB0AngularBVHNode {
#endif
    float cone_axis_x, cone_axis_y, cone_axis_z; // Offset 0..11: Bounding cone axis (light-local)
    float cos_half_angle;                        // Offset 12..15: cos(half_angle) of bounding cone
    uint32_t left_child;                         // Offset 16..19: Offset to left child node (or first record offset if leaf)
    uint32_t right_child;                        // Offset 20..23: Offset to right child node (or 0 if leaf)
    uint32_t record_count;                       // Offset 24..27: 0 = internal node, >0 = leaf record count
    uint32_t light_id;                           // Offset 28..31: Owning light ID
#ifdef __cplusplus
};
#else
} ASTGB0AngularBVHNode;
#endif

// 32-byte Conservative Continuous Angular Footprint (16-byte aligned)
#ifdef __cplusplus
struct alignas(16) ASTGB0AngularFootprint {
#else
typedef struct ASTGB0AngularFootprint {
#endif
    float cone_axis_x, cone_axis_y, cone_axis_z; // Offset 0..11: Bounding cone axis (light-local)
    float cos_half_angle;                        // Offset 12..15: cos(half_angle) (-1.0 = full sphere)
    float sin_half_angle;                        // Offset 16..19: sin(half_angle)
    float min_dist;                              // Offset 20..23: Min distance from light origin
    float max_dist;                              // Offset 24..27: Max distance from light origin
    uint32_t flags;                              // Offset 28..31: Bit 0: full coverage, Bit 1: seam cross, Bit 2: polar cross
#ifdef __cplusplus
};
#else
} ASTGB0AngularFootprint;
#endif

// 64-byte Dynamic Bone Bound (16-byte aligned)
#ifdef __cplusplus
struct alignas(16) ASTGBoneBoundGPU {
#else
typedef struct ASTGBoneBoundGPU {
#endif
    float local_min_x, local_min_y, local_min_z; // Offset 0..11: Bone-local AABB min
    uint32_t bone_id;                            // Offset 12..15: Bone ID within character
    float local_max_x, local_max_y, local_max_z; // Offset 16..27: Bone-local AABB max
    uint32_t group_id;                           // Offset 28..31: Owning Dynamic Group ID
    float world_min_x, world_min_y, world_min_z; // Offset 32..43: Current world-space AABB min
    uint32_t cluster_offset;                     // Offset 44..47: First receiver cluster offset
    float world_max_x, world_max_y, world_max_z; // Offset 48..59: Current world-space AABB max
    uint32_t cluster_count;                      // Offset 60..63: Number of child receiver clusters
#ifdef __cplusplus
};
#else
} ASTGBoneBoundGPU;
#endif

// 48-byte Dynamic Receiver Cluster (16-byte aligned)
#ifdef __cplusplus
struct alignas(16) ASTGReceiverClusterGPU {
#else
typedef struct ASTGReceiverClusterGPU {
#endif
    float world_center_x, world_center_y, world_center_z; // Offset 0..11: Cluster world center
    float radius;                                         // Offset 12..15: Cluster bounding radius
    float normal_axis_x, normal_axis_y, normal_axis_z;    // Offset 16..27: Representative normal cone axis
    float cos_normal_half_angle;                          // Offset 28..31: cos(half_angle) of cluster normal cone
    uint32_t probe_offset;                                // Offset 32..35: First surface probe offset
    uint32_t probe_count;                                 // Offset 36..39: Number of member surface probes
    uint32_t bone_id;                                     // Offset 40..43: Owning bone ID
    uint32_t generation;                                  // Offset 44..47: Cluster dirty generation
#ifdef __cplusplus
};
#else
} ASTGReceiverClusterGPU;
#endif

// 80-byte Dynamic Bone-Local Surface Probe (16-byte aligned)
#ifdef __cplusplus
struct alignas(16) ASTGDynamicSurfaceProbeGPU {
#else
typedef struct ASTGDynamicSurfaceProbeGPU {
#endif
    float local_pos_x, local_pos_y, local_pos_z; // Offset 0..11: True BONE-LOCAL position
    uint32_t bone_id;                            // Offset 12..15: Owning bone ID
    float local_norm_x, local_norm_y, local_norm_z; // Offset 16..27: True BONE-LOCAL normal
    uint32_t cluster_id;                         // Offset 28..31: Owning receiver cluster ID
    float world_pos_x, world_pos_y, world_pos_z; // Offset 32..43: Transformed world position
    uint32_t group_id;                           // Offset 44..47: Owning dynamic group ID
    float world_norm_x, world_norm_y, world_norm_z; // Offset 48..59: Transformed world normal
    uint32_t generation;                         // Offset 60..63: Probe mutation generation
    float irradiance_r, irradiance_g, irradiance_b; // Offset 64..75: Direct + Indirect irradiance
    uint32_t last_visibility_mask;               // Offset 76..79: Direct light visibility bitmask / cache tag
#ifdef __cplusplus
};
#else
} ASTGDynamicSurfaceProbeGPU;
#endif

// 8-byte Persistent B0 Blocker State per (light_id, b0_record_id)
struct ASTGB0PersistentState {
    uint32_t blocker_count;    // Number of dynamic occluders currently blocking this ray
    uint32_t last_generation;  // Generation stamp of last state modification
};
typedef struct ASTGB0PersistentState ASTGB0PersistentState;

// 16-byte B0 State Transition Record
struct ASTGB0TransitionRecord {
    uint32_t light_id;         // Light ID
    uint32_t b0_record_id;     // Index into g_b0_records
    uint32_t transport_node_id;// Associated transport node
    uint32_t new_visibility_state; // 0 = unblocked, 1 = blocked
    uint32_t new_blocker_count;// Resulting active blocker count
    uint32_t generation;       // Modification generation
    uint32_t pad[2];
};
typedef struct ASTGB0TransitionRecord ASTGB0TransitionRecord;

// 32-byte Per-Light B0 Range & BVH Descriptor
#ifdef __cplusplus
struct alignas(16) ASTGLightB0RangeGPU {
#else
typedef struct ASTGLightB0RangeGPU {
#endif
    uint32_t record_offset;
    uint32_t record_count;
    uint32_t bvh_offset;
    uint32_t bvh_node_count;
    float color_r;
    float color_g;
    float color_b;
    float intensity;
};
typedef struct ASTGLightB0RangeGPU ASTGLightB0RangeGPU;

// Persistent (Group, Light) Key for Stable Allocation
struct ASTGGroupLightKey {
    uint32_t group_id;
    uint32_t light_id;
#ifdef __cplusplus
    bool operator==(const ASTGGroupLightKey& o) const {
        return group_id == o.group_id && light_id == o.light_id;
    }
    bool operator<(const ASTGGroupLightKey& o) const {
        if (group_id != o.group_id) return group_id < o.group_id;
        return light_id < o.light_id;
    }
#endif
};
typedef struct ASTGGroupLightKey ASTGGroupLightKey;

// Stable Persistent Membership & Footprint Allocation Record
struct ASTGGroupLightMembershipAllocation {
    uint32_t group_id;
    uint32_t actual_light_id;
    uint32_t packed_light_index;

    uint32_t record_offset;
    uint32_t record_count;

    uint32_t membership_word_offset;
    uint32_t membership_word_count;

    uint32_t footprint_offset;
    uint32_t footprint_count;

    uint32_t light_layout_generation;
    uint32_t allocation_generation;
    uint64_t layout_hash;
};
typedef struct ASTGGroupLightMembershipAllocation ASTGGroupLightMembershipAllocation;

// 48-byte Changed Group/Light Pair Descriptor (16-byte aligned)
#ifdef __cplusplus
struct alignas(16) ASTGChangedGroupLightPairGPU {
#else
typedef struct ASTGChangedGroupLightPairGPU {
#endif
    uint32_t group_id;
    uint32_t actual_light_id;
    uint32_t packed_light_index;
    uint32_t first_bound;

    uint32_t bound_count;
    uint32_t record_offset;
    uint32_t record_count;
    uint32_t bvh_root_index;

    uint32_t generation;
    uint32_t membership_word_offset;
    uint32_t membership_word_count;
    uint32_t footprint_offset;
#ifdef __cplusplus
};
#else
} ASTGChangedGroupLightPairGPU;
#endif

// 16-byte Membership Word Work Item for Pass J4
#ifdef __cplusplus
struct alignas(16) ASTGMembershipWordWorkGPU {
#else
typedef struct ASTGMembershipWordWorkGPU {
#endif
    uint32_t pair_index;
    uint32_t local_word_index;
    uint32_t global_word_offset;
    uint32_t pad;
#ifdef __cplusplus
};
#else
} ASTGMembershipWordWorkGPU;
#endif

// 64-byte Bone Transform Matrix (16-byte aligned)
#ifdef __cplusplus
struct alignas(16) ASTGBoneTransformGPU {
#else
typedef struct ASTGBoneTransformGPU {
#endif
    float row0_x, row0_y, row0_z, row0_w;
    float row1_x, row1_y, row1_z, row1_w;
    float row2_x, row2_y, row2_z, row2_w;
    float row3_x, row3_y, row3_z, row3_w;
#ifdef __cplusplus
};
#else
} ASTGBoneTransformGPU;
#endif

// 16-byte Compact Probe-Light Work Item (Pass K4 -> K5)
#ifdef __cplusplus
struct alignas(16) ASTGProbeLightWorkGPU {
#else
typedef struct ASTGProbeLightWorkGPU {
#endif
    uint32_t probe_id;
    uint32_t actual_light_id;
    uint32_t packed_light_index;
    uint32_t dependency_generation;
#ifdef __cplusplus
};
#else
} ASTGProbeLightWorkGPU;
#endif

// 32-byte Compact Probe-Light Contribution (Pass K5 -> K6)
#ifdef __cplusplus
struct alignas(16) ASTGProbeLightContributionGPU {
#else
typedef struct ASTGProbeLightContributionGPU {
#endif
    uint32_t probe_id;
    uint32_t actual_light_id;
    uint32_t visibility;
    uint32_t dependency_generation;
    float irradiance_r;
    float irradiance_g;
    float irradiance_b;
    float pad;
#ifdef __cplusplus
};
#else
} ASTGProbeLightContributionGPU;
#endif

// 8-byte B0 Downstream Dependency Range
struct ASTGB0DependencyRangeGPU {
    uint32_t dependency_offset;
    uint32_t dependency_count;
};
typedef struct ASTGB0DependencyRangeGPU ASTGB0DependencyRangeGPU;

// 16-byte Group Bone Range Descriptor
struct ASTGGroupBoneRangeGPU {
    uint32_t group_id;
    uint32_t transform_offset;
    uint32_t transform_count;
    uint32_t generation;
};
typedef struct ASTGGroupBoneRangeGPU ASTGGroupBoneRangeGPU;

// Receiver Visibility Provenance Mode
enum ASTGReceiverVisibilityMode {
    ASTG_RECEIVER_VISIBILITY_AABB_PROXY = 0,
    ASTG_RECEIVER_VISIBILITY_DXR_DYNAMIC_GEOMETRY = 1
};
typedef enum ASTGReceiverVisibilityMode ASTGReceiverVisibilityMode;

// Execution Mode and Status Enums
enum ASTGPartsJKExecutionMode {
    ASTG_PARTS_JK_GPU_PRODUCTION = 0,
    ASTG_PARTS_JK_CPU_REFERENCE = 1
};
typedef enum ASTGPartsJKExecutionMode ASTGPartsJKExecutionMode;

enum ASTGPartsJKExecutionStatus {
    ASTG_PARTS_JK_GPU_OK = 0,
    ASTG_PARTS_JK_GPU_NOT_INITIALIZED = 1,
    ASTG_PARTS_JK_GPU_RESOURCE_OVERFLOW = 2,
    ASTG_PARTS_JK_GPU_SHADER_FAILURE = 3,
    ASTG_PARTS_JK_CPU_REFERENCE_ONLY = 4
};
typedef enum ASTGPartsJKExecutionStatus ASTGPartsJKExecutionStatus;

// Comprehensive Anti-Fallback & Execution Telemetry
struct ASTGPartsJKTelemetryGPU {
    uint32_t gpu_j1_dispatches;
    uint32_t gpu_j2_dispatches;
    uint32_t gpu_j3_exact_tests;
    uint32_t gpu_j4_membership_words;
    uint32_t gpu_j5_transitions;
    uint32_t gpu_k1_bones_tested;
    uint32_t gpu_k2_clusters_tested;
    uint32_t gpu_k3_probes_scheduled;
    uint32_t gpu_k4_visibility_rays;
    uint32_t gpu_k5_probe_light_accumulations;

    // Detailed GPU-originated telemetry counters
    uint32_t gpu_j1_threads_processed;
    uint32_t gpu_j1_footprints_written;
    uint32_t gpu_j2_pairs_processed;
    uint32_t gpu_j2_nodes_visited;
    uint32_t gpu_j2_stack_overflows;
    uint32_t gpu_j3_exact_blockers;
    uint32_t gpu_j4_additions;
    uint32_t gpu_j4_removals;
    uint32_t gpu_j4_underflow_errors;
    uint32_t gpu_j5_compacted_transitions;
    uint32_t gpu_k1_bones_transformed;
    uint32_t gpu_k2_probes_transformed;
    uint32_t gpu_k3_clusters_rebuilt;
    uint32_t gpu_k4_clusters_culled;
    uint32_t gpu_k4_work_emitted;
    uint32_t gpu_k4_work_overflow;
    uint32_t gpu_k5_work_consumed;
    uint32_t gpu_k5_visible_results;
    uint32_t gpu_k6_contributions_reduced;
    uint32_t gpu_k3_invalid_probe_indices;
    uint32_t gpu_j4_transition_overflow;
    uint32_t gpu_j5_compaction_overflow;

    uint32_t cpu_part_j_reference_calls;
    uint32_t cpu_part_k_reference_calls;

    // Real measured GPU timestamp query intervals (in ms, -1.0 if not measured)
    double gpu_j1_ms;
    double gpu_j2_ms;
    double gpu_j3_ms;
    double gpu_j2_j3_ms;
    double gpu_j4_ms;
    double gpu_j5_ms;
    double gpu_k1_ms;
    double gpu_k2_ms;
    double gpu_k3_ms;
    double gpu_k4_ms;
    double gpu_k5_ms;
    double gpu_k6_ms;
    double gpu_part_j_total_ms;
    double gpu_part_k_total_ms;
    double gpu_total_ms;

    // Measured CPU phase durations (in ms)
    double cpu_prep_ms;
    double cpu_upload_ms;
    double cpu_record_ms;
    double cpu_submission_ms;
    double cpu_sync_wait_ms;
    double cpu_readback_ms;
};
typedef struct ASTGPartsJKTelemetryGPU ASTGPartsJKTelemetryGPU;

struct ASTGD3D12DebugStatus {
    bool is_active;
    bool gpu_based_validation_active;
    uint64_t error_count;
    uint64_t warning_count;
    uint64_t corruption_count;
    uint64_t info_count;
};
typedef struct ASTGD3D12DebugStatus ASTGD3D12DebugStatus;

// Comprehensive GPU Telemetry for B0 Angular & Dynamic Receivers
struct ASTGB0AngularTelemetry {
    uint32_t bounds_updated;
    uint32_t bone_bounds_tested;
    uint32_t angular_hierarchy_nodes_visited;
    uint32_t angular_candidates;
    uint32_t exact_visibility_tests;
    uint32_t exact_blockers;
    uint32_t angular_false_positives;
    uint32_t visibility_transitions;
    uint32_t blocker_count_updates;
    uint32_t receiver_clusters_touched;
    uint32_t receiver_probes_touched;
    uint32_t receiver_probes_reused;
    double gpu_projection_us;
    double gpu_hierarchy_query_us;
    double gpu_exact_test_us;
    double gpu_compaction_update_us;
    double gpu_total_us;
    double cpu_submission_us;
    double cpu_wall_us;
    uint64_t readback_bytes;
};
typedef struct ASTGB0AngularTelemetry ASTGB0AngularTelemetry;

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

// Static Assertions for Parts J/K Data Structures
using ASTGSourceAngularFrameGPU = RTXSourceAngularFrame;
using ASTGB0AngularBVHNodeGPU = ASTGB0AngularBVHNode;
using ASTGB0AngularFootprintGPU = ASTGB0AngularFootprint;
using ASTGB0DirectionRecordGPU = ASTGB0DirectionRecord;

static_assert(sizeof(RTXSourceAngularFrame) == 64, "RTXSourceAngularFrame size must be exactly 64 bytes");
static_assert(alignof(RTXSourceAngularFrame) == 16, "RTXSourceAngularFrame alignment must be 16 bytes");
static_assert(offsetof(RTXSourceAngularFrame, origin_x) == 0, "RTXSourceAngularFrame::origin_x offset != 0");
static_assert(offsetof(RTXSourceAngularFrame, light_id) == 12, "RTXSourceAngularFrame::light_id offset != 12");
static_assert(offsetof(RTXSourceAngularFrame, forward_x) == 16, "RTXSourceAngularFrame::forward_x offset != 16");
static_assert(offsetof(RTXSourceAngularFrame, light_type) == 28, "RTXSourceAngularFrame::light_type offset != 28");
static_assert(offsetof(RTXSourceAngularFrame, right_x) == 32, "RTXSourceAngularFrame::right_x offset != 32");
static_assert(offsetof(RTXSourceAngularFrame, range) == 44, "RTXSourceAngularFrame::range offset != 44");
static_assert(offsetof(RTXSourceAngularFrame, up_x) == 48, "RTXSourceAngularFrame::up_x offset != 48");
static_assert(offsetof(RTXSourceAngularFrame, generation) == 60, "RTXSourceAngularFrame::generation offset != 60");

static_assert(sizeof(ASTGB0DirectionRecord) == 48, "ASTGB0DirectionRecord size must be exactly 48 bytes");
static_assert(alignof(ASTGB0DirectionRecord) == 16, "ASTGB0DirectionRecord alignment must be 16 bytes");
static_assert(offsetof(ASTGB0DirectionRecord, dir_local_x) == 0, "ASTGB0DirectionRecord::dir_local_x offset != 0");
static_assert(offsetof(ASTGB0DirectionRecord, source_light_id) == 12, "ASTGB0DirectionRecord::source_light_id offset != 12");
static_assert(offsetof(ASTGB0DirectionRecord, theta) == 16, "ASTGB0DirectionRecord::theta offset != 16");
static_assert(offsetof(ASTGB0DirectionRecord, phi) == 20, "ASTGB0DirectionRecord::phi offset != 20");
static_assert(offsetof(ASTGB0DirectionRecord, transport_node_id) == 24, "ASTGB0DirectionRecord::transport_node_id offset != 24");
static_assert(offsetof(ASTGB0DirectionRecord, flags) == 28, "ASTGB0DirectionRecord::flags offset != 28");
static_assert(offsetof(ASTGB0DirectionRecord, hit_dist) == 32, "ASTGB0DirectionRecord::hit_dist offset != 32");
static_assert(offsetof(ASTGB0DirectionRecord, generation) == 36, "ASTGB0DirectionRecord::generation offset != 36");
static_assert(offsetof(ASTGB0DirectionRecord, retained_receiver_id) == 40, "ASTGB0DirectionRecord::retained_receiver_id offset != 40");
static_assert(offsetof(ASTGB0DirectionRecord, solid_angle) == 44, "ASTGB0DirectionRecord::solid_angle offset != 44");

static_assert(sizeof(ASTGB0AngularBVHNode) == 32, "ASTGB0AngularBVHNode size must be exactly 32 bytes");
static_assert(alignof(ASTGB0AngularBVHNode) == 16, "ASTGB0AngularBVHNode alignment must be 16 bytes");
static_assert(offsetof(ASTGB0AngularBVHNode, cone_axis_x) == 0, "ASTGB0AngularBVHNode::cone_axis_x offset != 0");
static_assert(offsetof(ASTGB0AngularBVHNode, cos_half_angle) == 12, "ASTGB0AngularBVHNode::cos_half_angle offset != 12");
static_assert(offsetof(ASTGB0AngularBVHNode, left_child) == 16, "ASTGB0AngularBVHNode::left_child offset != 16");
static_assert(offsetof(ASTGB0AngularBVHNode, right_child) == 20, "ASTGB0AngularBVHNode::right_child offset != 20");
static_assert(offsetof(ASTGB0AngularBVHNode, record_count) == 24, "ASTGB0AngularBVHNode::record_count offset != 24");
static_assert(offsetof(ASTGB0AngularBVHNode, light_id) == 28, "ASTGB0AngularBVHNode::light_id offset != 28");

static_assert(sizeof(ASTGB0AngularFootprint) == 32, "ASTGB0AngularFootprint size must be exactly 32 bytes");
static_assert(alignof(ASTGB0AngularFootprint) == 16, "ASTGB0AngularFootprint alignment must be 16 bytes");
static_assert(offsetof(ASTGB0AngularFootprint, cone_axis_x) == 0, "ASTGB0AngularFootprint::cone_axis_x offset != 0");
static_assert(offsetof(ASTGB0AngularFootprint, cos_half_angle) == 12, "ASTGB0AngularFootprint::cos_half_angle offset != 12");
static_assert(offsetof(ASTGB0AngularFootprint, sin_half_angle) == 16, "ASTGB0AngularFootprint::sin_half_angle offset != 16");
static_assert(offsetof(ASTGB0AngularFootprint, min_dist) == 20, "ASTGB0AngularFootprint::min_dist offset != 20");
static_assert(offsetof(ASTGB0AngularFootprint, max_dist) == 24, "ASTGB0AngularFootprint::max_dist offset != 24");
static_assert(offsetof(ASTGB0AngularFootprint, flags) == 28, "ASTGB0AngularFootprint::flags offset != 28");

static_assert(sizeof(ASTGBoneBoundGPU) == 64, "ASTGBoneBoundGPU size must be exactly 64 bytes");
static_assert(alignof(ASTGBoneBoundGPU) == 16, "ASTGBoneBoundGPU alignment must be 16 bytes");
static_assert(offsetof(ASTGBoneBoundGPU, local_min_x) == 0, "ASTGBoneBoundGPU::local_min_x offset != 0");
static_assert(offsetof(ASTGBoneBoundGPU, bone_id) == 12, "ASTGBoneBoundGPU::bone_id offset != 12");
static_assert(offsetof(ASTGBoneBoundGPU, local_max_x) == 16, "ASTGBoneBoundGPU::local_max_x offset != 16");
static_assert(offsetof(ASTGBoneBoundGPU, group_id) == 28, "ASTGBoneBoundGPU::group_id offset != 28");
static_assert(offsetof(ASTGBoneBoundGPU, world_min_x) == 32, "ASTGBoneBoundGPU::world_min_x offset != 32");
static_assert(offsetof(ASTGBoneBoundGPU, cluster_offset) == 44, "ASTGBoneBoundGPU::cluster_offset offset != 44");
static_assert(offsetof(ASTGBoneBoundGPU, world_max_x) == 48, "ASTGBoneBoundGPU::world_max_x offset != 48");
static_assert(offsetof(ASTGBoneBoundGPU, cluster_count) == 60, "ASTGBoneBoundGPU::cluster_count offset != 60");

static_assert(sizeof(ASTGReceiverClusterGPU) == 48, "ASTGReceiverClusterGPU size must be exactly 48 bytes");
static_assert(alignof(ASTGReceiverClusterGPU) == 16, "ASTGReceiverClusterGPU alignment must be 16 bytes");
static_assert(offsetof(ASTGReceiverClusterGPU, world_center_x) == 0, "ASTGReceiverClusterGPU::world_center_x offset != 0");
static_assert(offsetof(ASTGReceiverClusterGPU, radius) == 12, "ASTGReceiverClusterGPU::radius offset != 12");
static_assert(offsetof(ASTGReceiverClusterGPU, normal_axis_x) == 16, "ASTGReceiverClusterGPU::normal_axis_x offset != 16");
static_assert(offsetof(ASTGReceiverClusterGPU, cos_normal_half_angle) == 28, "ASTGReceiverClusterGPU::cos_normal_half_angle offset != 28");
static_assert(offsetof(ASTGReceiverClusterGPU, probe_offset) == 32, "ASTGReceiverClusterGPU::probe_offset offset != 32");
static_assert(offsetof(ASTGReceiverClusterGPU, probe_count) == 36, "ASTGReceiverClusterGPU::probe_count offset != 36");
static_assert(offsetof(ASTGReceiverClusterGPU, bone_id) == 40, "ASTGReceiverClusterGPU::bone_id offset != 40");
static_assert(offsetof(ASTGReceiverClusterGPU, generation) == 44, "ASTGReceiverClusterGPU::generation offset != 44");

static_assert(sizeof(ASTGDynamicSurfaceProbeGPU) == 80, "ASTGDynamicSurfaceProbeGPU size must be exactly 80 bytes");
static_assert(alignof(ASTGDynamicSurfaceProbeGPU) == 16, "ASTGDynamicSurfaceProbeGPU alignment must be 16 bytes");
static_assert(offsetof(ASTGDynamicSurfaceProbeGPU, local_pos_x) == 0, "ASTGDynamicSurfaceProbeGPU::local_pos_x offset != 0");
static_assert(offsetof(ASTGDynamicSurfaceProbeGPU, bone_id) == 12, "ASTGDynamicSurfaceProbeGPU::bone_id offset != 12");
static_assert(offsetof(ASTGDynamicSurfaceProbeGPU, local_norm_x) == 16, "ASTGDynamicSurfaceProbeGPU::local_norm_x offset != 16");
static_assert(offsetof(ASTGDynamicSurfaceProbeGPU, cluster_id) == 28, "ASTGDynamicSurfaceProbeGPU::cluster_id offset != 28");
static_assert(offsetof(ASTGDynamicSurfaceProbeGPU, world_pos_x) == 32, "ASTGDynamicSurfaceProbeGPU::world_pos_x offset != 32");
static_assert(offsetof(ASTGDynamicSurfaceProbeGPU, group_id) == 44, "ASTGDynamicSurfaceProbeGPU::group_id offset != 44");
static_assert(offsetof(ASTGDynamicSurfaceProbeGPU, world_norm_x) == 48, "ASTGDynamicSurfaceProbeGPU::world_norm_x offset != 48");
static_assert(offsetof(ASTGDynamicSurfaceProbeGPU, generation) == 60, "ASTGDynamicSurfaceProbeGPU::generation offset != 60");
static_assert(offsetof(ASTGDynamicSurfaceProbeGPU, irradiance_r) == 64, "ASTGDynamicSurfaceProbeGPU::irradiance_r offset != 64");
static_assert(offsetof(ASTGDynamicSurfaceProbeGPU, last_visibility_mask) == 76, "ASTGDynamicSurfaceProbeGPU::last_visibility_mask offset != 76");

static_assert(sizeof(ASTGB0PersistentState) == 8, "ASTGB0PersistentState size must be exactly 8 bytes");
static_assert(offsetof(ASTGB0PersistentState, blocker_count) == 0, "ASTGB0PersistentState::blocker_count offset != 0");
static_assert(offsetof(ASTGB0PersistentState, last_generation) == 4, "ASTGB0PersistentState::last_generation offset != 4");

static_assert(sizeof(ASTGB0TransitionRecord) == 32, "ASTGB0TransitionRecord size must be exactly 32 bytes");
static_assert(offsetof(ASTGB0TransitionRecord, light_id) == 0, "ASTGB0TransitionRecord::light_id offset != 0");
static_assert(offsetof(ASTGB0TransitionRecord, b0_record_id) == 4, "ASTGB0TransitionRecord::b0_record_id offset != 4");
static_assert(offsetof(ASTGB0TransitionRecord, transport_node_id) == 8, "ASTGB0TransitionRecord::transport_node_id offset != 8");
static_assert(offsetof(ASTGB0TransitionRecord, new_visibility_state) == 12, "ASTGB0TransitionRecord::new_visibility_state offset != 12");
static_assert(offsetof(ASTGB0TransitionRecord, new_blocker_count) == 16, "ASTGB0TransitionRecord::new_blocker_count offset != 16");
static_assert(offsetof(ASTGB0TransitionRecord, generation) == 20, "ASTGB0TransitionRecord::generation offset != 20");

static_assert(sizeof(ASTGLightB0RangeGPU) == 32, "ASTGLightB0RangeGPU size must be exactly 32 bytes");
static_assert(alignof(ASTGLightB0RangeGPU) == 16, "ASTGLightB0RangeGPU alignment must be 16 bytes");
static_assert(offsetof(ASTGLightB0RangeGPU, record_offset) == 0, "ASTGLightB0RangeGPU::record_offset offset != 0");
static_assert(offsetof(ASTGLightB0RangeGPU, record_count) == 4, "ASTGLightB0RangeGPU::record_count offset != 4");
static_assert(offsetof(ASTGLightB0RangeGPU, bvh_offset) == 8, "ASTGLightB0RangeGPU::bvh_offset offset != 8");
static_assert(offsetof(ASTGLightB0RangeGPU, bvh_node_count) == 12, "ASTGLightB0RangeGPU::bvh_node_count offset != 12");
static_assert(offsetof(ASTGLightB0RangeGPU, color_r) == 16, "ASTGLightB0RangeGPU::color_r offset != 16");
static_assert(offsetof(ASTGLightB0RangeGPU, color_g) == 20, "ASTGLightB0RangeGPU::color_g offset != 20");
static_assert(offsetof(ASTGLightB0RangeGPU, color_b) == 24, "ASTGLightB0RangeGPU::color_b offset != 24");
static_assert(offsetof(ASTGLightB0RangeGPU, intensity) == 28, "ASTGLightB0RangeGPU::intensity offset != 28");

static_assert(sizeof(ASTGChangedGroupLightPairGPU) == 48, "ASTGChangedGroupLightPairGPU size must be exactly 48 bytes");
static_assert(alignof(ASTGChangedGroupLightPairGPU) == 16, "ASTGChangedGroupLightPairGPU alignment must be 16 bytes");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, group_id) == 0, "ASTGChangedGroupLightPairGPU::group_id offset != 0");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, actual_light_id) == 4, "ASTGChangedGroupLightPairGPU::actual_light_id offset != 4");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, packed_light_index) == 8, "ASTGChangedGroupLightPairGPU::packed_light_index offset != 8");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, first_bound) == 12, "ASTGChangedGroupLightPairGPU::first_bound offset != 12");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, bound_count) == 16, "ASTGChangedGroupLightPairGPU::bound_count offset != 16");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, record_offset) == 20, "ASTGChangedGroupLightPairGPU::record_offset offset != 20");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, record_count) == 24, "ASTGChangedGroupLightPairGPU::record_count offset != 24");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, bvh_root_index) == 28, "ASTGChangedGroupLightPairGPU::bvh_root_index offset != 28");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, generation) == 32, "ASTGChangedGroupLightPairGPU::generation offset != 32");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, membership_word_offset) == 36, "ASTGChangedGroupLightPairGPU::membership_word_offset offset != 36");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, membership_word_count) == 40, "ASTGChangedGroupLightPairGPU::membership_word_count offset != 40");
static_assert(offsetof(ASTGChangedGroupLightPairGPU, footprint_offset) == 44, "ASTGChangedGroupLightPairGPU::footprint_offset offset != 44");

static_assert(sizeof(ASTGMembershipWordWorkGPU) == 16, "ASTGMembershipWordWorkGPU size must be exactly 16 bytes");
static_assert(alignof(ASTGMembershipWordWorkGPU) == 16, "ASTGMembershipWordWorkGPU alignment must be 16 bytes");
static_assert(offsetof(ASTGMembershipWordWorkGPU, pair_index) == 0, "ASTGMembershipWordWorkGPU::pair_index offset != 0");
static_assert(offsetof(ASTGMembershipWordWorkGPU, local_word_index) == 4, "ASTGMembershipWordWorkGPU::local_word_index offset != 4");
static_assert(offsetof(ASTGMembershipWordWorkGPU, global_word_offset) == 8, "ASTGMembershipWordWorkGPU::global_word_offset offset != 8");

static_assert(sizeof(ASTGBoneTransformGPU) == 64, "ASTGBoneTransformGPU size must be exactly 64 bytes");
static_assert(alignof(ASTGBoneTransformGPU) == 16, "ASTGBoneTransformGPU alignment must be 16 bytes");
static_assert(offsetof(ASTGBoneTransformGPU, row0_x) == 0, "ASTGBoneTransformGPU::row0_x offset != 0");
static_assert(offsetof(ASTGBoneTransformGPU, row1_x) == 16, "ASTGBoneTransformGPU::row1_x offset != 16");
static_assert(offsetof(ASTGBoneTransformGPU, row2_x) == 32, "ASTGBoneTransformGPU::row2_x offset != 32");
static_assert(offsetof(ASTGBoneTransformGPU, row3_x) == 48, "ASTGBoneTransformGPU::row3_x offset != 48");

static_assert(sizeof(ASTGProbeLightWorkGPU) == 16, "ASTGProbeLightWorkGPU size must be exactly 16 bytes");
static_assert(offsetof(ASTGProbeLightWorkGPU, probe_id) == 0, "ASTGProbeLightWorkGPU::probe_id offset != 0");
static_assert(offsetof(ASTGProbeLightWorkGPU, actual_light_id) == 4, "ASTGProbeLightWorkGPU::actual_light_id offset != 4");
static_assert(offsetof(ASTGProbeLightWorkGPU, packed_light_index) == 8, "ASTGProbeLightWorkGPU::packed_light_index offset != 8");
static_assert(offsetof(ASTGProbeLightWorkGPU, dependency_generation) == 12, "ASTGProbeLightWorkGPU::dependency_generation offset != 12");

static_assert(sizeof(ASTGProbeLightContributionGPU) == 32, "ASTGProbeLightContributionGPU size must be exactly 32 bytes");
static_assert(alignof(ASTGProbeLightContributionGPU) == 16, "ASTGProbeLightContributionGPU alignment must be 16 bytes");
static_assert(offsetof(ASTGProbeLightContributionGPU, probe_id) == 0, "ASTGProbeLightContributionGPU::probe_id offset != 0");
static_assert(offsetof(ASTGProbeLightContributionGPU, actual_light_id) == 4, "ASTGProbeLightContributionGPU::actual_light_id offset != 4");
static_assert(offsetof(ASTGProbeLightContributionGPU, visibility) == 8, "ASTGProbeLightContributionGPU::visibility offset != 8");
static_assert(offsetof(ASTGProbeLightContributionGPU, dependency_generation) == 12, "ASTGProbeLightContributionGPU::dependency_generation offset != 12");
static_assert(offsetof(ASTGProbeLightContributionGPU, irradiance_r) == 16, "ASTGProbeLightContributionGPU::irradiance_r offset != 16");
static_assert(offsetof(ASTGProbeLightContributionGPU, irradiance_g) == 20, "ASTGProbeLightContributionGPU::irradiance_g offset != 20");
static_assert(offsetof(ASTGProbeLightContributionGPU, irradiance_b) == 24, "ASTGProbeLightContributionGPU::irradiance_b offset != 24");

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
