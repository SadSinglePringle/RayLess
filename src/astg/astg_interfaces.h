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
