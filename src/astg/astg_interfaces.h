#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==============================================================================
// ASTG ↔ ENGINE TRANSPORT INTERFACES (C-ABI)
// Formal contract between Godot (Frame Owner) and RayLess / ASTG (GI Subsystem)
// ==============================================================================

// Geometry Classifications
typedef enum ASTGGeometryClass {
    ASTG_GEOM_STATIC = 0,        // Persistent AS & Transport (Buildings, Terrain, Furniture)
    ASTG_GEOM_DESTRUCTIBLE = 1,  // Chunked AS & Dynamic Invalidation (Destructible Walls)
    ASTG_GEOM_DYNAMIC = 2,       // Excluded from persistent graph initially (Characters)
    ASTG_GEOM_EXCLUDED = 3       // Visual only / Godot rasterized (UI, Particles, Decals)
} ASTGGeometryClass;

// Light Classifications
typedef enum ASTGLightClass {
    ASTG_LIGHT_STATIONARY = 0,   // ASTG Persistent Transport + Godot Visible Direct Light
    ASTG_LIGHT_DYNAMIC = 1,      // Godot Direct Light only (Moving Flashlight / Torches)
    ASTG_LIGHT_EXCLUDED = 2      // Visual helper / Editor only
} ASTGLightClass;

// Dynamic Occlusion Transport Edge States (Handoff Item 2)
typedef enum ASTGTransportEdgeState {
    ASTG_EDGE_ACTIVE = 0,             // Structurally valid and not dynamically occluded
    ASTG_EDGE_INVALID_STATIC = 1,     // Geometry changed/destroyed permanently
    ASTG_EDGE_OCCLUDED_DYNAMIC = 2    // Temporarily occluded by moving object bounding box
} ASTGTransportEdgeState;

// Dynamic Occlusion Modes (Phase 5 / Handoff Item 1, 7, 10, 11, 12)
typedef enum ASTGDynamicOcclusionMode {
    ASTG_OCCLUSION_NONE = 0,                    // Diagnostic baseline (no dynamic occlusion)
    ASTG_OCCLUSION_DAG_EDGES_ALL_BOUNCES = 1,   // Mode A: General DAG-edge testing across B0..BN
    ASTG_OCCLUSION_ANGULAR_B0_DAG_B1_PLUS = 2,  // Mode B: Angular B0 footprint + DAG-edge testing for B1+
    ASTG_OCCLUSION_ANGULAR_B0_ONLY = 3          // Mode C: Angular B0 footprint only, B1+ untouched
} ASTGDynamicOcclusionMode;

// Angular Cell Dynamic Delta State (Handoff Item 19, 20)
typedef enum ASTGAngularCellDeltaState {
    ASTG_ANGULAR_CELL_STILL_COVERED = 0,
    ASTG_ANGULAR_CELL_NEWLY_COVERED = 1,
    ASTG_ANGULAR_CELL_NEWLY_UNCOVERED = 2
} ASTGAngularCellDeltaState;

// Dynamic Occlusion Precision Policy (Handoff Item 27)
typedef enum ASTGDynamicOcclusionPrecision {
    ASTG_OCCLUSION_BOUNDS_ONLY = 0,   // Pure bounding-box / slab intersection (zero DXR rays)
    ASTG_OCCLUSION_BOUNDS_THEN_DXR = 1 // Initial broadphase bounds followed by DXR refinement
} ASTGDynamicOcclusionPrecision;

// Authoritative Geometry & Primitive Metadata Mapping
struct ASTGPrimitiveMetadata {
    uint32_t mesh_id;
    uint32_t instance_id;
    uint32_t surface_cluster_id;
    uint32_t destruction_chunk_id;
    uint32_t transport_material_id;
};

// Simplified Diffuse Transport Material (Extracted from complex Godot PBR materials)
struct ASTGTransportMaterial {
    float diffuse_albedo_r;
    float diffuse_albedo_g;
    float diffuse_albedo_b;
    float emissive_strength;
    uint32_t flags; // Bit 0: Two-sided, Bit 1: Emissive Emitter, Bit 2: Opaque
};

// Immutable Spatial Parameters for ASTG Stationary Lights (32 bytes)
struct ASTGLightStatic {
    float pos_x, pos_y, pos_z;
    float range;
    float dir_x, dir_y, dir_z;
    uint32_t type; // 0=Omni, 1=Spot, 2=Directional, 3=Area
};

// Dynamic Energy State for ASTG Stationary Lights (32 bytes)
struct ASTGLightDynamic {
    float color_r;
    float color_g;
    float color_b;
    float intensity;
    uint32_t enabled;     // 1=Active, 0=Off
    uint32_t generation;  // Monotonically increments on RGB/intensity modifications
    uint32_t flags;
    uint32_t pad;
};

// Authoritative Camera View State (Used for Probe Relevance & Reconstruction, not Topology)
struct ASTGCameraState {
    float view_matrix[16];
    float proj_matrix[16];
    float pos_x, pos_y, pos_z;
    float fov_radians;
};

// Diagnostics & Synchronization Generations
struct ASTGSceneSyncState {
    uint32_t godot_scene_generation;
    uint32_t astg_geometry_generation;
    uint32_t is_synchronized; // 1 if godot_scene_gen == astg_geom_gen
};

#ifdef __cplusplus
}
#endif
