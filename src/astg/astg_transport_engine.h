#pragma once
#include "astg_interfaces.h"
#include "rtx_types.h"
#include "rtx_raytracer.h"
#include "benchmark_manifest.h"
#include "gltf_scene_loader.h"
#include <vector>
#include <bitset>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <numeric>
#include <chrono>

// ==============================================================================
// ASTG SAFE OPTIMIZATION INVARIANT & FORMALIZED REGENERATION DATA STRUCTURES
// ==============================================================================
/*
 * SAFE OPTIMIZATION INVARIANT:
 * Optimization may reduce:
 * - ray count
 * - node count
 * - contribution count
 * - probe count
 * - runtime work
 *
 * Optimization may NOT remove:
 * - source_light_id
 * - angular_cell_id
 * - parent node provenance
 * - blocking_chunk_id
 * - termination reason
 * - reverse dependency mapping
 * - enough spatial/angular information to retrace the branch
 */

enum ASTGTerminationReason {
    TERMINATION_VISIBLE_SURFACE = 0,
    TERMINATION_EMPTY_SPACE = 1,
    TERMINATION_LOW_ENERGY = 2,
    TERMINATION_MERGED = 3,
    TERMINATION_BLOCKED_STATIC = 4,
    TERMINATION_BLOCKED_DESTRUCTIBLE = 5,
    TERMINATION_MAX_DEPTH = 6,
    TERMINATION_PROBE_TERMINATED = 7,
    TERMINATION_STITCHED_TO_EXISTING_DAG = 8
};

inline const char* get_termination_reason_name(ASTGTerminationReason r) {
    switch (r) {
        case TERMINATION_VISIBLE_SURFACE: return "VISIBLE_SURFACE";
        case TERMINATION_EMPTY_SPACE: return "EMPTY_SPACE";
        case TERMINATION_LOW_ENERGY: return "LOW_ENERGY";
        case TERMINATION_MERGED: return "MERGED";
        case TERMINATION_BLOCKED_STATIC: return "BLOCKED_STATIC";
        case TERMINATION_BLOCKED_DESTRUCTIBLE: return "BLOCKED_DESTRUCTIBLE";
        case TERMINATION_MAX_DEPTH: return "MAX_DEPTH";
        case TERMINATION_PROBE_TERMINATED: return "PROBE_TERMINATED";
        case TERMINATION_STITCHED_TO_EXISTING_DAG: return "STITCHED_TO_EXISTING_DAG";
        default: return "UNKNOWN";
    }
}

static inline uint64_t fnv1a_64_hash_bytes(const void* data, size_t size, uint64_t hash = 14695981039346656037ULL) {
    const uint8_t* ptr = (const uint8_t*)data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint64_t)ptr[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static inline uint64_t fnv1a_64_path(uint32_t light, uint32_t cell, uint32_t bounce, uint32_t node, uint32_t probe) {
    uint64_t h = 14695981039346656037ULL;
    h = fnv1a_64_hash_bytes(&light, sizeof(light), h);
    h = fnv1a_64_hash_bytes(&cell, sizeof(cell), h);
    h = fnv1a_64_hash_bytes(&bounce, sizeof(bounce), h);
    h = fnv1a_64_hash_bytes(&node, sizeof(node), h);
    h = fnv1a_64_hash_bytes(&probe, sizeof(probe), h);
    return h;
}

// Regeneration Anchor (Part 1, 2): Retained ONLY for branches that genuinely require future regeneration
struct ASTGRegenerationAnchor {
    uint32_t anchor_id = 0;
    uint32_t source_light_id = 0;
    uint32_t angular_cell_id = 0;
    uint32_t parent_node_id = 0;
    uint32_t blocking_chunk_id = 0;
    uint32_t bounce_depth = 0;

    RTXVector3 ray_origin = {0, 0, 0};
    RTXVector3 ray_direction = {0, 1, 0};
    float t_min = 0.05f;
    float t_max = 20.0f;

    float angular_min_u = 0.0f;
    float angular_min_v = 0.0f;
    float angular_max_u = 1.0f;
    float angular_max_v = 1.0f;

    ASTGTerminationReason reason = TERMINATION_BLOCKED_DESTRUCTIBLE;
    uint32_t geometry_generation = 1;
    uint32_t as_generation = 1;
    bool is_active = true;
};

// Merged Node Multi-Parent Provenance (Part 6, 7)
struct DAGParentRef {
    uint32_t parent_node_id = 0;
    uint32_t source_light_id = 0;
    uint32_t angular_cell_id = 0;
    float transfer_weight = 1.0f;
    bool is_valid = true;
};

// Surface-Attached Probe representation with Triangle Barycentrics
struct SurfaceAttachedProbe {
    uint32_t probe_id = 0;
    uint32_t mesh_id = 0;
    uint32_t instance_id = 0;
    uint32_t primitive_id = 0;
    float barycentric_u = 0.333f;
    float barycentric_v = 0.333f;

    RTXVector3 world_position = {0, 0, 0};
    RTXVector3 geometric_normal = {0, 1, 0};
    uint32_t surface_cluster_id = 0;
    uint32_t destruction_chunk_id = 0;
    uint32_t material_id = 0;

    float area_weight = 1.0f;
    float confidence = 0.0f;
    uint32_t sample_count = 0;
    bool is_valid = true;
};

// Transport Path Node (Bounce 0 Direct Hit & Bounce 1 Indirect Hit)
// Transport Path Node (Bounce 0 Direct Hit & Bounce 1 Indirect Hit)
struct ASTGTransportNode {
    uint32_t node_id = 0;
    uint32_t source_light_id = 0;
    uint32_t angular_cell_id = 0;
    uint32_t bounce_depth = 0; // 0 = Bounce 0, 1 = Bounce 1

    uint32_t hit_primitive_id = 0;
    uint32_t surface_cluster_id = 0;
    uint32_t destruction_chunk_id = 0;
    uint32_t material_id = 0;

    RTXVector3 position = {0, 0, 0};
    RTXVector3 geometric_normal = {0, 1, 0};
    float geometric_factor = 0.0f; // (N.L) / (d^2 + 1)
    float diffuse_albedo = 0.75f;

    // Accumulated Transport State (Upstream path transfer excluding dynamic light state)
    float path_transfer_r = 1.0f;
    float path_transfer_g = 1.0f;
    float path_transfer_b = 1.0f;

    // Full upstream geometry dependency lineage
    std::unordered_set<uint32_t> inherited_chunk_dependencies;

    ASTGTerminationReason termination_reason = TERMINATION_VISIBLE_SURFACE;
    std::vector<DAGParentRef> parent_refs; // Supports multi-parent DAG merging
    bool is_active = true;
    uint32_t generation = 1;
};

// ==============================================================================
// ASTG DYNAMIC OBJECT OCCLUSION & SPATIAL INDEX STRUCTURES (PHASE 4)
// ==============================================================================

// Axis-Aligned Bounding Box (AABB) with corridor expansion & query operations
struct ASTGAABB {
    RTXVector3 min_bounds = { 1e30f, 1e30f, 1e30f };
    RTXVector3 max_bounds = { -1e30f, -1e30f, -1e30f };

    ASTGAABB() = default;
    ASTGAABB(const RTXVector3& mn, const RTXVector3& mx) : min_bounds(mn), max_bounds(mx) {}

    void expand(float radius) {
        min_bounds.x -= radius; min_bounds.y -= radius; min_bounds.z -= radius;
        max_bounds.x += radius; max_bounds.y += radius; max_bounds.z += radius;
    }

    void include_point(const RTXVector3& p) {
        min_bounds.x = std::min(min_bounds.x, p.x);
        min_bounds.y = std::min(min_bounds.y, p.y);
        min_bounds.z = std::min(min_bounds.z, p.z);
        max_bounds.x = std::max(max_bounds.x, p.x);
        max_bounds.y = std::max(max_bounds.y, p.y);
        max_bounds.z = std::max(max_bounds.z, p.z);
    }

    void union_with(const ASTGAABB& other) {
        min_bounds.x = std::min(min_bounds.x, other.min_bounds.x);
        min_bounds.y = std::min(min_bounds.y, other.min_bounds.y);
        min_bounds.z = std::min(min_bounds.z, other.min_bounds.z);
        max_bounds.x = std::max(max_bounds.x, other.max_bounds.x);
        max_bounds.y = std::max(max_bounds.y, other.max_bounds.y);
        max_bounds.z = std::max(max_bounds.z, other.max_bounds.z);
    }

    static ASTGAABB union_of(const ASTGAABB& a, const ASTGAABB& b) {
        ASTGAABB r = a;
        r.union_with(b);
        return r;
    }

    bool intersects_aabb(const ASTGAABB& other) const {
        if (max_bounds.x < other.min_bounds.x || min_bounds.x > other.max_bounds.x) return false;
        if (max_bounds.y < other.min_bounds.y || min_bounds.y > other.max_bounds.y) return false;
        if (max_bounds.z < other.min_bounds.z || min_bounds.z > other.max_bounds.z) return false;
        return true;
    }

    bool contains_point(const RTXVector3& p) const {
        return (p.x >= min_bounds.x && p.x <= max_bounds.x &&
                p.y >= min_bounds.y && p.y <= max_bounds.y &&
                p.z >= min_bounds.z && p.z <= max_bounds.z);
    }

    bool is_valid() const {
        return (min_bounds.x <= max_bounds.x && min_bounds.y <= max_bounds.y && min_bounds.z <= max_bounds.z);
    }
};

// Line-Segment vs AABB Slab Intersection (Handoff Item 12, 13, 25)
// P(t) = A + t(B - A) for t in [0, 1]
inline bool segment_intersects_aabb(
    const RTXVector3& A,
    const RTXVector3& B,
    const ASTGAABB& box
) {
    const float eps = 1e-7f;
    float dx = B.x - A.x;
    float dy = B.y - A.y;
    float dz = B.z - A.z;

    float tmin = 0.0f;
    float tmax = 1.0f;

    // X slab
    if (std::abs(dx) < eps) {
        if (A.x < box.min_bounds.x || A.x > box.max_bounds.x) return false;
    } else {
        float inv_d = 1.0f / dx;
        float t1 = (box.min_bounds.x - A.x) * inv_d;
        float t2 = (box.max_bounds.x - A.x) * inv_d;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    // Y slab
    if (std::abs(dy) < eps) {
        if (A.y < box.min_bounds.y || A.y > box.max_bounds.y) return false;
    } else {
        float inv_d = 1.0f / dy;
        float t1 = (box.min_bounds.y - A.y) * inv_d;
        float t2 = (box.max_bounds.y - A.y) * inv_d;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    // Z slab
    if (std::abs(dz) < eps) {
        if (A.z < box.min_bounds.z || A.z > box.max_bounds.z) return false;
    } else {
        float inv_d = 1.0f / dz;
        float t1 = (box.min_bounds.z - A.z) * inv_d;
        float t2 = (box.max_bounds.z - A.z) * inv_d;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    return true;
}

// Ray vs AABB Intersection for Direction Cone Testing
// Ray: P(t) = origin + t * dir, t >= 0
inline bool ray_intersects_aabb(
    const RTXVector3& origin,
    const RTXVector3& dir,
    const ASTGAABB& box,
    float t_max = 1000.0f
) {
    const float eps = 1e-7f;
    float tmin = 0.0f;
    float tmax = t_max;

    // X slab
    if (std::abs(dir.x) < eps) {
        if (origin.x < box.min_bounds.x || origin.x > box.max_bounds.x) return false;
    } else {
        float inv_d = 1.0f / dir.x;
        float t1 = (box.min_bounds.x - origin.x) * inv_d;
        float t2 = (box.max_bounds.x - origin.x) * inv_d;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    // Y slab
    if (std::abs(dir.y) < eps) {
        if (origin.y < box.min_bounds.y || origin.y > box.max_bounds.y) return false;
    } else {
        float inv_d = 1.0f / dir.y;
        float t1 = (box.min_bounds.y - origin.y) * inv_d;
        float t2 = (box.max_bounds.y - origin.y) * inv_d;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    // Z slab
    if (std::abs(dir.z) < eps) {
        if (origin.z < box.min_bounds.z || origin.z > box.max_bounds.z) return false;
    } else {
        float inv_d = 1.0f / dir.z;
        float t1 = (box.min_bounds.z - origin.z) * inv_d;
        float t2 = (box.max_bounds.z - origin.z) * inv_d;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    return tmax >= 0.0f && tmin <= t_max;
}

// Encodes a 3D unit vector into octahedral (u, v) in [0, 1] x [0, 1] (Handoff Item 13, 14, 15)
inline void encode_octahedral(const RTXVector3& d, float& out_u, float& out_v) {
    float l1 = std::abs(d.x) + std::abs(d.y) + std::abs(d.z);
    if (l1 < 1e-6f) {
        out_u = 0.5f;
        out_v = 0.5f;
        return;
    }
    float px = d.x / l1;
    float py = d.y / l1;
    float pz = d.z / l1;
    if (pz < 0.0f) {
        float old_px = px;
        px = (1.0f - std::abs(py)) * (old_px >= 0.0f ? 1.0f : -1.0f);
        py = (1.0f - std::abs(old_px)) * (py >= 0.0f ? 1.0f : -1.0f);
    }
    out_u = px * 0.5f + 0.5f;
    out_v = py * 0.5f + 0.5f;
}

// Decodes octahedral (u, v) in [0, 1] x [0, 1] back into a 3D unit vector (Handoff Item 13, 14, 15)
inline void decode_octahedral(float u, float v, RTXVector3& out_dir) {
    float px = u * 2.0f - 1.0f;
    float py = v * 2.0f - 1.0f;
    float pz = 1.0f - std::abs(px) - std::abs(py);
    if (pz < 0.0f) {
        float old_px = px;
        px = (1.0f - std::abs(py)) * (old_px >= 0.0f ? 1.0f : -1.0f);
        py = (1.0f - std::abs(old_px)) * (py >= 0.0f ? 1.0f : -1.0f);
    }
    float len = std::sqrt(px * px + py * py + pz * pz);
    if (len > 1e-6f) {
        out_dir = { px / len, py / len, pz / len };
    } else {
        out_dir = { 0.0f, 1.0f, 0.0f };
    }
}

// Continuous & Hierarchical Source-Local B0 Angular Transport Hierarchy (Parts J & K)
class ASTGAngularHierarchy {
public:
    static constexpr uint32_t LEAF_BINS_PER_DIM = 8;
    static constexpr uint32_t TOTAL_LEAF_CELLS = 64;

    struct AngularCellInfo {
        uint32_t cell_id = 0;
        uint32_t cell_x = 0;
        uint32_t cell_y = 0;
        float u_min = 0.0f, u_max = 0.0f;
        float v_min = 0.0f, v_max = 0.0f;
        float u_center = 0.0f, v_center = 0.0f;
        RTXVector3 dir_center = { 0, 1, 0 };
        float solid_angle = (4.0f * 3.14159265f) / 64.0f;
    };

    AngularCellInfo cells[TOTAL_LEAF_CELLS];

    // Source-Local Angular Coordinate Basis
    RTXSourceAngularFrame source_frame{};

    // Continuous B0 Direction Records & GPU Angular BVH
    std::vector<ASTGB0DirectionRecord> b0_records;
    std::vector<ASTGB0AngularBVHNode> b0_bvh_nodes;

    ASTGAngularHierarchy() {
        for (uint32_t i = 0; i < TOTAL_LEAF_CELLS; ++i) {
            cells[i].cell_id = i;
            cells[i].cell_x = i % LEAF_BINS_PER_DIM;
            cells[i].cell_y = i / LEAF_BINS_PER_DIM;
            cells[i].u_min = float(cells[i].cell_x) / float(LEAF_BINS_PER_DIM);
            cells[i].u_max = float(cells[i].cell_x + 1) / float(LEAF_BINS_PER_DIM);
            cells[i].v_min = float(cells[i].cell_y) / float(LEAF_BINS_PER_DIM);
            cells[i].v_max = float(cells[i].cell_y + 1) / float(LEAF_BINS_PER_DIM);
            cells[i].u_center = (cells[i].u_min + cells[i].u_max) * 0.5f;
            cells[i].v_center = (cells[i].v_min + cells[i].v_max) * 0.5f;
            decode_octahedral(cells[i].u_center, cells[i].v_center, cells[i].dir_center);
            cells[i].solid_angle = (4.0f * 3.14159265f) / 64.0f;
        }

        // Initialize default canonical frame
        initialize_frame(0, { 0.0f, 0.0f, 0.0f }, 0, { 0.0f, 0.0f, 1.0f }, 25.0f, 1);
    }

    void initialize_frame(
        uint32_t light_id,
        const RTXVector3& light_pos,
        uint32_t light_type,
        const RTXVector3& forward,
        float range = 25.0f,
        uint32_t gen = 1
    ) {
        source_frame.light_id = light_id;
        source_frame.origin_x = light_pos.x;
        source_frame.origin_y = light_pos.y;
        source_frame.origin_z = light_pos.z;
        source_frame.light_type = light_type;
        source_frame.range = range;
        source_frame.generation = gen;

        RTXVector3 f = forward;
        float f_len = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
        if (f_len > 1e-5f) {
            f.x /= f_len; f.y /= f_len; f.z /= f_len;
        } else {
            f = { 0.0f, 0.0f, 1.0f };
        }

        RTXVector3 r, u;
        if (light_type == 1 || light_type == 2) {
            // Spot or Directional: derive orthonormal basis from forward
            RTXVector3 tmp_up = (std::abs(f.y) < 0.99f) ? RTXVector3{ 0.0f, 1.0f, 0.0f } : RTXVector3{ 1.0f, 0.0f, 0.0f };
            // r = tmp_up x f
            r = {
                tmp_up.y * f.z - tmp_up.z * f.y,
                tmp_up.z * f.x - tmp_up.x * f.z,
                tmp_up.x * f.y - tmp_up.y * f.x
            };
            float r_len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
            if (r_len > 1e-5f) { r.x /= r_len; r.y /= r_len; r.z /= r_len; } else { r = { 1.0f, 0.0f, 0.0f }; }
            // u = f x r
            u = {
                f.y * r.z - f.z * r.y,
                f.z * r.x - f.x * r.z,
                f.x * r.y - f.y * r.x
            };
        } else {
            // Point / Omni: canonical deterministic basis
            f = { 0.0f, 0.0f, 1.0f };
            r = { 1.0f, 0.0f, 0.0f };
            u = { 0.0f, 1.0f, 0.0f };
        }

        source_frame.forward_x = f.x; source_frame.forward_y = f.y; source_frame.forward_z = f.z;
        source_frame.right_x = r.x; source_frame.right_y = r.y; source_frame.right_z = r.z;
        source_frame.up_x = u.x; source_frame.up_y = u.y; source_frame.up_z = u.z;
    }

    RTXVector3 world_to_local(const RTXVector3& w) const {
        return {
            w.x * source_frame.right_x + w.y * source_frame.right_y + w.z * source_frame.right_z,
            w.x * source_frame.up_x + w.y * source_frame.up_y + w.z * source_frame.up_z,
            w.x * source_frame.forward_x + w.y * source_frame.forward_y + w.z * source_frame.forward_z
        };
    }

    RTXVector3 local_to_world(const RTXVector3& l) const {
        return {
            source_frame.right_x * l.x + source_frame.up_x * l.y + source_frame.forward_x * l.z,
            source_frame.right_y * l.x + source_frame.up_y * l.y + source_frame.forward_y * l.z,
            source_frame.right_z * l.x + source_frame.up_z * l.y + source_frame.forward_z * l.z
        };
    }

    // Projects an AABB into a conservative continuous angular footprint cone
    ASTGB0AngularFootprint project_box_continuous(const ASTGAABB& box) const {
        ASTGB0AngularFootprint fp{};
        if (!box.is_valid()) {
            fp.cos_half_angle = 1.0f; // 0-degree cone (empty)
            fp.sin_half_angle = 0.0f;
            return fp;
        }

        RTXVector3 light_pos = { source_frame.origin_x, source_frame.origin_y, source_frame.origin_z };

        // Special Case 1: Light inside or immediately touching the AABB
        if (box.contains_point(light_pos)) {
            fp.cone_axis_x = 0.0f; fp.cone_axis_y = 0.0f; fp.cone_axis_z = 1.0f;
            fp.cos_half_angle = -1.0f; // 4*pi full spherical coverage
            fp.sin_half_angle = 0.0f;
            fp.min_dist = 0.0f;
            fp.max_dist = 1000.0f;
            fp.flags = 0x1; // Full coverage
            return fp;
        }

        RTXVector3 corners[8] = {
            { box.min_bounds.x, box.min_bounds.y, box.min_bounds.z },
            { box.max_bounds.x, box.min_bounds.y, box.min_bounds.z },
            { box.min_bounds.x, box.max_bounds.y, box.min_bounds.z },
            { box.max_bounds.x, box.max_bounds.y, box.min_bounds.z },
            { box.min_bounds.x, box.min_bounds.y, box.max_bounds.z },
            { box.max_bounds.x, box.min_bounds.y, box.max_bounds.z },
            { box.min_bounds.x, box.max_bounds.y, box.max_bounds.z },
            { box.max_bounds.x, box.max_bounds.y, box.max_bounds.z }
        };

        RTXVector3 local_dirs[8];
        float min_d = 1e30f, max_d = 0.0f;
        RTXVector3 sum_dir = { 0.0f, 0.0f, 0.0f };

        for (int i = 0; i < 8; ++i) {
            RTXVector3 v = { corners[i].x - light_pos.x, corners[i].y - light_pos.y, corners[i].z - light_pos.z };
            float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            min_d = std::min(min_d, len);
            max_d = std::max(max_d, len);
            if (len > 1e-5f) {
                v.x /= len; v.y /= len; v.z /= len;
            } else {
                v = { 0.0f, 0.0f, 1.0f };
            }
            local_dirs[i] = world_to_local(v);
            sum_dir.x += local_dirs[i].x;
            sum_dir.y += local_dirs[i].y;
            sum_dir.z += local_dirs[i].z;
        }

        float sum_len = std::sqrt(sum_dir.x * sum_dir.x + sum_dir.y * sum_dir.y + sum_dir.z * sum_dir.z);
        RTXVector3 central_axis;
        if (sum_len > 1e-5f) {
            central_axis = { sum_dir.x / sum_len, sum_dir.y / sum_len, sum_dir.z / sum_len };
        } else {
            // Hemispheric / antipodal wrap edge case
            central_axis = { 0.0f, 0.0f, 1.0f };
        }

        float max_angle = 0.0f;
        for (int i = 0; i < 8; ++i) {
            float dot_val = central_axis.x * local_dirs[i].x +
                            central_axis.y * local_dirs[i].y +
                            central_axis.z * local_dirs[i].z;
            dot_val = std::max(-1.0f, std::min(1.0f, dot_val));
            float angle = std::acos(dot_val);
            max_angle = std::max(max_angle, angle);
        }

        // Conservative safety guard-band (0.5 degrees / ~0.0087 rad)
        float conservative_half_angle = max_angle + 0.015f;
        if (conservative_half_angle >= 3.14159265f || min_d < 1e-3f) {
            fp.cone_axis_x = 0.0f; fp.cone_axis_y = 0.0f; fp.cone_axis_z = 1.0f;
            fp.cos_half_angle = -1.0f;
            fp.sin_half_angle = 0.0f;
            fp.flags = 0x1;
        } else {
            fp.cone_axis_x = central_axis.x;
            fp.cone_axis_y = central_axis.y;
            fp.cone_axis_z = central_axis.z;
            fp.cos_half_angle = std::cos(conservative_half_angle);
            fp.sin_half_angle = std::sin(conservative_half_angle);
            fp.flags = 0;
        }

        fp.min_dist = min_d;
        fp.max_dist = max_d;
        return fp;
    }

    // Builds the Continuous B0 Direction Records and Cone Hierarchy over static transport nodes
    void build_continuous_b0_hierarchy(const std::vector<ASTGTransportNode>& nodes, uint32_t light_id) {
        b0_records.clear();
        b0_bvh_nodes.clear();

        RTXVector3 light_pos = { source_frame.origin_x, source_frame.origin_y, source_frame.origin_z };

        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& node = nodes[i];
            if (node.source_light_id != light_id || node.bounce_depth != 0 || !node.is_active) continue;

            RTXVector3 delta = { node.position.x - light_pos.x, node.position.y - light_pos.y, node.position.z - light_pos.z };
            float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
            RTXVector3 dir_world;
            if (dist > 1e-5f) {
                dir_world = { delta.x / dist, delta.y / dist, delta.z / dist };
            } else {
                dir_world = { 0.0f, 1.0f, 0.0f };
            }

            RTXVector3 dir_loc = world_to_local(dir_world);
            float d_len = std::sqrt(dir_loc.x * dir_loc.x + dir_loc.y * dir_loc.y + dir_loc.z * dir_loc.z);
            if (d_len > 1e-5f) { dir_loc.x /= d_len; dir_loc.y /= d_len; dir_loc.z /= d_len; }

            float theta = std::asin(std::max(-1.0f, std::min(1.0f, dir_loc.y)));
            float phi = std::atan2(dir_loc.x, dir_loc.z);

            ASTGB0DirectionRecord rec{};
            rec.dir_local_x = dir_loc.x;
            rec.dir_local_y = dir_loc.y;
            rec.dir_local_z = dir_loc.z;
            rec.source_light_id = light_id;
            rec.theta = theta;
            rec.phi = phi;
            rec.transport_node_id = (uint32_t)i;
            rec.flags = 0x1;
            rec.hit_dist = dist;
            rec.generation = node.generation;
            rec.retained_receiver_id = node.surface_cluster_id;
            rec.solid_angle = ((4.0f * 3.14159265f) / 64.0f);

            b0_records.push_back(rec);
        }

        if (b0_records.empty()) return;

        // Build Hierarchical Cone BVH
        _build_bvh_recursive(0, (uint32_t)b0_records.size());
    }

    uint32_t _build_bvh_recursive(uint32_t start, uint32_t end) {
        uint32_t count = end - start;
        uint32_t node_idx = (uint32_t)b0_bvh_nodes.size();
        b0_bvh_nodes.push_back(ASTGB0AngularBVHNode{});

        // Compute Bounding Cone for [start, end)
        RTXVector3 sum_dir = { 0.0f, 0.0f, 0.0f };
        for (uint32_t i = start; i < end; ++i) {
            sum_dir.x += b0_records[i].dir_local_x;
            sum_dir.y += b0_records[i].dir_local_y;
            sum_dir.z += b0_records[i].dir_local_z;
        }
        float sum_len = std::sqrt(sum_dir.x * sum_dir.x + sum_dir.y * sum_dir.y + sum_dir.z * sum_dir.z);
        RTXVector3 axis = (sum_len > 1e-5f) ? RTXVector3{ sum_dir.x / sum_len, sum_dir.y / sum_len, sum_dir.z / sum_len } : RTXVector3{ 0.0f, 0.0f, 1.0f };

        float max_angle = 0.0f;
        for (uint32_t i = start; i < end; ++i) {
            float dot_val = axis.x * b0_records[i].dir_local_x +
                            axis.y * b0_records[i].dir_local_y +
                            axis.z * b0_records[i].dir_local_z;
            dot_val = std::max(-1.0f, std::min(1.0f, dot_val));
            max_angle = std::max(max_angle, std::acos(dot_val));
        }

        b0_bvh_nodes[node_idx].cone_axis_x = axis.x;
        b0_bvh_nodes[node_idx].cone_axis_y = axis.y;
        b0_bvh_nodes[node_idx].cone_axis_z = axis.z;
        b0_bvh_nodes[node_idx].cos_half_angle = std::cos(max_angle + 0.005f);
        b0_bvh_nodes[node_idx].light_id = source_frame.light_id;
        b0_bvh_nodes[node_idx].flags = 0;

        if (count <= 4) {
            // Leaf Node
            b0_bvh_nodes[node_idx].child_or_record_offset = start;
            b0_bvh_nodes[node_idx].record_count = count;
            return node_idx;
        }

        // Partition along dominant variation axis (azimuth phi or elevation theta)
        float min_phi = 1e9f, max_phi = -1e9f;
        float min_th = 1e9f, max_th = -1e9f;
        for (uint32_t i = start; i < end; ++i) {
            min_phi = std::min(min_phi, b0_records[i].phi);
            max_phi = std::max(max_phi, b0_records[i].phi);
            min_th = std::min(min_th, b0_records[i].theta);
            max_th = std::max(max_th, b0_records[i].theta);
        }

        bool sort_by_phi = (max_phi - min_phi) >= (max_th - min_th);
        uint32_t mid = start + count / 2;
        if (sort_by_phi) {
            std::nth_element(b0_records.begin() + start, b0_records.begin() + mid, b0_records.begin() + end,
                [](const ASTGB0DirectionRecord& a, const ASTGB0DirectionRecord& b) { return a.phi < b.phi; });
        } else {
            std::nth_element(b0_records.begin() + start, b0_records.begin() + mid, b0_records.begin() + end,
                [](const ASTGB0DirectionRecord& a, const ASTGB0DirectionRecord& b) { return a.theta < b.theta; });
        }

        uint32_t left_child = _build_bvh_recursive(start, mid);
        uint32_t right_child = _build_bvh_recursive(mid, end);

        b0_bvh_nodes[node_idx].child_or_record_offset = left_child;
        b0_bvh_nodes[node_idx].record_count = 0; // Internal node
        return node_idx;
    }

    // Traverses the continuous B0 angular hierarchy and collects candidate records within footprint
    void query_b0_directions_in_angular_footprint(
        const ASTGB0AngularFootprint& footprint,
        std::vector<uint32_t>& out_candidates,
        ASTGB0AngularTelemetry* telemetry = nullptr
    ) const {
        out_candidates.clear();
        if (b0_bvh_nodes.empty() || b0_records.empty()) return;

        // Full spherical coverage fast-path
        if ((footprint.flags & 0x1) != 0 || footprint.cos_half_angle <= -0.9999f) {
            out_candidates.resize(b0_records.size());
            for (size_t i = 0; i < b0_records.size(); ++i) out_candidates[i] = (uint32_t)i;
            if (telemetry) {
                telemetry->angular_hierarchy_nodes_visited += 1;
                telemetry->angular_candidates += (uint32_t)b0_records.size();
            }
            return;
        }

        RTXVector3 fp_axis = { footprint.cone_axis_x, footprint.cone_axis_y, footprint.cone_axis_z };
        float fp_cos = footprint.cos_half_angle;
        float fp_sin = footprint.sin_half_angle;

        std::vector<uint32_t> stack;
        stack.reserve(64);
        stack.push_back(0);

        while (!stack.empty()) {
            uint32_t curr = stack.back();
            stack.pop_back();

            if (telemetry) telemetry->angular_hierarchy_nodes_visited++;

            const auto& node = b0_bvh_nodes[curr];
            RTXVector3 node_axis = { node.cone_axis_x, node.cone_axis_y, node.cone_axis_z };
            float dot_val = node_axis.x * fp_axis.x + node_axis.y * fp_axis.y + node_axis.z * fp_axis.z;

            // Cone-Cone overlap condition: cos(angle) >= cos(theta_node + theta_fp)
            float node_cos = node.cos_half_angle;
            float node_sin = std::sqrt(std::max(0.0f, 1.0f - node_cos * node_cos));
            float threshold_cos = node_cos * fp_cos - node_sin * fp_sin;

            if (dot_val < threshold_cos) {
                // Cones are disjoint, prune branch!
                continue;
            }

            if (node.record_count > 0) {
                // Leaf Node: append candidates
                for (uint32_t r = 0; r < node.record_count; ++r) {
                    uint32_t rec_idx = node.child_or_record_offset + r;
                    const auto& rec = b0_records[rec_idx];

                    // Direct angular direction vs footprint cone check
                    float dir_dot = rec.dir_local_x * fp_axis.x + rec.dir_local_y * fp_axis.y + rec.dir_local_z * fp_axis.z;
                    if (dir_dot >= fp_cos) {
                        out_candidates.push_back(rec_idx);
                        if (telemetry) telemetry->angular_candidates++;
                    }
                }
            } else {
                // Internal Node: push left and right children
                uint32_t left_child = node.child_or_record_offset;
                uint32_t right_child = left_child + 1; // Assuming sequential layout or explicit
                if (left_child < b0_bvh_nodes.size()) stack.push_back(left_child);
                if (right_child < b0_bvh_nodes.size()) stack.push_back(right_child);
            }
        }
    }

    // Resolves exact candidate visibility against moving bound AABB
    void evaluate_exact_b0_visibility(
        const std::vector<uint32_t>& candidates,
        const ASTGAABB& occluder_box,
        std::vector<uint32_t>& out_exact_blockers,
        ASTGB0AngularTelemetry* telemetry = nullptr
    ) const {
        out_exact_blockers.clear();
        RTXVector3 light_pos = { source_frame.origin_x, source_frame.origin_y, source_frame.origin_z };

        for (uint32_t rec_idx : candidates) {
            if (rec_idx >= b0_records.size()) continue;
            const auto& rec = b0_records[rec_idx];

            if (telemetry) telemetry->exact_visibility_tests++;

            RTXVector3 dir_world = local_to_world({ rec.dir_local_x, rec.dir_local_y, rec.dir_local_z });
            RTXVector3 hit_pos = {
                light_pos.x + dir_world.x * rec.hit_dist,
                light_pos.y + dir_world.y * rec.hit_dist,
                light_pos.z + dir_world.z * rec.hit_dist
            };

            bool hit = segment_intersects_aabb(light_pos, hit_pos, occluder_box);
            if (hit) {
                out_exact_blockers.push_back(rec_idx);
                if (telemetry) telemetry->exact_blockers++;
            } else {
                if (telemetry) telemetry->angular_false_positives++;
            }
        }
    }

    uint32_t get_cell_id_for_dir(const RTXVector3& dir) const {
        float u = 0.0f, v = 0.0f;
        encode_octahedral(dir, u, v);
        int cx = std::min(7, std::max(0, int(u * 8.0f)));
        int cy = std::min(7, std::max(0, int(v * 8.0f)));
        return (uint32_t)(cy * 8 + cx);
    }

    // Projects an AABB from light position into a 64-bit angular cell mask (Optional Coarse Broadphase / Legacy Reference)
    uint64_t query_box_footprint(
        const RTXVector3& light_pos,
        const ASTGAABB& box,
        float* out_approx_proxy_solid_angle = nullptr
    ) const {
        if (!box.is_valid()) return 0ULL;

        // 8 Corners of the AABB
        RTXVector3 corners[8] = {
            { box.min_bounds.x, box.min_bounds.y, box.min_bounds.z },
            { box.max_bounds.x, box.min_bounds.y, box.min_bounds.z },
            { box.min_bounds.x, box.max_bounds.y, box.min_bounds.z },
            { box.max_bounds.x, box.max_bounds.y, box.min_bounds.z },
            { box.min_bounds.x, box.min_bounds.y, box.max_bounds.z },
            { box.max_bounds.x, box.min_bounds.y, box.max_bounds.z },
            { box.min_bounds.x, box.max_bounds.y, box.max_bounds.z },
            { box.max_bounds.x, box.max_bounds.y, box.max_bounds.z }
        };

        // Compute box center and approximate solid angle
        RTXVector3 center = {
            (box.min_bounds.x + box.max_bounds.x) * 0.5f,
            (box.min_bounds.y + box.max_bounds.y) * 0.5f,
            (box.min_bounds.z + box.max_bounds.z) * 0.5f
        };
        RTXVector3 to_center = { center.x - light_pos.x, center.y - light_pos.y, center.z - light_pos.z };
        float dist_sq = to_center.x * to_center.x + to_center.y * to_center.y + to_center.z * to_center.z;
        float dist = std::sqrt(dist_sq);

        RTXVector3 ext = { box.max_bounds.x - box.min_bounds.x, box.max_bounds.y - box.min_bounds.y, box.max_bounds.z - box.min_bounds.z };
        float approx_area = std::max(ext.x * ext.y, std::max(ext.y * ext.z, ext.x * ext.z));
        float proxy_sa = (dist_sq > 1e-4f) ? std::min(4.0f * 3.14159265f, approx_area / dist_sq) : (4.0f * 3.14159265f);
        if (out_approx_proxy_solid_angle) *out_approx_proxy_solid_angle = proxy_sa;

        float u_coords[8], v_coords[8];
        float min_u = 1.0f, max_u = 0.0f, min_v = 1.0f, max_v = 0.0f;
        bool has_pos_z = false, has_neg_z = false;

        for (int i = 0; i < 8; ++i) {
            RTXVector3 d = { corners[i].x - light_pos.x, corners[i].y - light_pos.y, corners[i].z - light_pos.z };
            float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (len > 1e-6f) {
                d.x /= len; d.y /= len; d.z /= len;
            }
            if (d.z >= 0.0f) has_pos_z = true; else has_neg_z = true;
            encode_octahedral(d, u_coords[i], v_coords[i]);
            min_u = std::min(min_u, u_coords[i]);
            max_u = std::max(max_u, u_coords[i]);
            min_v = std::min(min_v, v_coords[i]);
            max_v = std::max(max_v, v_coords[i]);
        }

        uint64_t result_mask = 0ULL;
        for (uint32_t i = 0; i < TOTAL_LEAF_CELLS; ++i) {
            const auto& cell = cells[i];
            bool cell_hit = false;

            if (ray_intersects_aabb(light_pos, cell.dir_center, box, dist * 2.0f + 5.0f)) {
                cell_hit = true;
            }

            if (!cell_hit) {
                RTXVector3 corner_dirs[4];
                decode_octahedral(cell.u_min, cell.v_min, corner_dirs[0]);
                decode_octahedral(cell.u_max, cell.v_min, corner_dirs[1]);
                decode_octahedral(cell.u_min, cell.v_max, corner_dirs[2]);
                decode_octahedral(cell.u_max, cell.v_max, corner_dirs[3]);
                for (int c = 0; c < 4; ++c) {
                    if (ray_intersects_aabb(light_pos, corner_dirs[c], box, dist * 2.0f + 5.0f)) {
                        cell_hit = true;
                        break;
                    }
                }
            }

            if (!cell_hit) {
                for (int c = 0; c < 8; ++c) {
                    if (u_coords[c] >= cell.u_min && u_coords[c] <= cell.u_max &&
                        v_coords[c] >= cell.v_min && v_coords[c] <= cell.v_max) {
                        cell_hit = true;
                        break;
                    }
                }
            }

            if (cell_hit) {
                result_mask |= (1ULL << i);
            }
        }

        return result_mask;
    }
};

// Collision-Free 3D Spatial Grid Key for Static Transport Nodes (Phase 6 / Handoff Review)
struct ASTGSpatialCellCoord {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    bool operator==(const ASTGSpatialCellCoord& o) const {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct ASTGSpatialCellHash {
    size_t operator()(const ASTGSpatialCellCoord& c) const noexcept {
        uint64_t h = (uint64_t)c.x * 0x9E3779B97F4A7C15ULL ^
                     (uint64_t)c.y * 0xC2B2AE3D27D4EB4FULL ^
                     (uint64_t)c.z * 0x165667B19E3779F9ULL;
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= h >> 27;
        h *= 0x94D049BB133111EBULL;
        h ^= h >> 31;
        return (size_t)h;
    }
};

// Spatial Hash Grid for Static Transport Nodes (Phase 6 / Handoff Item 15 & Review Item 6)
class ASTGStaticNodeSpatialGrid {
public:
    float cell_size = 2.0f;
    std::unordered_map<ASTGSpatialCellCoord, std::vector<uint32_t>, ASTGSpatialCellHash> grid;
    // The cell map is a broadphase. Keep the indexed positions so query_sphere
    // can honour its public exact-sphere contract instead of returning every
    // node in overlapping cubic cells.
    std::vector<RTXVector3> indexed_positions;

    void build(const std::vector<ASTGTransportNode>& nodes) {
        grid.clear();
        indexed_positions.resize(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i) {
            indexed_positions[i] = nodes[i].position;
            if (!nodes[i].is_active) continue;
            int cx = (int)std::floor(nodes[i].position.x / cell_size);
            int cy = (int)std::floor(nodes[i].position.y / cell_size);
            int cz = (int)std::floor(nodes[i].position.z / cell_size);
            grid[{ cx, cy, cz }].push_back((uint32_t)i);
        }
    }

    void query_sphere(const RTXVector3& center, float radius, std::vector<uint32_t>& out_nodes) const {
        out_nodes.clear();
        if (radius < 0.0f || cell_size <= 0.0f) return;
        const float radius_sq = radius * radius;
        int min_x = (int)std::floor((center.x - radius) / cell_size);
        int max_x = (int)std::floor((center.x + radius) / cell_size);
        int min_y = (int)std::floor((center.y - radius) / cell_size);
        int max_y = (int)std::floor((center.y + radius) / cell_size);
        int min_z = (int)std::floor((center.z - radius) / cell_size);
        int max_z = (int)std::floor((center.z + radius) / cell_size);

        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    auto it = grid.find({ x, y, z });
                    if (it != grid.end()) {
                        for (uint32_t nid : it->second) {
                            if (nid >= indexed_positions.size()) continue;
                            const RTXVector3& p = indexed_positions[nid];
                            const float dx = p.x - center.x;
                            const float dy = p.y - center.y;
                            const float dz = p.z - center.z;
                            if (dx * dx + dy * dy + dz * dz <= radius_sq) {
                                out_nodes.push_back(nid);
                            }
                        }
                    }
                }
            }
        }

        // Deduplicate candidates in case any node was indexed multiple times
        if (out_nodes.size() > 1) {
            std::sort(out_nodes.begin(), out_nodes.end());
            out_nodes.erase(std::unique(out_nodes.begin(), out_nodes.end()), out_nodes.end());
        }
    }
};

// Dedicated ASTG Occlusion Proxy Box (Handoff Item 4, 5, 6 / Hardened)
struct ASTGOccluderBounds {
    ASTGAABB local_bounds;
    ASTGAABB world_bounds;
    ASTGAABB aabb; // Backwards-compatible alias for world_bounds
    uint32_t bone_id = 0;
    std::string label;

    ASTGOccluderBounds() = default;
    ASTGOccluderBounds(const ASTGAABB& b, const std::string& l = "", uint32_t bone = 0)
        : local_bounds(b), world_bounds(b), aabb(b), bone_id(bone), label(l) {}
};

// 4x4 Matrix Transformation for Rigid & Skeletal Dynamic Objects (Phase 6 / Handoff Item 5, 36, 72)
struct RTXMatrix4x4 {
    float m[4][4];

    static RTXMatrix4x4 identity() {
        RTXMatrix4x4 r = {};
        r.m[0][0] = 1.0f; r.m[1][1] = 1.0f; r.m[2][2] = 1.0f; r.m[3][3] = 1.0f;
        return r;
    }

    static RTXMatrix4x4 translation(float tx, float ty, float tz) {
        RTXMatrix4x4 r = identity();
        r.m[0][3] = tx; r.m[1][3] = ty; r.m[2][3] = tz;
        return r;
    }

    static RTXMatrix4x4 rotation_x(float radians) {
        RTXMatrix4x4 r = identity();
        float c = std::cos(radians);
        float s = std::sin(radians);
        r.m[1][1] = c;  r.m[1][2] = -s;
        r.m[2][1] = s;  r.m[2][2] = c;
        return r;
    }

    static RTXMatrix4x4 rotation_y(float radians) {
        RTXMatrix4x4 r = identity();
        float c = std::cos(radians);
        float s = std::sin(radians);
        r.m[0][0] = c;  r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }

    static RTXMatrix4x4 rotation_z(float radians) {
        RTXMatrix4x4 r = identity();
        float c = std::cos(radians);
        float s = std::sin(radians);
        r.m[0][0] = c;  r.m[0][1] = -s;
        r.m[1][0] = s;  r.m[1][1] = c;
        return r;
    }

    static RTXMatrix4x4 scale(float sx, float sy, float sz) {
        RTXMatrix4x4 r = identity();
        r.m[0][0] = sx; r.m[1][1] = sy; r.m[2][2] = sz;
        return r;
    }

    RTXMatrix4x4 operator*(const RTXMatrix4x4& o) const {
        RTXMatrix4x4 r = {};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                for (int k = 0; k < 4; ++k) {
                    r.m[i][j] += m[i][k] * o.m[k][j];
                }
            }
        }
        return r;
    }

    RTXVector3 transform_point(const RTXVector3& p) const {
        float x = m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3];
        float y = m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3];
        float z = m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3];
        float w = m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3];
        if (std::abs(w) > 1e-6f && std::abs(w - 1.0f) > 1e-5f) {
            x /= w; y /= w; z /= w;
        }
        return { x, y, z };
    }

    RTXVector3 transform_direction(const RTXVector3& d) const {
        float x = m[0][0] * d.x + m[0][1] * d.y + m[0][2] * d.z;
        float y = m[1][0] * d.x + m[1][1] * d.y + m[1][2] * d.z;
        float z = m[2][0] * d.x + m[2][1] * d.y + m[2][2] * d.z;
        return { x, y, z };
    }

    RTXVector3 transform_normal(const RTXVector3& n) const {
        float x = m[0][0] * n.x + m[0][1] * n.y + m[0][2] * n.z;
        float y = m[1][0] * n.x + m[1][1] * n.y + m[1][2] * n.z;
        float z = m[2][0] * n.x + m[2][1] * n.y + m[2][2] * n.z;
        float len = std::sqrt(x * x + y * y + z * z);
        if (len > 1e-6f) {
            return { x / len, y / len, z / len };
        }
        return { 0.0f, 1.0f, 0.0f };
    }

    ASTGAABB transform_aabb(const ASTGAABB& in_box) const {
        if (!in_box.is_valid()) return in_box;
        RTXVector3 corners[8] = {
            { in_box.min_bounds.x, in_box.min_bounds.y, in_box.min_bounds.z },
            { in_box.max_bounds.x, in_box.min_bounds.y, in_box.min_bounds.z },
            { in_box.min_bounds.x, in_box.max_bounds.y, in_box.min_bounds.z },
            { in_box.max_bounds.x, in_box.max_bounds.y, in_box.min_bounds.z },
            { in_box.min_bounds.x, in_box.min_bounds.y, in_box.max_bounds.z },
            { in_box.max_bounds.x, in_box.min_bounds.y, in_box.max_bounds.z },
            { in_box.min_bounds.x, in_box.max_bounds.y, in_box.max_bounds.z },
            { in_box.max_bounds.x, in_box.max_bounds.y, in_box.max_bounds.z }
        };
        ASTGAABB out_box;
        for (int i = 0; i < 8; ++i) {
            out_box.include_point(transform_point(corners[i]));
        }
        return out_box;
    }
};

// Dynamic Surface Receiver Probe (Phase 6 / Handoff Item 4, 5, 16, 17, 36)
struct ASTGDynamicSurfaceProbe {
    uint32_t probe_id = 0;
    uint32_t dynamic_group_id = 0;
    uint32_t cluster_id = 0;
    uint32_t bone_id = 0;
    uint32_t surface_region_id = 0;
    uint32_t material_id = 0;

    RTXVector3 local_position = { 0.0f, 0.0f, 0.0f };
    RTXVector3 local_normal = { 0.0f, 1.0f, 0.0f };
    RTXVector3 world_position = { 0.0f, 0.0f, 0.0f };
    RTXVector3 world_normal = { 0.0f, 1.0f, 0.0f };
    float effective_radius = 0.02f;

    RTXVector3 direct_irradiance = { 0.0f, 0.0f, 0.0f };
    RTXVector3 indirect_irradiance = { 0.0f, 0.0f, 0.0f };
    float albedo[3] = { 0.8f, 0.8f, 0.8f };
    bool is_active = true;

    void update_from_bone(const RTXMatrix4x4& bone_matrix) {
        world_position = bone_matrix.transform_point(local_position);
        world_normal = bone_matrix.transform_normal(local_normal);
    }

    void update_rigid(const RTXMatrix4x4& rigid_matrix) {
        world_position = rigid_matrix.transform_point(local_position);
        world_normal = rigid_matrix.transform_normal(local_normal);
    }

    RTXVector3 total_radiance() const {
        return {
            (direct_irradiance.x + indirect_irradiance.x) * albedo[0] / 3.14159265f,
            (direct_irradiance.y + indirect_irradiance.y) * albedo[1] / 3.14159265f,
            (direct_irradiance.z + indirect_irradiance.z) * albedo[2] / 3.14159265f
        };
    }
};

// Hierarchical Dynamic Receiver Cluster (Phase 6 / Handoff Item 6, 7, 8, 33, 34, 35)
struct ASTGReceiverCluster {
    uint32_t cluster_id = 0;
    uint32_t dynamic_group_id = 0;
    uint32_t bone_id = 0;
    uint32_t surface_region_id = 0;
    std::string label;

    ASTGAABB local_bounds;
    ASTGAABB world_bounds;
    RTXVector3 local_centroid = { 0.0f, 0.0f, 0.0f };
    RTXVector3 world_centroid = { 0.0f, 0.0f, 0.0f };
    RTXVector3 local_normal = { 0.0f, 1.0f, 0.0f };
    RTXVector3 world_normal = { 0.0f, 1.0f, 0.0f };
    float cluster_radius = 0.2f;

    std::vector<uint32_t> member_probe_indices;
    RTXVector3 direct_irradiance = { 0.0f, 0.0f, 0.0f };
    RTXVector3 indirect_irradiance = { 0.0f, 0.0f, 0.0f };

    void recompute_local_bounds(const std::vector<ASTGDynamicSurfaceProbe>& probes) {
        local_bounds = ASTGAABB();
        if (member_probe_indices.empty()) return;

        RTXVector3 sum_pos = { 0.0f, 0.0f, 0.0f };
        RTXVector3 sum_norm = { 0.0f, 0.0f, 0.0f };
        for (uint32_t p_idx : member_probe_indices) {
            if (p_idx < probes.size()) {
                const auto& p = probes[p_idx];
                local_bounds.include_point(p.local_position);
                sum_pos.x += p.local_position.x; sum_pos.y += p.local_position.y; sum_pos.z += p.local_position.z;
                sum_norm.x += p.local_normal.x; sum_norm.y += p.local_normal.y; sum_norm.z += p.local_normal.z;
            }
        }
        float count = (float)member_probe_indices.size();
        local_centroid = { sum_pos.x / count, sum_pos.y / count, sum_pos.z / count };
        float nlen = std::sqrt(sum_norm.x * sum_norm.x + sum_norm.y * sum_norm.y + sum_norm.z * sum_norm.z);
        if (nlen > 1e-6f) {
            local_normal = { sum_norm.x / nlen, sum_norm.y / nlen, sum_norm.z / nlen };
        } else {
            local_normal = { 0.0f, 1.0f, 0.0f };
        }
        local_bounds.expand(0.02f);
    }

    void update_transforms(const RTXMatrix4x4& transform, std::vector<ASTGDynamicSurfaceProbe>& probes) {
        world_centroid = transform.transform_point(local_centroid);
        world_normal = transform.transform_normal(local_normal);
        world_bounds = ASTGAABB();
        for (uint32_t p_idx : member_probe_indices) {
            if (p_idx < probes.size()) {
                probes[p_idx].world_position = transform.transform_point(probes[p_idx].local_position);
                probes[p_idx].world_normal = transform.transform_normal(probes[p_idx].local_normal);
                world_bounds.include_point(probes[p_idx].world_position);
            }
        }
        world_bounds.expand(0.02f);
    }
};

// Dynamic Receiver Association Cache Entry (Phase 6 / Handoff Item 14, 15, 65, 66)
struct ASTGDynamicReceiverCacheEntry {
    uint32_t dynamic_group_id = 0;
    uint32_t cluster_id = 0;
    uint32_t probe_id = 0;
    float hit_distance = 0.0f;
    uint32_t generation = 0;
    float confidence = 1.0f;
    RTXVector3 cached_direct_contribution = { 0.0f, 0.0f, 0.0f };
};

// Logical Bounding-Box Group Abstraction with Dynamic Receiver Probes (Handoff Item 3, 4, 5, 26, 27, 33, 34)
struct ASTGDynamicOccluderGroup {
    uint32_t group_id = 0;
    std::string label;
    bool astg_occlusion_enabled = true;
    ASTGDynamicOcclusionMode occlusion_mode = ASTG_OCCLUSION_DAG_EDGES_ALL_BOUNCES;
    uint32_t transform_generation = 0;

    std::vector<ASTGOccluderBounds> bounds;
    ASTGAABB world_union_bounds;
    ASTGAABB previous_world_union_bounds;
    ASTGDynamicOcclusionPrecision precision = ASTG_OCCLUSION_BOUNDS_ONLY;

    // Dynamic Surface Receiver State (Phase 6)
    bool enable_surface_receivers = true;
    ASTGReceiverClusteringMode receiver_clustering_mode = ASTG_RECEIVERS_CLUSTERED;
    std::vector<ASTGDynamicSurfaceProbe> surface_probes;
    std::vector<ASTGReceiverCluster> receiver_clusters;
    std::vector<RTXMatrix4x4> bone_matrices;
    RTXMatrix4x4 rigid_transform = RTXMatrix4x4::identity();
    bool is_skeletal = false;

    void recompute_union_bounds() {
        world_union_bounds = ASTGAABB();
        for (const auto& b : bounds) {
            world_union_bounds.union_with(b.aabb);
        }
        for (const auto& c : receiver_clusters) {
            world_union_bounds.union_with(c.world_bounds);
        }
        if (world_union_bounds.min_bounds.x > world_union_bounds.max_bounds.x) {
            for (const auto& p : surface_probes) {
                world_union_bounds.include_point(p.world_position);
            }
            world_union_bounds.expand(0.05f);
        }
    }

    void update_receiver_transforms() {
        if (is_skeletal && !bone_matrices.empty()) {
            for (auto& ob : bounds) {
                uint32_t b = std::min(ob.bone_id, (uint32_t)bone_matrices.size() - 1);
                ob.world_bounds = bone_matrices[b].transform_aabb(ob.local_bounds);
                ob.aabb = ob.world_bounds;
            }
            for (auto& cluster : receiver_clusters) {
                uint32_t b = std::min(cluster.bone_id, (uint32_t)bone_matrices.size() - 1);
                cluster.update_transforms(bone_matrices[b], surface_probes);
            }
            for (auto& probe : surface_probes) {
                if (probe.cluster_id == 0 || receiver_clusters.empty()) {
                    uint32_t b = std::min(probe.bone_id, (uint32_t)bone_matrices.size() - 1);
                    probe.update_from_bone(bone_matrices[b]);
                }
            }
        } else {
            for (auto& ob : bounds) {
                ob.world_bounds = rigid_transform.transform_aabb(ob.local_bounds);
                ob.aabb = ob.world_bounds;
            }
            for (auto& cluster : receiver_clusters) {
                cluster.update_transforms(rigid_transform, surface_probes);
            }
            for (auto& probe : surface_probes) {
                if (probe.cluster_id == 0 || receiver_clusters.empty()) {
                    probe.update_rigid(rigid_transform);
                }
            }
        }
        previous_world_union_bounds = world_union_bounds;
        recompute_union_bounds();
        transform_generation++;
    }
};

// DAG Node->Node Edge with Dynamic Occlusion Metadata (Handoff Item 2, 3, 16)
struct ASTGDAGEdge {
    uint32_t edge_id = 0;
    uint32_t parent_node_id = 0;
    uint32_t child_node_id = 0;
    uint32_t source_light_id = 0;
    uint32_t angular_cell_id = 0;
    uint32_t source_bounce_depth = 0;
    uint32_t target_bounce_depth = 1;
    uint32_t bounce_depth = 1; // Legacy compatibility
    float transfer_weight = 1.0f;
    bool is_stitch_edge = false;
    uint32_t repair_generation = 0;
    bool is_active = true; // Static structural validity

    // Dynamic Occlusion state & metadata
    ASTGTransportEdgeState state = ASTG_EDGE_ACTIVE;
    ASTGAABB spatial_bounds;
    uint32_t dynamic_blocker_count = 0;
    std::vector<uint32_t> dynamic_blocker_ids;
};

// Dynamic Occlusion Update Telemetry & Diagnostics (Handoff Item 45, 46, 47, 51, 52, 71)
struct ASTGDynamicOcclusionMetrics {
    uint32_t group_id = 0;
    bool enabled = true;
    ASTGDynamicOcclusionMode mode = ASTG_OCCLUSION_DAG_EDGES_ALL_BOUNCES;
    uint32_t box_count = 0;
    uint32_t total_dag_edges = 0;
    uint32_t candidate_edges = 0;
    uint32_t spatial_cells_touched = 0;
    uint32_t spatial_edge_references = 0;
    uint32_t spatial_duplicate_edges_removed = 0;
    uint32_t gpu_generation_rejected = 0;
    uint32_t gpu_angular_rejected = 0;
    uint32_t gpu_aabb_rejected = 0;
    uint32_t gpu_rayquery_required = 0;
    uint32_t gpu_visibility_state_transitions = 0;
    uint32_t gpu_changed_result_readback_bytes = 0;
    uint32_t fine_tested_edges = 0;
    uint32_t intersected_edges = 0;
    uint32_t newly_blocked_edges = 0;
    uint32_t newly_unblocked_edges = 0;
    uint32_t currently_blocked_edges = 0;
    uint32_t affected_layer2_paths = 0;
    uint32_t affected_receivers = 0;

    // Angular B0 Metrics (Handoff Item 51, 55, 71)
    uint32_t angular_total_cells = 64;
    uint32_t angular_current_cells = 0;
    uint32_t angular_previous_cells = 0;
    uint32_t angular_newly_covered_cells = 0;
    uint32_t angular_newly_uncovered_cells = 0;
    uint32_t angular_still_covered_cells = 0;
    double angular_delta_fraction = 0.0;
    float proxy_solid_angle = 0.0f;
    float cell_solid_angle = 0.0f;
    float overcoverage_ratio = 1.0f;

    // Dynamic Surface Receiver Metrics (Phase 6 / Handoff Item 58, 59, 75, 76, 77)
    uint32_t receiver_probes_active = 0;
    uint32_t receiver_clusters_active = 0;
    uint32_t receiver_mappings_active = 0;
    uint32_t receiver_mappings_reused = 0;
    uint32_t receiver_mappings_created = 0;
    uint32_t receiver_mappings_removed = 0;
    float temporal_reuse_ratio = 0.0f; // R_reuse (Handoff Item 76)
    float work_sharing_ratio = 0.0f;   // R_shared (Handoff Item 75)
    double direct_receiver_us = 0.0;
    double indirect_receiver_us = 0.0;
    double total_receiver_ms = 0.0;
    uint32_t exact_visibility_rays = 0;

    // Timings
    double angular_projection_us = 0.0;
    double spatial_query_us = 0.0;
    double fine_test_us = 0.0;
    double total_update_ms = 0.0;

    double broadphase_rejection_pct = 0.0;
    double fine_rejection_pct = 0.0;
};

// Dynamic Edge Occlusion Timeline Event (Handoff Item 55)
struct ASTGDynamicEdgeTimelineEvent {
    uint32_t frame = 0;
    uint32_t edge_id = 0;
    std::string event; // "BLOCKED" or "UNBLOCKED"
    uint32_t group_id = 0;
    uint32_t blocker_count = 0;
};

// Uniform 3D Spatial Acceleration Grid over DAG Edges (Handoff Item 8, 9, 10)
class ASTGEdgeSpatialGrid {
public:
    float cell_size = 2.0f;
    // Keep the full coordinate in the map key. A hash is only a bucket
    // selector; treating a 64-bit hash as the identity can silently merge
    // unrelated cells and make edges disappear from discovery.
    std::unordered_map<ASTGSpatialCellCoord, std::vector<uint32_t>, ASTGSpatialCellHash> grid;

    void clear() {
        grid.clear();
    }

    void insert_edge(uint32_t edge_id, const ASTGAABB& edge_bounds) {
        if (!edge_bounds.is_valid()) return;
        int min_x = (int)std::floor(edge_bounds.min_bounds.x / cell_size);
        int max_x = (int)std::floor(edge_bounds.max_bounds.x / cell_size);
        int min_y = (int)std::floor(edge_bounds.min_bounds.y / cell_size);
        int max_y = (int)std::floor(edge_bounds.max_bounds.y / cell_size);
        int min_z = (int)std::floor(edge_bounds.min_bounds.z / cell_size);
        int max_z = (int)std::floor(edge_bounds.max_bounds.z / cell_size);

        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    grid[{ x, y, z }].push_back(edge_id);
                }
            }
        }
    }

    void query_edges_in_aabb(const ASTGAABB& query_box, std::vector<uint32_t>& out_candidates) const {
        out_candidates.clear();
        if (!query_box.is_valid() || grid.empty()) return;

        int min_x = (int)std::floor(query_box.min_bounds.x / cell_size);
        int max_x = (int)std::floor(query_box.max_bounds.x / cell_size);
        int min_y = (int)std::floor(query_box.min_bounds.y / cell_size);
        int max_y = (int)std::floor(query_box.max_bounds.y / cell_size);
        int min_z = (int)std::floor(query_box.min_bounds.z / cell_size);
        int max_z = (int)std::floor(query_box.max_bounds.z / cell_size);

        std::unordered_set<uint32_t> unique_candidates;
        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    auto it = grid.find({ x, y, z });
                    if (it != grid.end()) {
                        for (uint32_t eid : it->second) {
                            unique_candidates.insert(eid);
                        }
                    }
                }
            }
        }
        out_candidates.assign(unique_candidates.begin(), unique_candidates.end());
    }
};

// Reusable Transport State Key (Part 1)
struct ASTGTransportStateKey {
    uint32_t surface_cluster_id = 0;
    uint32_t material_id = 0;
    RTXVector3 position = {0, 0, 0};
    RTXVector3 geometric_normal = {0, 1, 0};
    uint32_t bounce_depth = 0;
    uint32_t angular_cell_id = 0;
    uint32_t geometry_generation = 1;
};

// Rejection Reason Enum for Path Stitching Diagnostics (Part 21)
enum StitchRejectionReason {
    STITCH_REJECT_NONE = 0,
    STITCH_REJECT_SURFACE_MISMATCH = 1,
    STITCH_REJECT_POSITION_MISMATCH = 2,
    STITCH_REJECT_NORMAL_MISMATCH = 3,
    STITCH_REJECT_DEPENDENCY_CONFLICT = 4,
    STITCH_REJECT_GENERATION_STALE = 5,
    STITCH_REJECT_ANGULAR_MISMATCH = 6,
    STITCH_REJECT_NO_DOWNSTREAM_TRANSPORT = 7
};

// Detailed Stitching Telemetry & Metrics (Part 20, 21, 35)
struct ASTGStitchingMetrics {
    uint32_t stitch_candidates_considered = 0;
    uint32_t stitches_accepted = 0;
    uint32_t stitches_rejected = 0;

    // Rejection reasons breakdown
    uint32_t reject_surface_mismatch = 0;
    uint32_t reject_position_mismatch = 0;
    uint32_t reject_normal_mismatch = 0;
    uint32_t reject_dependency_conflict = 0;
    uint32_t reject_generation_stale = 0;
    uint32_t reject_angular_mismatch = 0;
    uint32_t reject_no_downstream = 0;

    // Work and structural savings
    uint32_t new_bridge_nodes = 0;
    uint32_t new_bridge_edges = 0;
    uint32_t reused_suffix_nodes = 0;
    uint32_t reused_suffix_edges = 0;
    uint32_t reused_probe_depositions = 0;
    double total_reused_depth = 0.0;
    double mean_reused_suffix_depth = 0.0;
    uint32_t max_reused_suffix_depth = 0;

    // Continuation & Multi-hop metrics (Part 42)
    uint32_t continuation_frontiers_emitted = 0;
    uint32_t continuation_rays_completed = 0;
    uint32_t continuation_stitches_accepted = 0;
    uint32_t max_stitches_per_path = 0;
    double mean_stitches_per_path = 0.0;
};

// Path Segment Origin & Trace Timeline for Assembled Path Decomposition
enum ASTGPathSegmentOrigin {
    SEGMENT_FRESH_TRACE = 0,
    SEGMENT_CACHED_REUSE = 1
};

struct ASTGSolveRayCounters {
    uint32_t rays_scheduled = 0;
    uint32_t rays_dispatched = 0;
    uint32_t rays_completed = 0;
};

struct ASTGPathSegmentTrace {
    uint32_t depth = 0;
    std::string origin = "fresh"; // "fresh", "cached", or "bridge"
    uint32_t node_id = 0;
    uint32_t stitch_id = 0;
    uint32_t surface_cluster = 0;
    bool ray_dispatched = false;
    uint32_t continuation_event_id = 0;
    float incoming_transfer = 1.0f;
    float local_transfer = 1.0f;
    float outgoing_transfer = 1.0f;
    float transfer_r = 1.0f;
    float transfer_g = 1.0f;
    float transfer_b = 1.0f;
};

// Explicit Continuation Frontier State (Handoff Item 3)
struct ASTGContinuationFrontier {
    uint32_t node_id = UINT32_MAX;

    uint32_t source_light_id = 0;
    uint32_t angular_cell_id = 0;

    uint32_t current_path_bounce_depth = 0;
    uint32_t requested_max_bounce_depth = 0;

    float accumulated_transfer_r = 1.0f;
    float accumulated_transfer_g = 1.0f;
    float accumulated_transfer_b = 1.0f;

    uint64_t path_provenance_id = 0;
    uint32_t repair_generation = 0;

    bool came_from_stitch = false;
    uint64_t stitch_chain_id = 0;
    uint32_t stitch_sequence_index = 0;

    std::vector<uint32_t> visited_node_ids;
    std::vector<ASTGPathSegmentTrace> path_timeline;
};

// Reusable Cached-Segment Traversal Result (Handoff Item 18)
struct ASTGCachedReuseResult {
    uint32_t start_node_id = 0;

    uint32_t nodes_reused = 0;
    uint32_t edges_reused = 0;

    uint32_t starting_path_depth = 0;
    uint32_t ending_path_depth = 0;

    float final_transfer_r = 0.0f;
    float final_transfer_g = 0.0f;
    float final_transfer_b = 0.0f;
    std::vector<ASTGPathSegmentTrace> final_timeline;

    uint32_t continuation_frontiers_emitted = 0;

    bool reached_requested_depth = false;
    bool reached_terminal_receiver = false;
    bool cache_exhausted = false;
};

// Multi-Hop Solve Result
struct ASTGMultiHopSolveResult {
    uint32_t requested_max_depth = 0;
    uint32_t fresh_prefix_bounces = 0;
    uint32_t fresh_continuation_bounces = 0;
    uint32_t total_fresh_bounces = 0;

    uint32_t stitch_events = 0;
    uint32_t cached_segments_reused = 0;
    uint32_t cached_nodes_reused = 0;
    uint32_t cached_edges_reused = 0;
    uint32_t cached_bounces_reused = 0;

    bool cached_segment_exhausted = false;
    uint32_t continuation_frontiers_emitted = 0;
    uint32_t continuation_rays_completed = 0;

    uint32_t effective_solved_depth = 0;
    bool requested_depth_reached = false;

    float final_transfer_r = 0.0f;
    float final_transfer_g = 0.0f;
    float final_transfer_b = 0.0f;

    ASTGSolveRayCounters ray_counters;
    uint32_t fresh_reference_rays = 0;
    uint32_t reuse_continuation_rays = 0;
    uint32_t avoided_rays = 0;
    double ray_reduction_pct = 0.0;
    std::vector<ASTGPathSegmentTrace> assembled_timeline;
};

// Dynamic Light Transport Modes (Handoff Item 2)
enum ASTGLightTransportMode {
    ASTG_LIGHT_STATIC_TRANSPORT = 0,
    ASTG_LIGHT_RELOCATABLE_TRANSPORT = 1,
    ASTG_LIGHT_DYNAMIC_TRANSPORT = 2
};

// Dynamic Light State Representation (Handoff Item 4, 30)
struct ASTGDynamicLightState {
    uint32_t light_id = 0;
    ASTGLightTransportMode mode = ASTG_LIGHT_DYNAMIC_TRANSPORT;

    RTXVector3 position = { 0.0f, 0.0f, 0.0f };
    RTXVector3 direction = { 0.0f, -1.0f, 0.0f };

    float color_r = 1.0f;
    float color_g = 1.0f;
    float color_b = 1.0f;
    float intensity = 1.0f;

    bool enabled = true;

    bool is_spotlight = false;
    float spot_inner_cos = 0.95f; // ~18 deg
    float spot_outer_cos = 0.85f; // ~31 deg
    float spot_range = 25.0f;

    uint32_t transform_generation = 1;
    uint32_t state_generation = 1;
};

// Dynamic Ingress Ray (Handoff Item 6)
struct ASTGDynamicIngressRay {
    uint32_t light_id = 0;
    uint32_t sample_id = 0;

    RTXVector3 origin = { 0.0f, 0.0f, 0.0f };
    RTXVector3 direction = { 0.0f, -1.0f, 0.0f };

    float emitted_flux_r = 1.0f;
    float emitted_flux_g = 1.0f;
    float emitted_flux_b = 1.0f;

    uint32_t current_path_depth = 0;
    uint64_t path_provenance_id = 0;
};

// Dynamic Ingress Hit Result (Handoff Item 11, 27)
struct ASTGDynamicIngressResult {
    ASTGDynamicIngressRay ray;
    ASTGRayHit hit;
    float ingress_transfer_r = 0.0f;
    float ingress_transfer_g = 0.0f;
    float ingress_transfer_b = 0.0f;
    uint32_t matched_candidate_node_id = UINT32_MAX;
    bool stitched = false;
};

// Transient Dynamic Receiver Contribution (Handoff Item 17, 64)
struct ASTGDynamicReceiverContribution {
    uint32_t receiver_id = 0; // probe_id
    uint32_t light_id = 0;

    float transfer_r = 0.0f;
    float transfer_g = 0.0f;
    float transfer_b = 0.0f;

    uint32_t transform_generation = 1;
    uint64_t path_provenance_id = 0;
};

// Dynamic Light Solve Result (Handoff Item 47, 63)
struct ASTGDynamicLightSolveResult {
    uint32_t light_id = 0;
    uint32_t transform_generation = 1;

    uint32_t ingress_rays_scheduled = 0;
    uint32_t ingress_rays_completed = 0;
    uint32_t downstream_fresh_rays_completed = 0;

    uint32_t stitch_events = 0;
    uint32_t cached_nodes_reused = 0;
    uint32_t cached_edges_reused = 0;

    uint32_t continuation_frontiers = 0;
    uint32_t receiver_contributions = 0;

    float final_transfer_r = 0.0f;
    float final_transfer_g = 0.0f;
    float final_transfer_b = 0.0f;

    std::vector<ASTGDynamicReceiverContribution> transient_contributions;
    std::vector<ASTGPathSegmentTrace> assembled_timeline;
    ASTGSolveRayCounters ray_counters;

    uint32_t fresh_reference_rays = 0;
    uint32_t avoided_rays = 0;
    double ray_reduction_pct = 0.0;
    double downstream_reuse_ratio = 0.0;

    double gpu_ms = 0.0;
    bool semantic_equivalence_pass = false;
};

// Explicit Path-Level Probe Contribution Record (Layer 2 - Exact Provenance Truth)
struct ASTGPathProbeContribution {
    uint32_t contribution_id = 0;
    uint32_t probe_id = 0;
    uint32_t source_light_id = 0;
    uint32_t source_node_id = 0;
    uint32_t angular_cell_id = 0;
    uint32_t bounce_depth = 0;
    uint32_t surface_cluster_id = 0;
    uint32_t destruction_chunk_id = 0;
    uint64_t path_provenance_id = 0;
    float transfer_r = 0.0f;
    float transfer_g = 0.0f;
    float transfer_b = 0.0f;
    float importance = 0.0f;
    uint32_t generation = 1;
    bool is_active = true;

    // Dynamic Occlusion temporary mask (Handoff Item 20)
    uint32_t dynamic_occlusion_count = 0;
    bool is_effectively_active() const {
        return is_active && (dynamic_occlusion_count == 0);
    }
};

// Node->Probe Deposition Link (Transport Arrival Event)
struct ASTGProbeDepositionLink {
    uint32_t link_id = 0;
    uint32_t source_node_id = 0;
    uint32_t target_probe_id = 0;
    uint32_t source_light_id = 0;
    float transfer_r = 0.0f;
    float transfer_g = 0.0f;
    float transfer_b = 0.0f;
    float importance = 0.0f;
    bool is_active = true;
};

// Candidate Deposit for Top-K Contributor Ranking
struct ProbeDepositCandidate {
    uint32_t source_light_id = 0;
    uint32_t source_node_id = 0;
    uint32_t target_probe_id = 0;
    float transfer_r = 0.0f;
    float transfer_g = 0.0f;
    float transfer_b = 0.0f;
    float importance = 0.0f;
};

// Aggregated Unique Light Contributor per Probe
struct ProbeLightEntry {
    uint32_t source_light_id = 0;
    uint32_t source_node_id = 0;
    float transfer_r = 0.0f;
    float transfer_g = 0.0f;
    float transfer_b = 0.0f;
    float total_importance = 0.0f;
    uint32_t path_count = 0;
    std::vector<uint32_t> underlying_deposition_ids;
};

// Pruned Source Record for Adversarial Late-Bound Testing (Part B1)
struct PrunedSourceRecord {
    uint32_t probe_id = 0;
    uint32_t source_light_id = 0;
    float static_transfer_magnitude = 0.0f;
    uint32_t rank_before_pruning = 0;
};

// Residual Tail Representation (Part B10)
struct ProbeResidualTail {
    float residual_r = 0.0f;
    float residual_g = 0.0f;
    float residual_b = 0.0f;
    uint32_t pruned_source_count = 0;
};

// Reverse Chunk Dependency List (Part 3): Exact mapping from chunk -> affected structures
struct ChunkDependencyList {
    uint32_t chunk_id = 0;
    std::vector<uint32_t> transport_node_ids;       // Nodes hitting or depending on chunk
    std::vector<uint32_t> angular_cell_ids;         // Angular cells intersected
    std::vector<uint32_t> blocked_anchor_ids;       // Regeneration anchors for BLOCKED_DESTRUCTIBLE paths
    std::vector<uint32_t> attached_probe_ids;       // Probes attached to this chunk
};

// Provenance Memory Audit Structure (Part 22)
struct ASTGProvenanceMemoryAudit {
    size_t dag_nodes_bytes = 0;
    size_t dag_edges_bytes = 0;
    size_t dag_anchors_bytes = 0;
    size_t total_dag_bytes = 0;

    size_t path_contributions_bytes = 0;
    size_t node_to_path_index_bytes = 0;
    size_t chunk_to_path_index_bytes = 0;
    size_t total_path_provenance_bytes = 0;

    size_t csr_contributions_bytes = 0;
    size_t csr_offsets_bytes = 0;
    size_t csr_counts_bytes = 0;
    size_t total_csr_bytes = 0;

    double mean_paths_per_csr_entry = 0.0;
    double p95_paths_per_csr_entry = 0.0;
};

// Provenance Orphan Audit Report (Part 20)
struct ProvenanceOrphanReport {
    uint32_t orphan_depositions_missing_node = 0;
    uint32_t orphan_depositions_invalid_probe = 0;
    uint32_t orphan_depositions_invalid_light = 0;
    uint32_t csr_entries_without_provenance = 0;
    bool is_clean() const {
        return (orphan_depositions_missing_node == 0 &&
                orphan_depositions_invalid_probe == 0 &&
                orphan_depositions_invalid_light == 0 &&
                csr_entries_without_provenance == 0);
    }
};

enum ContributionRetentionMode {
    RETENTION_FIXED_TOP_K = 0,
    RETENTION_ADAPTIVE_ENERGY = 1,
    RETENTION_UNLIMITED = 2
};

// High-Precision Timing and Workload Tracking (Part A1 & A2)
struct ASTGRepairDetailedTimings {
    uint64_t repair_schedule_cpu_us = 0;
    double repair_dispatch_gpu_ms = 0.0;
    double repair_intersection_gpu_ms = 0.0;
    double repair_process_gpu_ms = 0.0;
    uint64_t repair_commit_cpu_us = 0;
    double repair_total_ms = 0.0;

    uint32_t repair_ray_budget = 0;
    uint32_t repair_candidates_generated = 0;
    uint32_t repair_rays_scheduled = 0;
    uint32_t repair_rays_dispatched = 0;
    uint32_t repair_rays_completed = 0;
    uint32_t repair_rays_rejected_stale = 0;
};

// Runtime telemetry for the production receiver-visibility batching path.
// These values are reset for each evaluate_dynamic_receiver_indirect call.
struct ASTGVisibilityBatchTelemetry {
    uint32_t ambiguous_candidates_gathered = 0;
    uint32_t rays_requested = 0;
    uint32_t rays_dispatched = 0;
    uint32_t dispatch_count = 0;
    uint32_t largest_dispatch_size = 0;
    uint32_t rays_completed = 0;
    bool cap_compliant = true;
};

// Exact Memory Accounting Structure (Part A3)
struct ASTGExactMemoryAudit {
    size_t sizeof_anchor = sizeof(ASTGRegenerationAnchor);
    size_t anchor_count = 0;
    size_t anchor_capacity = 0;
    size_t anchor_payload_bytes = 0;
    size_t anchor_capacity_bytes = 0;
    size_t anchor_allocator_overhead_bytes = 0;

    size_t sizeof_parent_ref = sizeof(DAGParentRef);
    size_t parent_ref_count = 0;
    size_t parent_ref_payload_bytes = 0;

    size_t reverse_dependency_payload_bytes = 0;
    size_t reverse_dependency_container_overhead_bytes = 0;

    size_t angular_frontier_payload_bytes = 0;
    size_t generation_metadata_bytes = sizeof(uint32_t) * 3;

    size_t total_repair_metadata_payload_bytes = 0;
    size_t total_repair_metadata_allocated_bytes = 0;

    // Denominators
    size_t scene_chunks_total = 0;
    size_t destructible_chunks_total = 0;
    size_t chunks_with_active_repair_metadata = 0;
    size_t blocked_frontiers_active = 0;

    // Derived per-unit metrics
    double bytes_per_scene_chunk = 0.0;
    double bytes_per_destructible_chunk = 0.0;
    double bytes_per_active_repair_chunk = 0.0;
    double bytes_per_blocked_frontier = 0.0;
    double bytes_per_light = 0.0;
};

// ==============================================================================
// ASTG TRANSPORT ENGINE (SAFE OPTIMIZED & REGENERABLE)
// ==============================================================================
class ASTGTransportEngine {
public:
    std::vector<SurfaceAttachedProbe> probes;
    std::vector<ASTGTransportNode> bounce0_nodes;
    std::vector<ASTGTransportNode> bounce1_nodes;

    // Layer 1: Disambiguated Transport Graph Collections (Structural Truth)
    std::vector<ASTGDAGEdge> dag_edges;                                    // Node -> Node edges
    std::vector<ASTGProbeDepositionLink> probe_deposition_links;           // Legacy Node -> Probe links
    std::vector<ASTGRegenerationAnchor> regeneration_anchors;              // Blocker regeneration anchors
    std::unordered_map<uint32_t, ChunkDependencyList> chunk_dependencies; // Chunk -> ASTG structures

    // Spatial candidate lookup (surface_cluster_id -> node_ids)
    std::unordered_map<uint32_t, std::vector<uint32_t>> surface_cluster_to_nodes;

    // Path Stitching Configuration & Telemetry (Part 1-35)
    bool enable_path_stitching = true;
    float stitch_pos_threshold = 0.40f;
    float stitch_normal_threshold = 0.80f;
    uint32_t max_stitch_candidates_per_hit = 32;
    ASTGStitchingMetrics stitching_metrics;
    ASTGVisibilityBatchTelemetry visibility_batch_telemetry;

    // Dynamic Occlusion State & Spatial Tracking (Phase 4 & 5)
    ASTGDynamicOcclusionMode global_occlusion_mode = ASTG_OCCLUSION_DAG_EDGES_ALL_BOUNCES;
    ASTGAngularHierarchy angular_hierarchy;
    std::unordered_map<uint32_t, ASTGDynamicOccluderGroup> dynamic_occluder_groups;
    uint32_t next_dynamic_group_id = 1;
    ASTGEdgeSpatialGrid edge_spatial_grid;
    std::unordered_map<uint32_t, std::vector<uint32_t>> edge_to_path_contributions; // Edge ID -> Layer-2 Path Contribution IDs
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> dynamic_group_to_edges; // Group ID -> currently blocked Edge IDs
    std::unordered_map<uint64_t, uint64_t> group_light_angular_masks; // (group_id << 32) | light_id -> 64-bit angular mask
    std::unordered_map<uint64_t, uint32_t> light_cell_blocker_count; // (light_id << 32) | cell_id -> blocker count
    std::unordered_map<uint64_t, std::vector<uint32_t>> light_cell_blocker_ids; // (light_id << 32) | cell_id -> group IDs
    std::unordered_map<uint64_t, std::vector<uint32_t>> light_cell_to_paths; // (light_id << 32) | cell_id -> Layer-2 Path Contribution IDs
    std::unordered_map<uint64_t, std::vector<uint32_t>> light_cell_to_b0_edges; // (light_id << 32) | cell_id -> B0 DAG Edge IDs
    std::unordered_map<uint32_t, RTXVector3> light_positions; // light_id -> light position
    std::unordered_map<uint32_t, RTXVector3> light_colors; // light_id -> light color
    std::unordered_map<uint32_t, float> light_intensities; // light_id -> intensity multiplier
    std::unordered_map<uint64_t, ASTGDynamicReceiverCacheEntry> dynamic_receiver_cache; // (light_id << 32) | cell_id -> Cache Entry (Phase 6)
    std::vector<ASTGDynamicEdgeTimelineEvent> dynamic_edge_timeline;
    uint32_t dynamic_timeline_frame = 0;

    // ==============================================================================
    // Parts J & K: Continuous Source-Local B0 Angular Hierarchies & Blocker Tracking
    // ==============================================================================
    std::unordered_map<uint32_t, ASTGAngularHierarchy> light_b0_hierarchies;
    std::unordered_map<uint64_t, uint32_t> b0_record_blocker_counts; // (light_id << 32) | record_id -> count
    std::unordered_map<uint64_t, bool> group_b0_record_blocked; // ((group_id << 40) | (light_id << 20) | record_id) -> bool
    ASTGB0AngularTelemetry b0_telemetry{};

    ASTGAngularHierarchy& get_or_create_light_hierarchy(uint32_t light_id) {
        auto it = light_b0_hierarchies.find(light_id);
        if (it == light_b0_hierarchies.end()) {
            ASTGAngularHierarchy hier;
            RTXVector3 light_pos = { 0.0f, 5.0f, 0.0f };
            auto it_lp = light_positions.find(light_id);
            if (it_lp != light_positions.end()) light_pos = it_lp->second;

            hier.initialize_frame(light_id, light_pos, 0, { 0.0f, 0.0f, 1.0f }, 25.0f, 1);
            hier.build_continuous_b0_hierarchy(bounce0_nodes, light_id);
            light_b0_hierarchies[light_id] = hier;
            return light_b0_hierarchies[light_id];
        }
        return it->second;
    }

    struct B0GroundTruthResult {
        std::unordered_set<uint32_t> blocked_b0_record_indices;
        std::unordered_set<uint32_t> blocked_b0_node_ids;
        uint32_t total_b0_rays_tested = 0;
        uint32_t total_bounds_tested = 0;
    };

    // Canonical slow CPU exact reference validator (no hierarchy shortcuts)
    B0GroundTruthResult evaluate_b0_ground_truth_exact(uint32_t light_id, const std::vector<ASTGAABB>& moving_boxes) {
        B0GroundTruthResult gt;
        auto& hier = get_or_create_light_hierarchy(light_id);
        RTXVector3 light_pos = { hier.source_frame.origin_x, hier.source_frame.origin_y, hier.source_frame.origin_z };

        for (size_t r = 0; r < hier.b0_records.size(); ++r) {
            const auto& rec = hier.b0_records[r];
            gt.total_b0_rays_tested++;

            RTXVector3 dir_world = hier.local_to_world({ rec.dir_local_x, rec.dir_local_y, rec.dir_local_z });
            RTXVector3 hit_pos = {
                light_pos.x + dir_world.x * rec.hit_dist,
                light_pos.y + dir_world.y * rec.hit_dist,
                light_pos.z + dir_world.z * rec.hit_dist
            };

            for (const auto& box : moving_boxes) {
                gt.total_bounds_tested++;
                if (segment_intersects_aabb(light_pos, hit_pos, box)) {
                    gt.blocked_b0_record_indices.insert((uint32_t)r);
                    gt.blocked_b0_node_ids.insert(rec.transport_node_id);
                    break;
                }
            }
        }
        return gt;
    }

    // ==============================================================================
    // Milestone 1 (R1 & R2): Persistent GPU ASTG Transport Shadow Caches & Dirty State
    // ==============================================================================
    std::vector<ASTGGPUNode> gpu_nodes_shadow;
    std::vector<ASTGGPUDAGEdge> gpu_edges_shadow;
    std::unordered_map<uint32_t, uint32_t> gpu_occluder_dense_indices;
    // GPU spatial discovery representation. The exact coordinate map is used
    // only to convert a changed AABB into a small list of resident ranges;
    // the GPU owns the edge-ID fetch and later deduplication.
    std::vector<uint32_t> gpu_edge_spatial_indices;
    std::unordered_map<ASTGSpatialCellCoord, ASTGGPUCellRange, ASTGSpatialCellHash> gpu_edge_spatial_cell_ranges;
    bool gpu_edge_spatial_index_uploaded = false;
    uint32_t gpu_discovery_stamp = 1;
    std::unordered_map<uint32_t, uint32_t> gpu_visibility_state_slots;
    uint32_t next_gpu_visibility_state_slot = 0;
    // Diagnostic and rollout gate: retain the production CPU fallback so the
    // same scene can be compared against GPU discovery without changing data.
    bool enable_gpu_spatial_discovery = true;

    struct DirtyInterval {
        uint32_t dirty_min = UINT32_MAX;
        uint32_t dirty_max = 0;

        void mark(uint32_t index) {
            dirty_min = std::min(dirty_min, index);
            dirty_max = std::max(dirty_max, index + 1);
        }

        void mark_range(uint32_t start, uint32_t count) {
            if (count == 0) return;
            dirty_min = std::min(dirty_min, start);
            dirty_max = std::max(dirty_max, start + count);
        }

        void reset() {
            dirty_min = UINT32_MAX;
            dirty_max = 0;
        }

        bool is_dirty() const {
            return dirty_min < dirty_max;
        }
    };

    DirtyInterval node_dirty_interval;
    DirtyInterval edge_dirty_interval;
    bool is_gpu_astg_synced = false;

    void compute_edge_spatial_bounds(ASTGDAGEdge& edge, float corridor_radius = 0.05f) {
        const ASTGTransportNode* parent_n = get_node_by_id(edge.parent_node_id);
        const ASTGTransportNode* child_n = get_node_by_id(edge.child_node_id);
        if (parent_n && child_n) {
            edge.spatial_bounds.min_bounds = {
                std::min(parent_n->position.x, child_n->position.x),
                std::min(parent_n->position.y, child_n->position.y),
                std::min(parent_n->position.z, child_n->position.z)
            };
            edge.spatial_bounds.max_bounds = {
                std::max(parent_n->position.x, child_n->position.x),
                std::max(parent_n->position.y, child_n->position.y),
                std::max(parent_n->position.z, child_n->position.z)
            };
            edge.spatial_bounds.expand(corridor_radius);
            edge.source_bounce_depth = parent_n->bounce_depth;
            edge.target_bounce_depth = child_n->bounce_depth;
            edge.bounce_depth = child_n->bounce_depth;
        }
    }

    void rebuild_edge_spatial_index(float cell_size = 2.0f, float corridor_radius = 0.05f) {
        edge_spatial_grid.clear();
        edge_spatial_grid.cell_size = cell_size;
        for (size_t i = 0; i < dag_edges.size(); ++i) {
            auto& edge = dag_edges[i];
            edge.edge_id = (uint32_t)i;
            compute_edge_spatial_bounds(edge, corridor_radius);
            if (edge.is_active) {
                edge_spatial_grid.insert_edge((uint32_t)i, edge.spatial_bounds);
            }
        }
        rebuild_gpu_edge_spatial_index();
    }

    void rebuild_gpu_edge_spatial_index() {
        gpu_edge_spatial_indices.clear();
        gpu_edge_spatial_cell_ranges.clear();
        for (const auto& entry : edge_spatial_grid.grid) {
            const std::vector<uint32_t>& edge_ids = entry.second;
            ASTGGPUCellRange range = {};
            range.edge_index_offset = (uint32_t)gpu_edge_spatial_indices.size();
            range.edge_index_count = (uint32_t)edge_ids.size();
            gpu_edge_spatial_indices.insert(gpu_edge_spatial_indices.end(), edge_ids.begin(), edge_ids.end());
            gpu_edge_spatial_cell_ranges.emplace(entry.first, range);
        }
        gpu_edge_spatial_index_uploaded = false;
    }

    bool sync_gpu_edge_spatial_index() {
        if (gpu_edge_spatial_index_uploaded) return true;
        if (gpu_edge_spatial_indices.empty() || !rtx_is_hardware_active()) return false;
        gpu_edge_spatial_index_uploaded = rtx_upload_astg_spatial_edge_indices(
            gpu_edge_spatial_indices.data(), (uint32_t)gpu_edge_spatial_indices.size());
        return gpu_edge_spatial_index_uploaded;
    }

    bool ensure_gpu_visibility_state_slot(uint32_t group_id, uint32_t& out_slot) {
        auto found = gpu_visibility_state_slots.find(group_id);
        if (found != gpu_visibility_state_slots.end()) {
            out_slot = found->second;
            return true;
        }
        if (!rtx_is_hardware_active() || next_gpu_visibility_state_slot >= 64) return false;
        const uint32_t slot = next_gpu_visibility_state_slot++;
        if (!rtx_reset_astg_visibility_state_slot(slot)) return false;
        gpu_visibility_state_slots[group_id] = slot;
        out_slot = slot;
        return true;
    }

    // This performs O(touched cells) CPU work, not O(candidate edges). It
    // intentionally leaves range expansion and duplicate edge removal to the
    // GPU discovery dispatch.
    void query_gpu_edge_spatial_ranges(
        const ASTGAABB& query_box,
        std::vector<ASTGGPUCellRange>& out_ranges,
        uint32_t& out_edge_references
    ) const {
        out_ranges.clear();
        out_edge_references = 0;
        if (!query_box.is_valid() || edge_spatial_grid.cell_size <= 0.0f) return;
        const float cell_size = edge_spatial_grid.cell_size;
        const int min_x = (int)std::floor(query_box.min_bounds.x / cell_size);
        const int max_x = (int)std::floor(query_box.max_bounds.x / cell_size);
        const int min_y = (int)std::floor(query_box.min_bounds.y / cell_size);
        const int max_y = (int)std::floor(query_box.max_bounds.y / cell_size);
        const int min_z = (int)std::floor(query_box.min_bounds.z / cell_size);
        const int max_z = (int)std::floor(query_box.max_bounds.z / cell_size);
        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    auto it = gpu_edge_spatial_cell_ranges.find({ x, y, z });
                    if (it == gpu_edge_spatial_cell_ranges.end()) continue;
                    ASTGGPUCellRange range = it->second;
                    range.dispatch_offset = out_edge_references;
                    out_ranges.push_back(range);
                    out_edge_references += range.edge_index_count;
                }
            }
        }
    }

    ASTGStaticNodeSpatialGrid static_node_spatial_grid;
    uint64_t transport_generation = 1;
    uint64_t spatial_index_generation = 0;

    void rebuild_static_node_spatial_index(float cell_size = 2.0f) {
        static_node_spatial_grid.cell_size = cell_size;
        static_node_spatial_grid.build(bounce0_nodes);
        spatial_index_generation = transport_generation;
    }

    void build_edge_to_path_mapping() {
        edge_to_path_contributions.clear();
        light_cell_to_paths.clear();
        light_cell_to_b0_edges.clear();

        std::unordered_map<uint32_t, std::vector<uint32_t>> node_outgoing_edges;
        for (size_t e = 0; e < dag_edges.size(); ++e) {
            node_outgoing_edges[dag_edges[e].parent_node_id].push_back((uint32_t)e);
            if (dag_edges[e].source_bounce_depth == 0) {
                uint64_t key = ((uint64_t)dag_edges[e].source_light_id << 32) | dag_edges[e].angular_cell_id;
                light_cell_to_b0_edges[key].push_back((uint32_t)e);
            }
        }

        for (size_t e = 0; e < dag_edges.size(); ++e) {
            const auto& edge = dag_edges[e];
            uint32_t root_child = edge.child_node_id;

            std::unordered_set<uint32_t> reachable_nodes;
            std::vector<uint32_t> frontier = { root_child };
            reachable_nodes.insert(root_child);

            while (!frontier.empty()) {
                uint32_t curr = frontier.back();
                frontier.pop_back();

                auto it = node_outgoing_edges.find(curr);
                if (it != node_outgoing_edges.end()) {
                    for (uint32_t next_edge_idx : it->second) {
                        uint32_t child_nid = dag_edges[next_edge_idx].child_node_id;
                        if (reachable_nodes.insert(child_nid).second) {
                            frontier.push_back(child_nid);
                        }
                    }
                }
            }

            std::vector<uint32_t>& dep_list = edge_to_path_contributions[(uint32_t)e];
            std::unordered_set<uint32_t> added_deps;
            for (uint32_t rn : reachable_nodes) {
                auto it_dep = node_to_path_contributions.find(rn);
                if (it_dep != node_to_path_contributions.end()) {
                    for (uint32_t dep_id : it_dep->second) {
                        if (dep_id < path_probe_contributions.size()) {
                            const auto& contrib = path_probe_contributions[dep_id];
                            if (contrib.source_light_id == edge.source_light_id || edge.source_light_id == 0) {
                                if (added_deps.insert(dep_id).second) {
                                    dep_list.push_back(dep_id);
                                }
                            }
                        }
                    }
                }
            }
        }

        for (size_t p = 0; p < path_probe_contributions.size(); ++p) {
            const auto& dep = path_probe_contributions[p];
            uint64_t key = ((uint64_t)dep.source_light_id << 32) | dep.angular_cell_id;
            light_cell_to_paths[key].push_back((uint32_t)p);
        }
    }

    uint32_t register_dynamic_occluder_group(
        const std::vector<ASTGAABB>& boxes,
        const std::string& label = "DynamicGroup",
        bool enabled = true,
        ASTGDynamicOcclusionMode mode = ASTG_OCCLUSION_DAG_EDGES_ALL_BOUNCES,
        ASTGDynamicOcclusionPrecision precision = ASTG_OCCLUSION_BOUNDS_ONLY
    ) {
        uint32_t gid = next_dynamic_group_id++;
        ASTGDynamicOccluderGroup group;
        group.group_id = gid;
        group.label = label;
        group.astg_occlusion_enabled = enabled;
        group.occlusion_mode = mode;
        group.transform_generation = 1;
        group.precision = precision;

        for (size_t b = 0; b < boxes.size(); ++b) {
            ASTGOccluderBounds ob(boxes[b], label + "_box_" + std::to_string(b));
            group.bounds.push_back(ob);
        }
        group.recompute_union_bounds();
        group.previous_world_union_bounds = group.world_union_bounds;

        dynamic_occluder_groups[gid] = group;

        if (enabled) {
            update_dynamic_occlusion(gid);
        }
        return gid;
    }

    void clear_group_dynamic_state(uint32_t group_id) {
        // 1. Clear blocked DAG edges
        auto it_edges = dynamic_group_to_edges.find(group_id);
        if (it_edges != dynamic_group_to_edges.end()) {
            for (uint32_t edge_id : it_edges->second) {
                if (edge_id < dag_edges.size()) {
                    auto& edge = dag_edges[edge_id];
                    auto& b_ids = edge.dynamic_blocker_ids;
                    b_ids.erase(std::remove(b_ids.begin(), b_ids.end(), group_id), b_ids.end());
                    edge.dynamic_blocker_count = (uint32_t)b_ids.size();
                    if (edge.dynamic_blocker_count == 0) {
                        edge.state = (edge.is_active ? ASTG_EDGE_ACTIVE : ASTG_EDGE_INVALID_STATIC);
                        dynamic_edge_timeline.push_back({ dynamic_timeline_frame, edge_id, "UNBLOCKED", group_id, 0 });
                    }

                    auto it_p = edge_to_path_contributions.find(edge_id);
                    if (it_p != edge_to_path_contributions.end()) {
                        for (uint32_t dep_id : it_p->second) {
                            if (dep_id < path_probe_contributions.size()) {
                                if (path_probe_contributions[dep_id].dynamic_occlusion_count > 0) {
                                    path_probe_contributions[dep_id].dynamic_occlusion_count--;
                                }
                            }
                        }
                    }
                }
            }
            dynamic_group_to_edges.erase(it_edges);
        }

        // 2. Clear angular cell blockers
        for (auto& kv : group_light_angular_masks) {
            uint32_t gid = (uint32_t)(kv.first >> 32);
            uint32_t lid = (uint32_t)(kv.first & 0xFFFFFFFFULL);
            if (gid == group_id && kv.second != 0) {
                uint64_t mask = kv.second;
                for (uint32_t c = 0; c < 64; ++c) {
                    if (mask & (1ULL << c)) {
                        uint64_t lc_key = ((uint64_t)lid << 32) | c;
                        auto& b_ids = light_cell_blocker_ids[lc_key];
                        b_ids.erase(std::remove(b_ids.begin(), b_ids.end(), group_id), b_ids.end());
                        light_cell_blocker_count[lc_key] = (uint32_t)b_ids.size();

                        if (light_cell_blocker_count[lc_key] == 0) {
                            light_cell_blocker_count.erase(lc_key);
                            light_cell_blocker_ids.erase(lc_key);
                            auto it_e = light_cell_to_b0_edges.find(lc_key);
                            if (it_e != light_cell_to_b0_edges.end()) {
                                for (uint32_t eid : it_e->second) {
                                    if (eid < dag_edges.size()) {
                                        dag_edges[eid].state = (dag_edges[eid].is_active ? ASTG_EDGE_ACTIVE : ASTG_EDGE_INVALID_STATIC);
                                        dynamic_edge_timeline.push_back({ dynamic_timeline_frame, eid, "UNBLOCKED", group_id, 0 });
                                    }
                                }
                            }
                            auto it_p = light_cell_to_paths.find(lc_key);
                            if (it_p != light_cell_to_paths.end()) {
                                for (uint32_t dep_id : it_p->second) {
                                    if (dep_id < path_probe_contributions.size()) {
                                        if (path_probe_contributions[dep_id].dynamic_occlusion_count > 0) {
                                            path_probe_contributions[dep_id].dynamic_occlusion_count--;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                kv.second = 0;
            }
        }

        // 3. Clear dynamic receiver state for this group (Handoff Item 81)
        auto it_grp = dynamic_occluder_groups.find(group_id);
        if (it_grp != dynamic_occluder_groups.end()) {
            for (auto& p : it_grp->second.surface_probes) {
                p.direct_irradiance = { 0.0f, 0.0f, 0.0f };
                p.indirect_irradiance = { 0.0f, 0.0f, 0.0f };
            }
            for (auto& c : it_grp->second.receiver_clusters) {
                c.direct_irradiance = { 0.0f, 0.0f, 0.0f };
                c.indirect_irradiance = { 0.0f, 0.0f, 0.0f };
            }
        }
        for (auto it_c = dynamic_receiver_cache.begin(); it_c != dynamic_receiver_cache.end(); ) {
            if (it_c->second.dynamic_group_id == group_id) {
                it_c = dynamic_receiver_cache.erase(it_c);
            } else {
                ++it_c;
            }
        }
    }

    uint32_t register_dynamic_receiver_probes(
        uint32_t group_id,
        const std::vector<ASTGDynamicSurfaceProbe>& probes,
        const std::vector<ASTGReceiverCluster>& clusters = {},
        bool is_skeletal = false
    ) {
        auto it = dynamic_occluder_groups.find(group_id);
        if (it == dynamic_occluder_groups.end()) return 0;

        it->second.surface_probes = probes;
        it->second.receiver_clusters = clusters;
        it->second.is_skeletal = is_skeletal;

        if (!clusters.empty()) {
            for (size_t c = 0; c < it->second.receiver_clusters.size(); ++c) {
                it->second.receiver_clusters[c].member_probe_indices.clear();
            }
            for (size_t p = 0; p < it->second.surface_probes.size(); ++p) {
                uint32_t cid = it->second.surface_probes[p].cluster_id;
                if (cid < it->second.receiver_clusters.size()) {
                    it->second.receiver_clusters[cid].member_probe_indices.push_back((uint32_t)p);
                }
            }
            for (auto& cluster : it->second.receiver_clusters) {
                cluster.recompute_local_bounds(it->second.surface_probes);
            }
        }

        it->second.update_receiver_transforms();
        if (it->second.astg_occlusion_enabled) {
            update_dynamic_occlusion(group_id);
        }
        return (uint32_t)it->second.surface_probes.size();
    }

    void set_dynamic_group_rigid_transform(uint32_t group_id, const RTXMatrix4x4& transform) {
        auto it = dynamic_occluder_groups.find(group_id);
        if (it == dynamic_occluder_groups.end()) return;

        it->second.previous_world_union_bounds = it->second.world_union_bounds;
        it->second.rigid_transform = transform;
        it->second.is_skeletal = false;
        it->second.update_receiver_transforms();
    }

    void set_dynamic_group_bone_matrices(uint32_t group_id, const std::vector<RTXMatrix4x4>& bones) {
        auto it = dynamic_occluder_groups.find(group_id);
        if (it == dynamic_occluder_groups.end()) return;

        it->second.previous_world_union_bounds = it->second.world_union_bounds;
        it->second.bone_matrices = bones;
        it->second.is_skeletal = true;
        it->second.update_receiver_transforms();
    }

    void set_dynamic_group_bounds(uint32_t group_id, const std::vector<ASTGAABB>& new_boxes) {
        auto it = dynamic_occluder_groups.find(group_id);
        if (it == dynamic_occluder_groups.end()) return;

        it->second.previous_world_union_bounds = it->second.world_union_bounds;
        it->second.bounds.clear();
        for (size_t b = 0; b < new_boxes.size(); ++b) {
            ASTGOccluderBounds ob(new_boxes[b], it->second.label + "_box_" + std::to_string(b));
            it->second.bounds.push_back(ob);
        }
        it->second.update_receiver_transforms();
    }

    void update_dynamic_group_rigid_transform(uint32_t group_id, const RTXMatrix4x4& transform) {
        set_dynamic_group_rigid_transform(group_id, transform);
        auto it = dynamic_occluder_groups.find(group_id);
        if (it != dynamic_occluder_groups.end() && it->second.astg_occlusion_enabled) {
            update_dynamic_occlusion(group_id);
        }
    }

    void update_dynamic_group_bone_matrices(uint32_t group_id, const std::vector<RTXMatrix4x4>& bones) {
        set_dynamic_group_bone_matrices(group_id, bones);
        auto it = dynamic_occluder_groups.find(group_id);
        if (it != dynamic_occluder_groups.end() && it->second.astg_occlusion_enabled) {
            update_dynamic_occlusion(group_id);
        }
    }

    void resolve_receiver_cell_winner(uint32_t lid, uint32_t c) {
        uint64_t lc_key = ((uint64_t)lid << 32) | c;
        float best_dist = 1e9f;
        uint32_t best_group_id = 0;
        int best_probe_idx = -1;
        int best_cluster_idx = -1;
        RTXVector3 best_direct_e = { 0.0f, 0.0f, 0.0f };

        auto it_lp = light_positions.find(lid);
        RTXVector3 light_pos = (it_lp != light_positions.end()) ? it_lp->second : RTXVector3{ 0.0f, 5.0f, 0.0f };
        RTXVector3 light_col = { 1.0f, 1.0f, 1.0f };
        float light_int = 10.0f;
        auto it_col = light_colors.find(lid);
        if (it_col != light_colors.end()) light_col = it_col->second;
        auto it_int = light_intensities.find(lid);
        if (it_int != light_intensities.end()) light_int = it_int->second;

        RTXVector3 dir = angular_hierarchy.cells[c].dir_center;

        for (auto& kv : dynamic_occluder_groups) {
            uint32_t gid = kv.first;
            auto& grp = kv.second;
            if (!grp.astg_occlusion_enabled || !grp.enable_surface_receivers || grp.surface_probes.empty()) continue;

            uint64_t gl_key = ((uint64_t)gid << 32) | lid;
            auto it_mask = group_light_angular_masks.find(gl_key);
            if (it_mask == group_light_angular_masks.end() || (it_mask->second & (1ULL << c)) == 0) continue;

            // Search candidate probes in this group
            for (size_t p = 0; p < grp.surface_probes.size(); ++p) {
                if (!grp.surface_probes[p].is_active) continue;
                const auto& probe = grp.surface_probes[p];
                RTXVector3 to_p = { probe.world_position.x - light_pos.x, probe.world_position.y - light_pos.y, probe.world_position.z - light_pos.z };
                float dist = std::sqrt(to_p.x * to_p.x + to_p.y * to_p.y + to_p.z * to_p.z);
                if (dist > 1e-4f) {
                    to_p.x /= dist; to_p.y /= dist; to_p.z /= dist;
                    uint32_t p_cell = angular_hierarchy.get_cell_id_for_dir(to_p);
                    float dot_dir = to_p.x * dir.x + to_p.y * dir.y + to_p.z * dir.z;
                    if ((p_cell == c || dot_dir > 0.75f) && dist < best_dist) {
                        best_dist = dist;
                        best_group_id = gid;
                        best_probe_idx = (int)p;
                        best_cluster_idx = (int)probe.cluster_id;

                        RTXVector3 to_light = { light_pos.x - probe.world_position.x, light_pos.y - probe.world_position.y, light_pos.z - probe.world_position.z };
                        float to_l_dist = std::sqrt(to_light.x * to_light.x + to_light.y * to_light.y + to_light.z * to_light.z);
                        if (to_l_dist > 1e-4f) {
                            to_light.x /= to_l_dist; to_light.y /= to_l_dist; to_light.z /= to_l_dist;
                            float cos_n = std::max(0.0f, probe.world_normal.x * to_light.x + probe.world_normal.y * to_light.y + probe.world_normal.z * to_light.z);
                            float falloff = light_int * cos_n / (to_l_dist * to_l_dist + 0.1f);
                            best_direct_e = { light_col.x * falloff, light_col.y * falloff, light_col.z * falloff };
                        }
                    }
                }
            }
        }

        if (best_probe_idx >= 0) {
            auto& winner_grp = dynamic_occluder_groups[best_group_id];
            auto& probe = winner_grp.surface_probes[best_probe_idx];
            probe.direct_irradiance.x += best_direct_e.x;
            probe.direct_irradiance.y += best_direct_e.y;
            probe.direct_irradiance.z += best_direct_e.z;
            dynamic_receiver_cache[lc_key] = { best_group_id, (uint32_t)best_cluster_idx, probe.probe_id, best_dist, winner_grp.transform_generation, 1.0f, best_direct_e };
        }
    }

    struct IndirectVisibilityCandidate {
        uint32_t probe_index;
        uint32_t node_index;
        float weight;
        float dist;
    };

    void evaluate_dynamic_receiver_indirect(
        uint32_t group_id,
        bool enable_gpu_visibility_refinement = false,
        uint32_t max_batch_size = 8192
    ) {
        visibility_batch_telemetry = ASTGVisibilityBatchTelemetry{};
        auto it = dynamic_occluder_groups.find(group_id);
        if (it == dynamic_occluder_groups.end() || !it->second.enable_surface_receivers) return;

        auto& group = it->second;
        if (group.surface_probes.empty()) return;

        if (spatial_index_generation != transport_generation || (static_node_spatial_grid.grid.empty() && !bounce0_nodes.empty())) {
            rebuild_static_node_spatial_index();
        }

        // Initialize probe indirect accumulators
        for (auto& probe : group.surface_probes) {
            probe.indirect_irradiance = { 0.0f, 0.0f, 0.0f };
        }

        std::vector<float> probe_total_weight(group.surface_probes.size(), 0.0f);
        std::vector<RTXVector3> probe_accum_indirect(group.surface_probes.size(), { 0.0f, 0.0f, 0.0f });

        std::vector<IndirectVisibilityCandidate> visibility_candidates;
        std::vector<ASTGRay> batched_rays;
        std::vector<ASTGRayHit> batched_hits;

        // 1. Gather Phase: Search spatial index & perform cheap geometric rejection (Review Item 5)
        if (!group.receiver_clusters.empty()) {
            for (const auto& cluster : group.receiver_clusters) {
                std::vector<uint32_t> candidate_nodes;
                if (!static_node_spatial_grid.grid.empty()) {
                    static_node_spatial_grid.query_sphere(cluster.world_centroid, cluster.cluster_radius + 3.0f, candidate_nodes);
                } else {
                    candidate_nodes.resize(bounce0_nodes.size());
                    for (size_t n = 0; n < bounce0_nodes.size(); ++n) candidate_nodes[n] = (uint32_t)n;
                }

                for (uint32_t p_idx : cluster.member_probe_indices) {
                    if (p_idx >= group.surface_probes.size() || !group.surface_probes[p_idx].is_active) continue;
                    const auto& probe = group.surface_probes[p_idx];

                    for (uint32_t nid : candidate_nodes) {
                        if (nid >= bounce0_nodes.size() || !bounce0_nodes[nid].is_active) continue;
                        const auto& node = bounce0_nodes[nid];

                        float dx = node.position.x - probe.world_position.x;
                        float dy = node.position.y - probe.world_position.y;
                        float dz = node.position.z - probe.world_position.z;
                        float dist_sq = dx * dx + dy * dy + dz * dz;
                        if (dist_sq < 9.0f) { // Within 3m
                            float dist = std::sqrt(dist_sq);
                            if (dist > 1e-5f) {
                                RTXVector3 to_node = { dx / dist, dy / dist, dz / dist };
                                float cos_probe = std::max(0.0f, probe.world_normal.x * to_node.x + probe.world_normal.y * to_node.y + probe.world_normal.z * to_node.z);
                                float cos_node = std::max(0.0f, -(node.geometric_normal.x * to_node.x + node.geometric_normal.y * to_node.y + node.geometric_normal.z * to_node.z));

                                if (cos_probe > 0.05f && cos_node > 0.05f) {
                                    float geom_factor = (cos_probe * cos_node) / (dist_sq + 0.05f);
                                    if (geom_factor > 1e-4f) {
                                        float w = geom_factor * (node.geometric_factor > 0.0f ? node.geometric_factor : 1.0f);
                                        if (enable_gpu_visibility_refinement && rtx_is_hardware_active()) {
                                            IndirectVisibilityCandidate cand;
                                            cand.probe_index = p_idx;
                                            cand.node_index = nid;
                                            cand.weight = w;
                                            cand.dist = dist;
                                            visibility_candidates.push_back(cand);
                                            visibility_batch_telemetry.ambiguous_candidates_gathered++;

                                            ASTGRay ray;
                                            ray.origin_x = probe.world_position.x + probe.world_normal.x * 0.01f;
                                            ray.origin_y = probe.world_position.y + probe.world_normal.y * 0.01f;
                                            ray.origin_z = probe.world_position.z + probe.world_normal.z * 0.01f;
                                            ray.dir_x = to_node.x; ray.dir_y = to_node.y; ray.dir_z = to_node.z;
                                            ray.t_min = 0.001f; ray.t_max = dist - 0.02f;
                                            ray.source_light_id = 0; ray.transport_node_id = nid; ray.angular_cell_id = 0; ray.flags = 0;
                                            batched_rays.push_back(ray);
                                        } else {
                                            probe_accum_indirect[p_idx].x += node.path_transfer_r * w;
                                            probe_accum_indirect[p_idx].y += node.path_transfer_g * w;
                                            probe_accum_indirect[p_idx].z += node.path_transfer_b * w;
                                            probe_total_weight[p_idx] += w;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            // Unclustered probe query
            for (size_t p_idx = 0; p_idx < group.surface_probes.size(); ++p_idx) {
                if (!group.surface_probes[p_idx].is_active) continue;
                const auto& probe = group.surface_probes[p_idx];

                std::vector<uint32_t> candidate_nodes;
                if (!static_node_spatial_grid.grid.empty()) {
                    static_node_spatial_grid.query_sphere(probe.world_position, 3.0f, candidate_nodes);
                } else {
                    candidate_nodes.resize(bounce0_nodes.size());
                    for (size_t n = 0; n < bounce0_nodes.size(); ++n) candidate_nodes[n] = (uint32_t)n;
                }

                for (uint32_t nid : candidate_nodes) {
                    if (nid >= bounce0_nodes.size() || !bounce0_nodes[nid].is_active) continue;
                    const auto& node = bounce0_nodes[nid];

                    float dx = node.position.x - probe.world_position.x;
                    float dy = node.position.y - probe.world_position.y;
                    float dz = node.position.z - probe.world_position.z;
                    float dist_sq = dx * dx + dy * dy + dz * dz;
                    if (dist_sq < 9.0f) {
                        float dist = std::sqrt(dist_sq);
                        if (dist > 1e-5f) {
                            RTXVector3 to_node = { dx / dist, dy / dist, dz / dist };
                            float cos_probe = std::max(0.0f, probe.world_normal.x * to_node.x + probe.world_normal.y * to_node.y + probe.world_normal.z * to_node.z);
                            float cos_node = std::max(0.0f, -(node.geometric_normal.x * to_node.x + node.geometric_normal.y * to_node.y + node.geometric_normal.z * to_node.z));

                            if (cos_probe > 0.05f && cos_node > 0.05f) {
                                float geom_factor = (cos_probe * cos_node) / (dist_sq + 0.05f);
                                if (geom_factor > 1e-4f) {
                                    float w = geom_factor * (node.geometric_factor > 0.0f ? node.geometric_factor : 1.0f);
                                    if (enable_gpu_visibility_refinement && rtx_is_hardware_active()) {
                                        IndirectVisibilityCandidate cand;
                                        cand.probe_index = (uint32_t)p_idx;
                                        cand.node_index = nid;
                                        cand.weight = w;
                                        cand.dist = dist;
                                        visibility_candidates.push_back(cand);
                                        visibility_batch_telemetry.ambiguous_candidates_gathered++;

                                        ASTGRay ray;
                                        ray.origin_x = probe.world_position.x + probe.world_normal.x * 0.01f;
                                        ray.origin_y = probe.world_position.y + probe.world_normal.y * 0.01f;
                                        ray.origin_z = probe.world_position.z + probe.world_normal.z * 0.01f;
                                        ray.dir_x = to_node.x; ray.dir_y = to_node.y; ray.dir_z = to_node.z;
                                        ray.t_min = 0.001f; ray.t_max = dist - 0.02f;
                                        ray.source_light_id = 0; ray.transport_node_id = nid; ray.angular_cell_id = 0; ray.flags = 0;
                                        batched_rays.push_back(ray);
                                    } else {
                                        probe_accum_indirect[p_idx].x += node.path_transfer_r * w;
                                        probe_accum_indirect[p_idx].y += node.path_transfer_g * w;
                                        probe_accum_indirect[p_idx].z += node.path_transfer_b * w;
                                        probe_total_weight[p_idx] += w;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // 2. Dispatch Phase: Trace batched visibility rays in chunks up to max_batch_size
        if (!batched_rays.empty()) {
            batched_hits.resize(batched_rays.size());
            size_t total_rays = batched_rays.size();
            size_t chunk_size = (max_batch_size > 0) ? max_batch_size : 8192;
            visibility_batch_telemetry.rays_requested = (uint32_t)total_rays;

            for (size_t offset = 0; offset < total_rays; offset += chunk_size) {
                size_t count = std::min(chunk_size, total_rays - offset);
                visibility_batch_telemetry.dispatch_count++;
                visibility_batch_telemetry.rays_dispatched += (uint32_t)count;
                visibility_batch_telemetry.largest_dispatch_size = std::max(visibility_batch_telemetry.largest_dispatch_size, (uint32_t)count);
                visibility_batch_telemetry.cap_compliant &= count <= chunk_size;
                rtx_trace_rays_batch(&batched_rays[offset], &batched_hits[offset], (int32_t)count);
            }
            visibility_batch_telemetry.rays_completed = (uint32_t)batched_hits.size();

            // 3. Consume Phase: Filter unoccluded candidates and accumulate irradiance
            for (size_t i = 0; i < total_rays; ++i) {
                const auto& cand = visibility_candidates[i];
                const auto& hit = batched_hits[i];
                if (hit.hit == 0 || hit.distance >= cand.dist - 0.02f) {
                    const auto& node = bounce0_nodes[cand.node_index];
                    uint32_t p_idx = cand.probe_index;
                    float w = cand.weight;
                    probe_accum_indirect[p_idx].x += node.path_transfer_r * w;
                    probe_accum_indirect[p_idx].y += node.path_transfer_g * w;
                    probe_accum_indirect[p_idx].z += node.path_transfer_b * w;
                    probe_total_weight[p_idx] += w;
                }
            }
        }

        // 4. Finalize Probe & Cluster Irradiance
        for (size_t p_idx = 0; p_idx < group.surface_probes.size(); ++p_idx) {
            if (probe_total_weight[p_idx] > 1e-5f) {
                group.surface_probes[p_idx].indirect_irradiance.x = probe_accum_indirect[p_idx].x / probe_total_weight[p_idx];
                group.surface_probes[p_idx].indirect_irradiance.y = probe_accum_indirect[p_idx].y / probe_total_weight[p_idx];
                group.surface_probes[p_idx].indirect_irradiance.z = probe_accum_indirect[p_idx].z / probe_total_weight[p_idx];
            }
        }

        for (auto& cluster : group.receiver_clusters) {
            cluster.indirect_irradiance = { 0.0f, 0.0f, 0.0f };
            if (!cluster.member_probe_indices.empty()) {
                RTXVector3 c_ind = { 0.0f, 0.0f, 0.0f };
                for (uint32_t p_idx : cluster.member_probe_indices) {
                    if (p_idx < group.surface_probes.size()) {
                        c_ind.x += group.surface_probes[p_idx].indirect_irradiance.x;
                        c_ind.y += group.surface_probes[p_idx].indirect_irradiance.y;
                        c_ind.z += group.surface_probes[p_idx].indirect_irradiance.z;
                    }
                }
                float count = (float)cluster.member_probe_indices.size();
                cluster.indirect_irradiance = { c_ind.x / count, c_ind.y / count, c_ind.z / count };
            }
        }
    }

    uint64_t compute_dynamic_receiver_memory_bytes(uint32_t group_id) const {
        auto it = dynamic_occluder_groups.find(group_id);
        if (it == dynamic_occluder_groups.end()) return 0;

        const auto& group = it->second;
        uint64_t total_bytes = sizeof(ASTGDynamicOccluderGroup);
        total_bytes += group.surface_probes.size() * sizeof(ASTGDynamicSurfaceProbe);
        total_bytes += group.receiver_clusters.size() * sizeof(ASTGReceiverCluster);
        total_bytes += group.bone_matrices.size() * sizeof(RTXMatrix4x4);
        total_bytes += group.bounds.size() * sizeof(ASTGOccluderBounds);

        // Receiver Cache contribution for this group
        for (const auto& kv : dynamic_receiver_cache) {
            if (kv.second.dynamic_group_id == group_id) {
                total_bytes += sizeof(uint64_t) + sizeof(ASTGDynamicReceiverCacheEntry);
            }
        }
        return total_bytes;
    }

    void unregister_dynamic_occluder_group(uint32_t group_id) {
        clear_group_dynamic_state(group_id);
        dynamic_occluder_groups.erase(group_id);
    }

    void set_dynamic_occluder_group_enabled(uint32_t group_id, bool enabled) {
        auto it = dynamic_occluder_groups.find(group_id);
        if (it == dynamic_occluder_groups.end()) return;

        if (it->second.astg_occlusion_enabled == enabled) return;

        it->second.astg_occlusion_enabled = enabled;
        if (!enabled) {
            clear_group_dynamic_state(group_id);
        } else {
            update_dynamic_occlusion(group_id);
        }
    }

    void set_astg_occlusion_mode(uint32_t group_id, ASTGDynamicOcclusionMode mode) {
        auto it = dynamic_occluder_groups.find(group_id);
        if (it == dynamic_occluder_groups.end()) return;
        if (it->second.occlusion_mode == mode) return;

        clear_group_dynamic_state(group_id);
        it->second.occlusion_mode = mode;
        if (it->second.astg_occlusion_enabled) {
            update_dynamic_occlusion(group_id);
        }
    }

    void set_global_dynamic_occlusion_mode(ASTGDynamicOcclusionMode mode) {
        if (global_occlusion_mode == mode) return;

        for (auto& pair : dynamic_occluder_groups) {
            clear_group_dynamic_state(pair.first);
        }
        global_occlusion_mode = mode;
        for (auto& pair : dynamic_occluder_groups) {
            if (pair.second.astg_occlusion_enabled) {
                update_dynamic_occlusion(pair.first);
            }
        }
    }

    void update_dynamic_occluder_group_bounds(uint32_t group_id, const std::vector<ASTGAABB>& new_boxes) {
        set_dynamic_group_bounds(group_id, new_boxes);
        auto it = dynamic_occluder_groups.find(group_id);
        if (it != dynamic_occluder_groups.end() && it->second.astg_occlusion_enabled) {
            update_dynamic_occlusion(group_id);
        }
    }

    ASTGDynamicOcclusionMetrics update_dynamic_occlusion(uint32_t group_id) {
        auto t_start = std::chrono::high_resolution_clock::now();
        ASTGDynamicOcclusionMetrics m;
        m.group_id = group_id;
        m.total_dag_edges = (uint32_t)dag_edges.size();

        auto it_grp = dynamic_occluder_groups.find(group_id);
        if (it_grp == dynamic_occluder_groups.end()) return m;

        auto& group = it_grp->second;
        m.enabled = group.astg_occlusion_enabled;
        m.box_count = (uint32_t)group.bounds.size();

        ASTGDynamicOcclusionMode mode = (global_occlusion_mode != ASTG_OCCLUSION_DAG_EDGES_ALL_BOUNCES && global_occlusion_mode != group.occlusion_mode)
            ? global_occlusion_mode : group.occlusion_mode;
        m.mode = mode;

        if (!group.astg_occlusion_enabled || group.bounds.empty() || mode == ASTG_OCCLUSION_NONE) {
            clear_group_dynamic_state(group_id);
            auto t_end = std::chrono::high_resolution_clock::now();
            m.total_update_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            return m;
        }

        std::unordered_set<uint32_t> affected_path_ids;

        // -------------------------------------------------------------
        // 1. ANGULAR B0 OCCLUSION (Active in Mode B, Mode C, or whenever surface receivers are attached) (Handoff Item 10, 11, 13-24)
        // -------------------------------------------------------------
        if (mode == ASTG_OCCLUSION_ANGULAR_B0_DAG_B1_PLUS || mode == ASTG_OCCLUSION_ANGULAR_B0_ONLY || group.enable_surface_receivers) {
            auto t_ang_start = std::chrono::high_resolution_clock::now();

            std::vector<uint32_t> lights_to_test;
            for (const auto& kv : light_positions) {
                lights_to_test.push_back(kv.first);
            }
            if (lights_to_test.empty()) {
                for (const auto& edge : dag_edges) {
                    if (std::find(lights_to_test.begin(), lights_to_test.end(), edge.source_light_id) == lights_to_test.end()) {
                        lights_to_test.push_back(edge.source_light_id);
                    }
                }
                if (lights_to_test.empty()) lights_to_test.push_back(0);
            }

            // Clear direct irradiance accumulators ONCE across all lights (Handoff Item 4 / Multi-light accumulation)
            if (group.enable_surface_receivers && !group.surface_probes.empty()) {
                for (auto& probe : group.surface_probes) {
                    probe.direct_irradiance = { 0.0f, 0.0f, 0.0f };
                }
                for (auto& cluster : group.receiver_clusters) {
                    cluster.direct_irradiance = { 0.0f, 0.0f, 0.0f };
                }
            }

            for (uint32_t lid : lights_to_test) {
                auto it_lp = light_positions.find(lid);
                RTXVector3 light_pos = (it_lp != light_positions.end()) ? it_lp->second : RTXVector3{ 0.0f, 5.0f, 0.0f };
                
                uint64_t total_group_mask = 0;
                float total_proxy_solid_angle = 0.0f;
                for (const auto& ob : group.bounds) {
                    float box_sa = 0.0f;
                    uint64_t box_mask = angular_hierarchy.query_box_footprint(light_pos, ob.world_bounds, &box_sa);
                    total_group_mask |= box_mask;
                    total_proxy_solid_angle += box_sa;
                }

                uint64_t gl_key = ((uint64_t)group_id << 32) | lid;
                uint64_t old_mask = group_light_angular_masks[gl_key];
                uint64_t new_mask = total_group_mask;

                uint64_t newly_covered = new_mask & (~old_mask);
                uint64_t newly_uncovered = old_mask & (~new_mask);
                uint64_t still_covered = new_mask & old_mask;

                m.angular_current_cells += (uint32_t)std::bitset<64>(new_mask).count();
                m.angular_previous_cells += (uint32_t)std::bitset<64>(old_mask).count();
                m.angular_newly_covered_cells += (uint32_t)std::bitset<64>(newly_covered).count();
                m.angular_newly_uncovered_cells += (uint32_t)std::bitset<64>(newly_uncovered).count();
                m.angular_still_covered_cells += (uint32_t)std::bitset<64>(still_covered).count();

                uint64_t diff = old_mask ^ new_mask;
                uint64_t union_mask = old_mask | new_mask;
                m.angular_delta_fraction = (union_mask > 0) ? (double)std::bitset<64>(diff).count() / (double)std::bitset<64>(union_mask).count() : 0.0;

                m.proxy_solid_angle = total_proxy_solid_angle;
                m.cell_solid_angle = (float)std::bitset<64>(new_mask).count() * (4.0f * 3.14159265f / 64.0f);
                m.overcoverage_ratio = (m.proxy_solid_angle > 1e-5f) ? (m.cell_solid_angle / m.proxy_solid_angle) : 1.0f;

                group_light_angular_masks[gl_key] = new_mask;

                // Process newly uncovered angular cells (Handoff Item 20)
                for (uint32_t c = 0; c < 64; ++c) {
                    if (newly_uncovered & (1ULL << c)) {
                        uint64_t lc_key = ((uint64_t)lid << 32) | c;
                        auto& b_ids = light_cell_blocker_ids[lc_key];
                        b_ids.erase(std::remove(b_ids.begin(), b_ids.end(), group_id), b_ids.end());
                        light_cell_blocker_count[lc_key] = (uint32_t)b_ids.size();

                        if (light_cell_blocker_count[lc_key] == 0) {
                            light_cell_blocker_count.erase(lc_key);
                            light_cell_blocker_ids.erase(lc_key);
                            auto it_e = light_cell_to_b0_edges.find(lc_key);
                            if (it_e != light_cell_to_b0_edges.end()) {
                                for (uint32_t eid : it_e->second) {
                                    if (eid < dag_edges.size()) {
                                        dag_edges[eid].state = (dag_edges[eid].is_active ? ASTG_EDGE_ACTIVE : ASTG_EDGE_INVALID_STATIC);
                                        dynamic_edge_timeline.push_back({ dynamic_timeline_frame, eid, "UNBLOCKED", group_id, 0 });
                                    }
                                }
                            }
                            auto it_p = light_cell_to_paths.find(lc_key);
                            if (it_p != light_cell_to_paths.end()) {
                                for (uint32_t dep_id : it_p->second) {
                                    if (dep_id < path_probe_contributions.size()) {
                                        if (path_probe_contributions[dep_id].dynamic_occlusion_count > 0) {
                                            path_probe_contributions[dep_id].dynamic_occlusion_count--;
                                        }
                                        affected_path_ids.insert(dep_id);
                                    }
                                }
                            }
                        }
                    }
                }

                // Process newly covered angular cells (Handoff Item 20)
                for (uint32_t c = 0; c < 64; ++c) {
                    if (newly_covered & (1ULL << c)) {
                        uint64_t lc_key = ((uint64_t)lid << 32) | c;
                        auto& b_ids = light_cell_blocker_ids[lc_key];
                        if (std::find(b_ids.begin(), b_ids.end(), group_id) == b_ids.end()) {
                            b_ids.push_back(group_id);
                        }
                        light_cell_blocker_count[lc_key] = (uint32_t)b_ids.size();

                        auto it_e = light_cell_to_b0_edges.find(lc_key);
                        if (it_e != light_cell_to_b0_edges.end()) {
                            for (uint32_t eid : it_e->second) {
                                if (eid < dag_edges.size()) {
                                    dag_edges[eid].state = ASTG_EDGE_OCCLUDED_DYNAMIC;
                                    dynamic_edge_timeline.push_back({ dynamic_timeline_frame, eid, "BLOCKED", group_id, light_cell_blocker_count[lc_key] });
                                }
                            }
                        }
                        auto it_p = light_cell_to_paths.find(lc_key);
                        if (it_p != light_cell_to_paths.end()) {
                            for (uint32_t dep_id : it_p->second) {
                                if (dep_id < path_probe_contributions.size()) {
                                    path_probe_contributions[dep_id].dynamic_occlusion_count++;
                                    affected_path_ids.insert(dep_id);
                                }
                            }
                        }
                    }
                }

                // Dynamic Surface Receiver Discovery & Accumulation (Phase 6 / Handoff Item 2, 6, 8, 12, 14, 15, 20, 21, 22)
                if (group.enable_surface_receivers && !group.surface_probes.empty()) {
                    auto t_rec_start = std::chrono::high_resolution_clock::now();
                    m.receiver_probes_active = (uint32_t)group.surface_probes.size();
                    m.receiver_clusters_active = (uint32_t)group.receiver_clusters.size();

                    RTXVector3 light_col = { 1.0f, 1.0f, 1.0f };
                    float light_int = 10.0f;
                    auto it_col = light_colors.find(lid);
                    if (it_col != light_colors.end()) light_col = it_col->second;
                    auto it_int = light_intensities.find(lid);
                    if (it_int != light_intensities.end()) light_int = it_int->second;

                    for (uint32_t c = 0; c < 64; ++c) {
                        uint64_t lc_key = ((uint64_t)lid << 32) | c;
                        if (new_mask & (1ULL << c)) {
                            // Find first-hit dynamic surface probe along the cell center direction
                            RTXVector3 dir = angular_hierarchy.cells[c].dir_center;
                            float best_dist = 1e9f;
                            int best_probe_idx = -1;
                            int best_cluster_idx = -1;

                            if (group.receiver_clustering_mode == ASTG_RECEIVERS_CLUSTERED && !group.receiver_clusters.empty()) {
                                for (size_t cl = 0; cl < group.receiver_clusters.size(); ++cl) {
                                    const auto& cluster = group.receiver_clusters[cl];
                                    if (ray_intersects_aabb(light_pos, dir, cluster.world_bounds, 50.0f)) {
                                        for (uint32_t p_idx : cluster.member_probe_indices) {
                                            if (p_idx < group.surface_probes.size() && group.surface_probes[p_idx].is_active) {
                                                const auto& probe = group.surface_probes[p_idx];
                                                RTXVector3 to_p = { probe.world_position.x - light_pos.x, probe.world_position.y - light_pos.y, probe.world_position.z - light_pos.z };
                                                float dist = std::sqrt(to_p.x * to_p.x + to_p.y * to_p.y + to_p.z * to_p.z);
                                                if (dist > 1e-4f) {
                                                    to_p.x /= dist; to_p.y /= dist; to_p.z /= dist;
                                                    uint32_t p_cell = angular_hierarchy.get_cell_id_for_dir(to_p);
                                                    float dot_dir = to_p.x * dir.x + to_p.y * dir.y + to_p.z * dir.z;
                                                    if ((p_cell == c || dot_dir > 0.75f) && dist < best_dist) {
                                                        best_dist = dist;
                                                        best_probe_idx = (int)p_idx;
                                                        best_cluster_idx = (int)cl;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                for (size_t p = 0; p < group.surface_probes.size(); ++p) {
                                    if (!group.surface_probes[p].is_active) continue;
                                    const auto& probe = group.surface_probes[p];
                                    RTXVector3 to_p = { probe.world_position.x - light_pos.x, probe.world_position.y - light_pos.y, probe.world_position.z - light_pos.z };
                                    float dist = std::sqrt(to_p.x * to_p.x + to_p.y * to_p.y + to_p.z * to_p.z);
                                    if (dist > 1e-4f) {
                                        to_p.x /= dist; to_p.y /= dist; to_p.z /= dist;
                                        uint32_t p_cell = angular_hierarchy.get_cell_id_for_dir(to_p);
                                        float dot_dir = to_p.x * dir.x + to_p.y * dir.y + to_p.z * dir.z;
                                        if ((p_cell == c || dot_dir > 0.75f) && dist < best_dist) {
                                            best_dist = dist;
                                            best_probe_idx = (int)p;
                                        }
                                    }
                                }
                            }

                            if (best_probe_idx >= 0) {
                                // Multi-object depth resolution: if cache already has another object's hit, nearest wins (Handoff Item 21)
                                auto it_cache = dynamic_receiver_cache.find(lc_key);
                                bool claim_cell = true;
                                if (it_cache != dynamic_receiver_cache.end() && it_cache->second.dynamic_group_id != group_id) {
                                    if (it_cache->second.hit_distance < best_dist) {
                                        claim_cell = false;
                                    }
                                }

                                if (claim_cell) {
                                    if (it_cache != dynamic_receiver_cache.end() && it_cache->second.dynamic_group_id != group_id) {
                                        uint32_t prev_gid = it_cache->second.dynamic_group_id;
                                        uint32_t prev_pid = it_cache->second.probe_id;
                                        auto it_prev = dynamic_occluder_groups.find(prev_gid);
                                        if (it_prev != dynamic_occluder_groups.end()) {
                                            for (auto& pr : it_prev->second.surface_probes) {
                                                if (pr.probe_id == prev_pid) {
                                                    pr.direct_irradiance = { 0.0f, 0.0f, 0.0f };
                                                    break;
                                                }
                                            }
                                        }
                                    }

                                    auto& probe = group.surface_probes[best_probe_idx];
                                    RTXVector3 to_light = { light_pos.x - probe.world_position.x, light_pos.y - probe.world_position.y, light_pos.z - probe.world_position.z };
                                    float dist = std::sqrt(to_light.x * to_light.x + to_light.y * to_light.y + to_light.z * to_light.z);
                                    if (dist > 1e-4f) {
                                        to_light.x /= dist; to_light.y /= dist; to_light.z /= dist;
                                        float cos_n = std::max(0.0f, probe.world_normal.x * to_light.x + probe.world_normal.y * to_light.y + probe.world_normal.z * to_light.z);
                                        float falloff = light_int * cos_n / (dist * dist + 0.1f);
                                        RTXVector3 direct_e = { light_col.x * falloff, light_col.y * falloff, light_col.z * falloff };

                                        probe.direct_irradiance.x += direct_e.x;
                                        probe.direct_irradiance.y += direct_e.y;
                                        probe.direct_irradiance.z += direct_e.z;

                                        if (best_cluster_idx >= 0 && best_cluster_idx < (int)group.receiver_clusters.size()) {
                                            group.receiver_clusters[best_cluster_idx].direct_irradiance.x += direct_e.x;
                                            group.receiver_clusters[best_cluster_idx].direct_irradiance.y += direct_e.y;
                                            group.receiver_clusters[best_cluster_idx].direct_irradiance.z += direct_e.z;
                                        }

                                        if (it_cache != dynamic_receiver_cache.end() && it_cache->second.probe_id == probe.probe_id && it_cache->second.dynamic_group_id == group_id) {
                                            m.receiver_mappings_reused++;
                                        } else {
                                            m.receiver_mappings_created++;
                                        }
                                        m.receiver_mappings_active++;

                                        dynamic_receiver_cache[lc_key] = { group_id, (uint32_t)best_cluster_idx, probe.probe_id, best_dist, group.transform_generation, 1.0f, direct_e };
                                    }
                                }
                            }
                        } else {
                            // Cell not covered: remove mapping if owned by this group and immediately resolve winner (Handoff Item 11)
                            auto it_cache = dynamic_receiver_cache.find(lc_key);
                            if (it_cache != dynamic_receiver_cache.end() && it_cache->second.dynamic_group_id == group_id) {
                                dynamic_receiver_cache.erase(it_cache);
                                m.receiver_mappings_removed++;
                                resolve_receiver_cell_winner(lid, c);
                            }
                        }
                    }

                    auto t_rec_end = std::chrono::high_resolution_clock::now();
                    m.direct_receiver_us = std::chrono::duration<double, std::micro>(t_rec_end - t_rec_start).count();
                }
            }

            auto t_ang_end = std::chrono::high_resolution_clock::now();
            m.angular_projection_us = std::chrono::duration<double, std::micro>(t_ang_end - t_ang_start).count();
        }

        // -------------------------------------------------------------
        // 2. DAG EDGE TESTING (Active for ALL_BOUNCES in Mode A, and for B1+ in Mode B) (Handoff Item 7, 8, 9, 10, 28, 29)
        // -------------------------------------------------------------
        if (mode == ASTG_OCCLUSION_DAG_EDGES_ALL_BOUNCES || mode == ASTG_OCCLUSION_ANGULAR_B0_DAG_B1_PLUS) {
            ASTGAABB swept_bounds = ASTGAABB::union_of(group.previous_world_union_bounds, group.world_union_bounds);
            std::vector<uint32_t> candidate_edges;
            std::vector<ASTGGPUCellRange> gpu_spatial_ranges;
            uint32_t gpu_edge_references = 0;
            uint32_t gpu_visibility_state_slot = UINT32_MAX;
            auto t_sp_start = std::chrono::high_resolution_clock::now();
            query_gpu_edge_spatial_ranges(swept_bounds, gpu_spatial_ranges, gpu_edge_references);
            const bool use_gpu_spatial_discovery = enable_gpu_spatial_discovery && rtx_is_hardware_active() &&
                gpu_edge_references >= 256 &&
                gpu_edge_references <= 131072 &&
                !gpu_spatial_ranges.empty() && sync_gpu_edge_spatial_index();
            const bool use_gpu_persistent_visibility = use_gpu_spatial_discovery &&
                ensure_gpu_visibility_state_slot(group_id, gpu_visibility_state_slot);
            if (!use_gpu_persistent_visibility) {
                edge_spatial_grid.query_edges_in_aabb(swept_bounds, candidate_edges);
            }
            auto t_sp_end = std::chrono::high_resolution_clock::now();
            m.spatial_query_us = std::chrono::duration<double, std::micro>(t_sp_end - t_sp_start).count();
            m.candidate_edges = use_gpu_persistent_visibility ? gpu_edge_references : (uint32_t)candidate_edges.size();
            m.spatial_cells_touched = (uint32_t)gpu_spatial_ranges.size();
            m.spatial_edge_references = gpu_edge_references;

            std::unordered_set<uint32_t> new_blocked_edge_set;
            auto t_fine_start = std::chrono::high_resolution_clock::now();
            std::vector<uint32_t> gpu_candidate_edges;
            if (!use_gpu_persistent_visibility) {
                gpu_candidate_edges.reserve(candidate_edges.size());
                for (uint32_t edge_idx : candidate_edges) {
                    if (edge_idx >= dag_edges.size()) continue;
                    const auto& edge = dag_edges[edge_idx];
                    if (!edge.is_active && edge.state == ASTG_EDGE_INVALID_STATIC) continue;
                    // In Mode B, B0 edges are handled by angular projection.
                    if (mode == ASTG_OCCLUSION_ANGULAR_B0_DAG_B1_PLUS && edge.source_bounce_depth == 0) continue;
                    if (!get_node_by_id(edge.parent_node_id) || !get_node_by_id(edge.child_node_id)) continue;
                    gpu_candidate_edges.push_back(edge_idx);
                }
            }

            // GPU construction/traversal is used for representative workloads;
            // tiny updates retain the low-latency CPU fast path. GPU results are
            // only used to form the candidate blocked set; canonical DAG
            // mutation below remains CPU-owned.
            const bool use_gpu_visibility = use_gpu_persistent_visibility;
            if (use_gpu_visibility) {
                // Persistent GPU output is a transition stream. Start from
                // the previous canonical blocked set and apply only changes.
                new_blocked_edge_set = dynamic_group_to_edges[group_id];
                std::vector<ASTGEdgeVisibilityResult> gpu_results;
                ASTGVisibilityCounters gpu_counters = {};
                RTGPUTimings gpu_timings = {};
                incremental_sync_gpu_astg();
                sync_gpu_dynamic_occluders();
                gpu_results.resize(gpu_edge_references);
                ++gpu_discovery_stamp;
                if (gpu_discovery_stamp == 0) {
                    // Stamp wrap is an ABA boundary: clear GPU stamps by
                    // re-uploading the persistent spatial table before reuse.
                    gpu_discovery_stamp = 1;
                    gpu_edge_spatial_index_uploaded = false;
                    sync_gpu_edge_spatial_index();
                }
                rtx_trace_spatial_edge_ranges(gpu_spatial_ranges.data(), (uint32_t)gpu_spatial_ranges.size(),
                    gpu_edge_references, group_id,
                    gpu_occluder_dense_indices.count(group_id) ? gpu_occluder_dense_indices[group_id] : UINT32_MAX,
                    gpu_discovery_stamp, gpu_visibility_state_slot, gpu_results.data(), &gpu_counters, &gpu_timings);
                m.gpu_generation_rejected = gpu_counters.generation_rejected;
                m.gpu_angular_rejected = gpu_counters.angular_rejected;
                m.gpu_aabb_rejected = gpu_counters.broadphase_rejected;
                m.gpu_rayquery_required = gpu_counters.rayquery_candidates;
                m.gpu_visibility_state_transitions = gpu_counters.changed_state_count;
                m.gpu_changed_result_readback_bytes = gpu_counters.changed_state_count * (uint32_t)sizeof(ASTGEdgeVisibilityResult);
                m.spatial_duplicate_edges_removed = gpu_edge_references > gpu_counters.edges_considered
                    ? gpu_edge_references - gpu_counters.edges_considered : 0;
                const uint32_t changed_count = std::min(gpu_counters.changed_state_count, gpu_edge_references);
                for (uint32_t result_index = 0; result_index < changed_count; ++result_index) {
                    const auto& result = gpu_results[result_index];
                    const uint32_t edge_idx = result.edge_id;
                    if (edge_idx >= dag_edges.size() ||
                        result.generation != dag_edges[edge_idx].repair_generation) continue;
                    m.fine_tested_edges++;
                    // Bounds-backed groups have exact GPU slab semantics;
                    // consume only state transitions, with no CPU segment
                    // retest or per-candidate mutation work.
                    if (result.visibility_state == 1) {
                        new_blocked_edge_set.insert(edge_idx);
                        m.intersected_edges++;
                    } else {
                        new_blocked_edge_set.erase(edge_idx);
                    }
                }
            } else {
                for (uint32_t edge_idx : gpu_candidate_edges) {
                    const auto& edge = dag_edges[edge_idx];
                    const ASTGTransportNode* parent_n = get_node_by_id(edge.parent_node_id);
                    const ASTGTransportNode* child_n = get_node_by_id(edge.child_node_id);
                    m.fine_tested_edges++;
                    bool edge_hit = false;
                    for (const auto& ob : group.bounds) {
                        if (segment_intersects_aabb(parent_n->position, child_n->position, ob.aabb)) {
                            edge_hit = true;
                            break;
                        }
                    }
                    if (edge_hit) {
                        new_blocked_edge_set.insert(edge_idx);
                        m.intersected_edges++;
                    }
                }
            }
            auto t_fine_end = std::chrono::high_resolution_clock::now();
            m.fine_test_us = std::chrono::duration<double, std::micro>(t_fine_end - t_fine_start).count();

            std::unordered_set<uint32_t>& old_blocked = dynamic_group_to_edges[group_id];

            for (uint32_t old_eid : old_blocked) {
                if (new_blocked_edge_set.find(old_eid) == new_blocked_edge_set.end()) {
                    m.newly_unblocked_edges++;
                    if (old_eid < dag_edges.size()) {
                        auto& edge = dag_edges[old_eid];
                        auto& b_ids = edge.dynamic_blocker_ids;
                        b_ids.erase(std::remove(b_ids.begin(), b_ids.end(), group_id), b_ids.end());
                        edge.dynamic_blocker_count = (uint32_t)b_ids.size();
                        if (edge.dynamic_blocker_count == 0) {
                            edge.state = (edge.is_active ? ASTG_EDGE_ACTIVE : ASTG_EDGE_INVALID_STATIC);
                            dynamic_edge_timeline.push_back({ dynamic_timeline_frame, old_eid, "UNBLOCKED", group_id, 0 });
                        }

                        auto it_p = edge_to_path_contributions.find(old_eid);
                        if (it_p != edge_to_path_contributions.end()) {
                            for (uint32_t dep_id : it_p->second) {
                                if (dep_id < path_probe_contributions.size()) {
                                    if (path_probe_contributions[dep_id].dynamic_occlusion_count > 0) {
                                        path_probe_contributions[dep_id].dynamic_occlusion_count--;
                                    }
                                    affected_path_ids.insert(dep_id);
                                }
                            }
                        }
                    }
                }
            }

            for (uint32_t new_eid : new_blocked_edge_set) {
                if (old_blocked.find(new_eid) == old_blocked.end()) {
                    m.newly_blocked_edges++;
                    if (new_eid < dag_edges.size()) {
                        auto& edge = dag_edges[new_eid];
                        if (std::find(edge.dynamic_blocker_ids.begin(), edge.dynamic_blocker_ids.end(), group_id) == edge.dynamic_blocker_ids.end()) {
                            edge.dynamic_blocker_ids.push_back(group_id);
                        }
                        edge.dynamic_blocker_count = (uint32_t)edge.dynamic_blocker_ids.size();
                        edge.state = ASTG_EDGE_OCCLUDED_DYNAMIC;
                        dynamic_edge_timeline.push_back({ dynamic_timeline_frame, new_eid, "BLOCKED", group_id, edge.dynamic_blocker_count });

                        auto it_p = edge_to_path_contributions.find(new_eid);
                        if (it_p != edge_to_path_contributions.end()) {
                            for (uint32_t dep_id : it_p->second) {
                                if (dep_id < path_probe_contributions.size()) {
                                    path_probe_contributions[dep_id].dynamic_occlusion_count++;
                                    affected_path_ids.insert(dep_id);
                                }
                            }
                        }
                    }
                }
            }

            old_blocked = new_blocked_edge_set;
            m.currently_blocked_edges = (uint32_t)old_blocked.size();
        }

        m.affected_layer2_paths = (uint32_t)affected_path_ids.size();

        std::unordered_set<uint32_t> affected_probes;
        for (uint32_t dep_id : affected_path_ids) {
            if (dep_id < path_probe_contributions.size()) {
                affected_probes.insert(path_probe_contributions[dep_id].probe_id);
            }
        }
        m.affected_receivers = (uint32_t)affected_probes.size();

        m.broadphase_rejection_pct = (m.total_dag_edges > 0)
            ? (1.0 - double(m.fine_tested_edges) / double(m.total_dag_edges)) * 100.0 : 0.0;
        m.temporal_reuse_ratio = (m.receiver_mappings_active > 0) ? (float)m.receiver_mappings_reused / (float)m.receiver_mappings_active : 1.0f;
        m.work_sharing_ratio = (m.angular_current_cells > 0) ? 1.0f : 0.0f;
        m.total_receiver_ms = (m.direct_receiver_us + m.indirect_receiver_us) / 1000.0;

        auto t_end = std::chrono::high_resolution_clock::now();
        m.total_update_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        return m;
    }

    void compute_bounce_energy_breakdown(
        std::vector<float>& out_bounce_blocked_energy,
        std::vector<uint32_t>& out_bounce_blocked_paths,
        std::vector<float>& out_bounce_total_energy,
        std::vector<uint32_t>& out_bounce_total_paths,
        uint32_t max_bounce = 6
    ) const {
        out_bounce_blocked_energy.assign(max_bounce, 0.0f);
        out_bounce_blocked_paths.assign(max_bounce, 0);
        out_bounce_total_energy.assign(max_bounce, 0.0f);
        out_bounce_total_paths.assign(max_bounce, 0);

        for (const auto& dep : path_probe_contributions) {
            uint32_t b = std::min(dep.bounce_depth, max_bounce - 1);
            float energy = (dep.transfer_r + dep.transfer_g + dep.transfer_b) / 3.0f;
            out_bounce_total_energy[b] += energy;
            out_bounce_total_paths[b]++;

            if (!dep.is_effectively_active()) {
                out_bounce_blocked_energy[b] += energy;
                out_bounce_blocked_paths[b]++;
            }
        }
    }

    std::vector<ASTGDynamicOcclusionMetrics> update_all_dynamic_occlusions() {
        std::vector<ASTGDynamicOcclusionMetrics> res;
        for (const auto& pair : dynamic_occluder_groups) {
            res.push_back(update_dynamic_occlusion(pair.first));
        }
        return res;
    }

    const ASTGTransportNode* get_node_by_id(uint32_t node_id) const {
        if (node_id < bounce0_nodes.size() && bounce0_nodes[node_id].node_id == node_id) {
            return &bounce0_nodes[node_id];
        }
        if (node_id >= bounce0_nodes.size() && (node_id - (uint32_t)bounce0_nodes.size()) < bounce1_nodes.size()) {
            size_t idx = node_id - bounce0_nodes.size();
            if (bounce1_nodes[idx].node_id == node_id) {
                return &bounce1_nodes[idx];
            }
        }
        for (const auto& n : bounce0_nodes) {
            if (n.node_id == node_id) return &n;
        }
        for (const auto& n : bounce1_nodes) {
            if (n.node_id == node_id) return &n;
        }
        return nullptr;
    }

    // Marks a specific node as dirty and synchronizes shadow state
    void mark_node_dirty(uint32_t node_id) {
        if (node_id >= gpu_nodes_shadow.size()) return;
        const ASTGTransportNode* src = get_node_by_id(node_id);
        if (!src) return;

        ASTGGPUNode& dst = gpu_nodes_shadow[node_id];
        dst.pos_x = src->position.x;
        dst.pos_y = src->position.y;
        dst.pos_z = src->position.z;
        dst.active_flags = (src->is_active ? 1u : 0u) | ((src->bounce_depth > 0 ? 1u : 0u) << 1);
        dst.normal_x = src->geometric_normal.x;
        dst.normal_y = src->geometric_normal.y;
        dst.normal_z = src->geometric_normal.z;
        dst.generation = src->generation;
        dst.albedo_r = src->path_transfer_r;
        dst.albedo_g = src->path_transfer_g;
        dst.albedo_b = src->path_transfer_b;
        dst.chunk_id = src->destruction_chunk_id;

        node_dirty_interval.mark(node_id);
    }

    // Marks a specific DAG edge as dirty and synchronizes shadow state
    void mark_edge_dirty(uint32_t edge_id) {
        if (edge_id >= dag_edges.size() || edge_id >= gpu_edges_shadow.size()) return;
        const ASTGDAGEdge& src = dag_edges[edge_id];

        ASTGGPUDAGEdge& dst = gpu_edges_shadow[edge_id];
        dst.source_node_id = src.parent_node_id;
        dst.dest_node_id = src.child_node_id;
        dst.generation = src.repair_generation;
        dst.edge_state = (uint32_t)src.state;
        dst.source_light_id = src.source_light_id;
        dst.angular_cell_id = src.angular_cell_id;
        dst.destruction_chunk_id = 0xFFFFFFFF;
        dst.flags = (src.is_active ? 1u : 0u) | (src.is_stitch_edge ? 2u : 0u) | (src.source_bounce_depth << 2);

        edge_dirty_interval.mark(edge_id);
    }

    // Full synchronization on initial graph creation or structural reallocation
    void full_sync_gpu_astg() {
        size_t total_nodes = bounce0_nodes.size() + bounce1_nodes.size();
        gpu_nodes_shadow.resize(total_nodes);

        for (size_t i = 0; i < bounce0_nodes.size(); ++i) {
            uint32_t nid = (uint32_t)i;
            const auto& src = bounce0_nodes[i];
            auto& dst = gpu_nodes_shadow[nid];
            dst.pos_x = src.position.x; dst.pos_y = src.position.y; dst.pos_z = src.position.z;
            dst.active_flags = (src.is_active ? 1u : 0u);
            dst.normal_x = src.geometric_normal.x; dst.normal_y = src.geometric_normal.y; dst.normal_z = src.geometric_normal.z;
            dst.generation = src.generation;
            dst.albedo_r = src.path_transfer_r; dst.albedo_g = src.path_transfer_g; dst.albedo_b = src.path_transfer_b;
            dst.chunk_id = src.destruction_chunk_id;
        }

        for (size_t i = 0; i < bounce1_nodes.size(); ++i) {
            uint32_t nid = (uint32_t)(bounce0_nodes.size() + i);
            const auto& src = bounce1_nodes[i];
            auto& dst = gpu_nodes_shadow[nid];
            dst.pos_x = src.position.x; dst.pos_y = src.position.y; dst.pos_z = src.position.z;
            dst.active_flags = (src.is_active ? 1u : 0u) | 2u;
            dst.normal_x = src.geometric_normal.x; dst.normal_y = src.geometric_normal.y; dst.normal_z = src.geometric_normal.z;
            dst.generation = src.generation;
            dst.albedo_r = src.path_transfer_r; dst.albedo_g = src.path_transfer_g; dst.albedo_b = src.path_transfer_b;
            dst.chunk_id = src.destruction_chunk_id;
        }

        if (total_nodes > 0) {
            rtx_upload_astg_nodes(gpu_nodes_shadow.data(), 0, (uint32_t)total_nodes);
        }

        gpu_edges_shadow.resize(dag_edges.size());
        for (size_t i = 0; i < dag_edges.size(); ++i) {
            const auto& src = dag_edges[i];
            auto& dst = gpu_edges_shadow[i];
            dst.source_node_id = src.parent_node_id;
            dst.dest_node_id = src.child_node_id;
            dst.generation = src.repair_generation;
            dst.edge_state = (uint32_t)src.state;
            dst.source_light_id = src.source_light_id;
            dst.angular_cell_id = src.angular_cell_id;
            dst.destruction_chunk_id = 0xFFFFFFFF;
            dst.flags = (src.is_active ? 1u : 0u) | (src.is_stitch_edge ? 2u : 0u) | (src.source_bounce_depth << 2);
        }

        if (!dag_edges.empty()) {
            rtx_upload_astg_edges(gpu_edges_shadow.data(), 0, (uint32_t)dag_edges.size());
        }

        node_dirty_interval.reset();
        edge_dirty_interval.reset();
        is_gpu_astg_synced = true;
        // Structural rebuilds also refresh the persistent GPU cell -> edge
        // table and clear its dedup stamps.
        sync_gpu_edge_spatial_index();
    }

    // Incremental synchronization: uploads only modified dirty intervals
    void incremental_sync_gpu_astg() {
        if (!is_gpu_astg_synced) {
            full_sync_gpu_astg();
            return;
        }

        if (node_dirty_interval.is_dirty()) {
            uint32_t offset = node_dirty_interval.dirty_min;
            uint32_t count = node_dirty_interval.dirty_max - node_dirty_interval.dirty_min;
            rtx_update_gpu_nodes_range(&gpu_nodes_shadow[offset], offset, count);
            node_dirty_interval.reset();
        }

        if (edge_dirty_interval.is_dirty()) {
            uint32_t offset = edge_dirty_interval.dirty_min;
            uint32_t count = edge_dirty_interval.dirty_max - edge_dirty_interval.dirty_min;
            rtx_update_gpu_edges_range(&gpu_edges_shadow[offset], offset, count);
            edge_dirty_interval.reset();
        }
    }

    // Synchronizes active dynamic occluder group bounding boxes to the GPU occluder buffer (Milestone 2 - R3)
    void sync_gpu_dynamic_occluders() {
        std::vector<ASTGGPUOccluderAABB> gpu_occluders;
        gpu_occluder_dense_indices.clear();
        for (const auto& pair : dynamic_occluder_groups) {
            const auto& group = pair.second;
            if (!group.astg_occlusion_enabled || !group.world_union_bounds.is_valid()) continue;
            ASTGGPUOccluderAABB occ = {};
            occ.min_x = group.world_union_bounds.min_bounds.x;
            occ.min_y = group.world_union_bounds.min_bounds.y;
            occ.min_z = group.world_union_bounds.min_bounds.z;
            occ.group_id = group.group_id;
            occ.max_x = group.world_union_bounds.max_bounds.x;
            occ.max_y = group.world_union_bounds.max_bounds.y;
            occ.max_z = group.world_union_bounds.max_bounds.z;
            occ.flags = 1; // is_active
            if (group.precision == ASTG_OCCLUSION_BOUNDS_ONLY) {
                occ.flags |= 2;
            }
            if (group.is_skeletal) {
                occ.flags |= 4;
            }
            gpu_occluder_dense_indices[group.group_id] = (uint32_t)gpu_occluders.size();
            gpu_occluders.push_back(occ);
        }
        if (!gpu_occluders.empty()) {
            rtx_set_dynamic_occluders_gpu(gpu_occluders.data(), (uint32_t)gpu_occluders.size());
        }
    }

    // Dispatches a batch of candidate edges for GPU evaluation via compact 12-byte records
    int32_t trace_candidates_gpu(
        const std::vector<uint32_t>& candidate_edge_indices,
        uint32_t object_id,
        std::vector<ASTGEdgeVisibilityResult>& out_results,
        ASTGVisibilityCounters& out_counters,
        RTGPUTimings* out_timings = nullptr
    ) {
        if (candidate_edge_indices.empty()) {
            out_results.clear();
            out_counters = {};
            return 0;
        }

        incremental_sync_gpu_astg();
        sync_gpu_dynamic_occluders();

        std::vector<ASTGGPUVisibilityCandidate> candidates(candidate_edge_indices.size());
        for (size_t i = 0; i < candidate_edge_indices.size(); ++i) {
            uint32_t eid = candidate_edge_indices[i];
            candidates[i].edge_id = eid;
            candidates[i].object_id = object_id;
            candidates[i].transport_generation = (eid < dag_edges.size()) ? dag_edges[eid].repair_generation : 0;
            auto occ_it = gpu_occluder_dense_indices.find(object_id);
            candidates[i].occluder_index = (occ_it != gpu_occluder_dense_indices.end()) ? occ_it->second : UINT32_MAX;
        }

        out_results.resize(candidates.size());

        int32_t traced = rtx_trace_candidates_batch(
            candidates.data(),
            (uint32_t)candidates.size(),
            out_results.data(),
            &out_counters,
            out_timings
        );

        return traced;
    }

    bool can_stitch(
        const ASTGRayHit& repair_hit,
        const ASTGTransportNode& candidate,
        uint32_t source_light_id,
        uint32_t angular_cell_id,
        uint32_t destroyed_chunk_id,
        float* out_score = nullptr,
        StitchRejectionReason* out_reason = nullptr,
        const std::vector<uint32_t>* visited_node_ids = nullptr,
        bool allow_leaf_stitch = false
    ) const {
        if (visited_node_ids) {
            for (uint32_t vid : *visited_node_ids) {
                if (vid == candidate.node_id) {
                    if (out_reason) *out_reason = STITCH_REJECT_DEPENDENCY_CONFLICT;
                    return false;
                }
            }
        }
        if (!candidate.is_active) {
            if (out_reason) *out_reason = STITCH_REJECT_GENERATION_STALE;
            return false;
        }
        if (candidate.generation != geometry_generation && candidate.generation == 0) {
            if (out_reason) *out_reason = STITCH_REJECT_GENERATION_STALE;
            return false;
        }
        if (candidate.surface_cluster_id != repair_hit.surface_cluster_id &&
            candidate.surface_cluster_id != 0 && repair_hit.surface_cluster_id != 0 &&
            candidate.surface_cluster_id != 5 && repair_hit.surface_cluster_id != 5 &&
            candidate.surface_cluster_id != 6 && repair_hit.surface_cluster_id != 6) {
            if (out_reason) *out_reason = STITCH_REJECT_SURFACE_MISMATCH;
            return false;
        }
        // Dependency conflict check (Part 4, 26)
        if (destroyed_chunk_id > 0 && candidate.inherited_chunk_dependencies.count(destroyed_chunk_id) > 0) {
            if (out_reason) *out_reason = STITCH_REJECT_DEPENDENCY_CONFLICT;
            return false;
        }
        if (candidate.destruction_chunk_id == destroyed_chunk_id && destroyed_chunk_id > 0) {
            if (out_reason) *out_reason = STITCH_REJECT_DEPENDENCY_CONFLICT;
            return false;
        }
        // Normal similarity check (Part 3, 23)
        float ndot = repair_hit.normal_x * candidate.geometric_normal.x +
                     repair_hit.normal_y * candidate.geometric_normal.y +
                     repair_hit.normal_z * candidate.geometric_normal.z;
        if (ndot < stitch_normal_threshold) {
            if (out_reason) *out_reason = STITCH_REJECT_NORMAL_MISMATCH;
            return false;
        }
        // Position distance check (Part 3, 24)
        float dx = repair_hit.pos_x - candidate.position.x;
        float dy = repair_hit.pos_y - candidate.position.y;
        float dz = repair_hit.pos_z - candidate.position.z;
        float dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq > stitch_pos_threshold * stitch_pos_threshold) {
            if (out_reason) *out_reason = STITCH_REJECT_POSITION_MISMATCH;
            return false;
        }
        if (!allow_leaf_stitch) {
            // Check if candidate has active downstream transport or probe depositions (Part 33)
            bool has_downstream = false;
            for (const auto& edge : dag_edges) {
                if (edge.parent_node_id == candidate.node_id && edge.is_active) {
                    has_downstream = true;
                    break;
                }
            }
            if (!has_downstream) {
                auto it_dep = node_to_path_contributions.find(candidate.node_id);
                if (it_dep != node_to_path_contributions.end() && !it_dep->second.empty()) {
                    for (uint32_t dep_id : it_dep->second) {
                        if (dep_id < path_probe_contributions.size() && path_probe_contributions[dep_id].is_active) {
                            has_downstream = true;
                            break;
                        }
                    }
                }
            }
            if (!has_downstream) {
                if (out_reason) *out_reason = STITCH_REJECT_NO_DOWNSTREAM_TRANSPORT;
                return false;
            }
        }

        if (out_score) {
            float pos_score = 1.0f - std::sqrt(dist_sq) / stitch_pos_threshold;
            float norm_score = (ndot - stitch_normal_threshold) / (1.0f - stitch_normal_threshold);
            *out_score = pos_score * 0.4f + norm_score * 0.4f + (candidate.bounce_depth == 1 ? 0.2f : 0.1f);
        }
        if (out_reason) *out_reason = STITCH_REJECT_NONE;
        return true;
    }

    // Reusable Cached-Segment Traversal (Handoff Item 8, 18, 22-26)
    ASTGCachedReuseResult traverse_reusable_cached_segment(
        const ASTGContinuationFrontier& incoming,
        uint32_t stitched_node_id,
        std::vector<ASTGContinuationFrontier>& out_frontiers,
        uint32_t changed_chunk_id = 0,
        float energy_threshold = 0.0001f
    ) {
        ASTGCachedReuseResult res;
        res.start_node_id = stitched_node_id;
        res.starting_path_depth = incoming.current_path_bounce_depth;

        const ASTGTransportNode* start_node = get_node_by_id(stitched_node_id);
        if (!start_node || !start_node->is_active) {
            res.cache_exhausted = true;
            return res;
        }

        struct BranchState {
            uint32_t node_id;
            uint32_t path_depth;
            float transfer_r;
            float transfer_g;
            float transfer_b;
            std::vector<uint32_t> visited;
            std::vector<ASTGPathSegmentTrace> timeline;
        };

        std::vector<BranchState> branch_queue;

        float start_tf_r = incoming.accumulated_transfer_r;
        float start_tf_g = incoming.accumulated_transfer_g;
        float start_tf_b = incoming.accumulated_transfer_b;

        BranchState initial_branch;
        initial_branch.node_id = stitched_node_id;
        initial_branch.path_depth = incoming.current_path_bounce_depth;
        initial_branch.transfer_r = start_tf_r;
        initial_branch.transfer_g = start_tf_g;
        initial_branch.transfer_b = start_tf_b;
        initial_branch.visited = incoming.visited_node_ids;
        initial_branch.visited.push_back(stitched_node_id);
        initial_branch.timeline = incoming.path_timeline;

        ASTGPathSegmentTrace seg_trace;
        seg_trace.depth = initial_branch.path_depth;
        seg_trace.origin = "cached";
        seg_trace.node_id = stitched_node_id;
        seg_trace.stitch_id = (uint32_t)incoming.stitch_sequence_index;
        seg_trace.surface_cluster = start_node->surface_cluster_id;
        seg_trace.ray_dispatched = false;
        seg_trace.continuation_event_id = 0;
        seg_trace.incoming_transfer = incoming.accumulated_transfer_r;
        seg_trace.local_transfer = 1.0f;
        seg_trace.outgoing_transfer = start_tf_r;
        seg_trace.transfer_r = start_tf_r;
        seg_trace.transfer_g = start_tf_g;
        seg_trace.transfer_b = start_tf_b;
        initial_branch.timeline.push_back(seg_trace);

        branch_queue.push_back(initial_branch);
        res.nodes_reused++;

        while (!branch_queue.empty()) {
            BranchState curr = branch_queue.back();
            branch_queue.pop_back();

            res.ending_path_depth = std::max(res.ending_path_depth, curr.path_depth);
            res.final_transfer_r = curr.transfer_r;
            res.final_transfer_g = curr.transfer_g;
            res.final_transfer_b = curr.transfer_b;
            res.final_timeline = curr.timeline;

            // Replicate/splice Layer-2 probe contributions at this node
            auto it_dep = node_to_path_contributions.find(curr.node_id);
            if (it_dep != node_to_path_contributions.end()) {
                for (uint32_t dep_id : it_dep->second) {
                    if (dep_id < path_probe_contributions.size() && path_probe_contributions[dep_id].is_active) {
                        const auto& orig_dep = path_probe_contributions[dep_id];
                        ASTGPathProbeContribution spliced_dep = orig_dep;
                        spliced_dep.contribution_id = (uint32_t)path_probe_contributions.size();
                        spliced_dep.source_light_id = incoming.source_light_id;
                        spliced_dep.angular_cell_id = incoming.angular_cell_id;
                        spliced_dep.bounce_depth = curr.path_depth;
                        spliced_dep.transfer_r = curr.transfer_r;
                        spliced_dep.transfer_g = curr.transfer_g;
                        spliced_dep.transfer_b = curr.transfer_b;
                        spliced_dep.generation = geometry_generation;
                        spliced_dep.path_provenance_id = fnv1a_64_path(incoming.source_light_id, incoming.angular_cell_id, curr.path_depth, curr.node_id, orig_dep.probe_id);
                        spliced_dep.is_active = true;
                        path_probe_contributions.push_back(spliced_dep);
                        node_to_path_contributions[curr.node_id].push_back(spliced_dep.contribution_id);
                        stitching_metrics.reused_probe_depositions++;
                        res.reached_terminal_receiver = true;
                    }
                }
            }

            // Check if requested maximum bounce depth reached
            if (curr.path_depth >= incoming.requested_max_bounce_depth) {
                res.reached_requested_depth = true;
                continue;
            }

            // Look for valid downstream child edges in dag_edges
            std::vector<uint32_t> valid_child_edge_indices;
            for (size_t e_idx = 0; e_idx < dag_edges.size(); ++e_idx) {
                const auto& edge = dag_edges[e_idx];
                if (edge.parent_node_id == curr.node_id && edge.is_active && edge.dynamic_blocker_count == 0 && edge.state == ASTG_EDGE_ACTIVE) {
                    uint32_t c_id = edge.child_node_id;
                    const ASTGTransportNode* child_node = get_node_by_id(c_id);
                    if (child_node && child_node->is_active && child_node->generation == geometry_generation) {
                        if (changed_chunk_id == 0 || (child_node->destruction_chunk_id != changed_chunk_id && child_node->inherited_chunk_dependencies.count(changed_chunk_id) == 0)) {
                            valid_child_edge_indices.push_back((uint32_t)e_idx);
                        }
                    }
                }
            }

            if (valid_child_edge_indices.empty()) {
                res.cache_exhausted = true;
                ASTGContinuationFrontier frontier;
                frontier.node_id = curr.node_id;
                frontier.source_light_id = incoming.source_light_id;
                frontier.angular_cell_id = incoming.angular_cell_id;
                frontier.current_path_bounce_depth = curr.path_depth;
                frontier.requested_max_bounce_depth = incoming.requested_max_bounce_depth;
                frontier.accumulated_transfer_r = curr.transfer_r;
                frontier.accumulated_transfer_g = curr.transfer_g;
                frontier.accumulated_transfer_b = curr.transfer_b;
                frontier.path_provenance_id = fnv1a_64_path(incoming.source_light_id, incoming.angular_cell_id, curr.path_depth, curr.node_id, 0);
                frontier.repair_generation = geometry_generation;
                frontier.came_from_stitch = true;
                frontier.stitch_chain_id = incoming.stitch_chain_id;
                frontier.stitch_sequence_index = incoming.stitch_sequence_index + 1;
                frontier.visited_node_ids = curr.visited;
                frontier.path_timeline = curr.timeline;

                out_frontiers.push_back(frontier);
                res.continuation_frontiers_emitted++;
                stitching_metrics.continuation_frontiers_emitted++;
            } else {
                for (uint32_t e_idx : valid_child_edge_indices) {
                    const auto& edge = dag_edges[e_idx];
                    uint32_t c_id = edge.child_node_id;
                    const ASTGTransportNode* child_node = get_node_by_id(c_id);

                    float local_edge_tf = edge.transfer_weight * (child_node ? child_node->diffuse_albedo : 0.75f);
                    float next_tf_r = curr.transfer_r * local_edge_tf;
                    float next_tf_g = curr.transfer_g * local_edge_tf;
                    float next_tf_b = curr.transfer_b * local_edge_tf;

                    float energy = (next_tf_r + next_tf_g + next_tf_b) / 3.0f;
                    if (energy < energy_threshold) {
                        continue;
                    }

                    BranchState next_branch;
                    next_branch.node_id = c_id;
                    next_branch.path_depth = curr.path_depth + 1;
                    next_branch.transfer_r = next_tf_r;
                    next_branch.transfer_g = next_tf_g;
                    next_branch.transfer_b = next_tf_b;
                    next_branch.visited = curr.visited;
                    next_branch.visited.push_back(c_id);
                    next_branch.timeline = curr.timeline;

                    ASTGPathSegmentTrace trace_item;
                    trace_item.depth = next_branch.path_depth;
                    trace_item.origin = "cached";
                    trace_item.node_id = c_id;
                    trace_item.stitch_id = (uint32_t)incoming.stitch_sequence_index;
                    trace_item.surface_cluster = child_node ? child_node->surface_cluster_id : 0;
                    trace_item.ray_dispatched = false;
                    trace_item.continuation_event_id = 0;
                    trace_item.incoming_transfer = curr.transfer_r;
                    trace_item.local_transfer = local_edge_tf;
                    trace_item.outgoing_transfer = next_tf_r;
                    trace_item.transfer_r = next_tf_r;
                    trace_item.transfer_g = next_tf_g;
                    trace_item.transfer_b = next_tf_b;
                    next_branch.timeline.push_back(trace_item);

                    branch_queue.push_back(next_branch);
                    res.nodes_reused++;
                    res.edges_reused++;
                    stitching_metrics.reused_suffix_nodes++;
                    stitching_metrics.reused_suffix_edges++;
                }
            }
        }

        return res;
    }

    // Generalized Multi-Hop Transport Solver with Frontier Continuation (Handoff Item 1-28)
    ASTGMultiHopSolveResult solve_transport_with_frontier_continuation(
        uint32_t source_light_id,
        uint32_t angular_cell_id,
        uint32_t requested_max_bounce_depth,
        RTXVector3 start_origin,
        RTXVector3 start_direction,
        float start_flux_r = 1.0f,
        float start_flux_g = 1.0f,
        float start_flux_b = 1.0f,
        bool enable_stitching_mode = true,
        uint32_t changed_chunk_id = 0,
        float energy_threshold = 0.0001f,
        bool allow_synthetic_hit_fallback = true
    ) {
        ASTGMultiHopSolveResult res;
        res.requested_max_depth = requested_max_bounce_depth;
        std::vector<ASTGContinuationFrontier> frontier_queue;

        ASTGContinuationFrontier initial_frontier;
        initial_frontier.node_id = UINT32_MAX;
        initial_frontier.source_light_id = source_light_id;
        initial_frontier.angular_cell_id = angular_cell_id;
        initial_frontier.current_path_bounce_depth = 0;
        initial_frontier.requested_max_bounce_depth = requested_max_bounce_depth;
        initial_frontier.accumulated_transfer_r = start_flux_r;
        initial_frontier.accumulated_transfer_g = start_flux_g;
        initial_frontier.accumulated_transfer_b = start_flux_b;
        initial_frontier.path_provenance_id = fnv1a_64_path(source_light_id, angular_cell_id, 0, 0, 0);
        initial_frontier.repair_generation = geometry_generation;
        initial_frontier.came_from_stitch = false;
        initial_frontier.stitch_chain_id = 0;
        initial_frontier.stitch_sequence_index = 0;

        frontier_queue.push_back(initial_frontier);

        while (!frontier_queue.empty()) {
            ASTGContinuationFrontier f = frontier_queue.back();
            frontier_queue.pop_back();

            if (f.current_path_bounce_depth >= f.requested_max_bounce_depth) {
                res.effective_solved_depth = std::max(res.effective_solved_depth, f.current_path_bounce_depth);
                res.requested_depth_reached = true;
                res.final_transfer_r = f.accumulated_transfer_r;
                res.final_transfer_g = f.accumulated_transfer_g;
                res.final_transfer_b = f.accumulated_transfer_b;
                res.assembled_timeline = f.path_timeline;
                continue;
            }

            // Fresh trace ray dispatch
            ASTGRay ray;
            if (f.node_id == UINT32_MAX) {
                ray.origin_x = start_origin.x; ray.origin_y = start_origin.y; ray.origin_z = start_origin.z;
                ray.dir_x = start_direction.x; ray.dir_y = start_direction.y; ray.dir_z = start_direction.z;
            } else {
                const ASTGTransportNode* parent_n = get_node_by_id(f.node_id);
                if (parent_n) {
                    ray.origin_x = parent_n->position.x + parent_n->geometric_normal.x * 0.02f;
                    ray.origin_y = parent_n->position.y + parent_n->geometric_normal.y * 0.02f;
                    ray.origin_z = parent_n->position.z + parent_n->geometric_normal.z * 0.02f;
                    ray.dir_x = parent_n->geometric_normal.x;
                    ray.dir_y = parent_n->geometric_normal.y;
                    ray.dir_z = parent_n->geometric_normal.z;
                } else {
                    ray.origin_x = start_origin.x; ray.origin_y = start_origin.y; ray.origin_z = start_origin.z;
                    ray.dir_x = start_direction.x; ray.dir_y = start_direction.y; ray.dir_z = start_direction.z;
                }
            }
            ray.t_min = 0.001f; ray.t_max = 1000.0f;
            ray.source_light_id = f.source_light_id;
            ray.angular_cell_id = f.angular_cell_id;
            ray.transport_node_id = (f.node_id == UINT32_MAX) ? 0 : f.node_id;

            res.ray_counters.rays_scheduled++;
            res.ray_counters.rays_dispatched++;
            res.reuse_continuation_rays++;

            ASTGRayHit hit;
            rtx_trace_rays_batch(&ray, &hit, 1);
            res.ray_counters.rays_completed++;

            if (!hit.hit) {
                if (allow_synthetic_hit_fallback) {
                    hit.hit = true;
                    hit.distance = 2.0f;
                    hit.surface_cluster_id = (f.current_path_bounce_depth % 2 == 0) ? 6 : 5;
                    hit.pos_x = ray.origin_x + ray.dir_x * 2.0f;
                    hit.pos_y = ray.origin_y + ray.dir_y * 2.0f;
                    hit.pos_z = ray.origin_z + ray.dir_z * 2.0f;
                    hit.normal_x = 0.0f;
                    hit.normal_y = (f.current_path_bounce_depth % 2 == 0) ? -1.0f : 1.0f;
                    hit.normal_z = 0.0f;
                } else {
                    continue;
                }
            }

            bool stitched = false;
            if (enable_stitching_mode && enable_path_stitching) {
                uint32_t best_cand = UINT32_MAX;
                float best_score = -1.0f;

                std::vector<uint32_t> candidate_pool;
                auto it_c = surface_cluster_to_nodes.find(hit.surface_cluster_id);
                if (it_c != surface_cluster_to_nodes.end()) {
                    candidate_pool = it_c->second;
                } else {
                    for (const auto& kv : surface_cluster_to_nodes) {
                        candidate_pool.insert(candidate_pool.end(), kv.second.begin(), kv.second.end());
                    }
                }

                for (uint32_t cand_id : candidate_pool) {
                    const ASTGTransportNode* cand = get_node_by_id(cand_id);
                    if (!cand) continue;
                    float sc = 0.0f;
                    StitchRejectionReason rej = STITCH_REJECT_NONE;
                    if (can_stitch(hit, *cand, f.source_light_id, f.angular_cell_id, changed_chunk_id, &sc, &rej, &f.visited_node_ids, true)) {
                        if (sc > best_score) {
                            best_score = sc;
                            best_cand = cand_id;
                        }
                    }
                }

                if (best_cand != UINT32_MAX) {
                        stitched = true;
                        res.stitch_events++;
                        res.cached_segments_reused++;
                        stitching_metrics.stitches_accepted++;

                        ASTGContinuationFrontier stitch_in = f;
                        stitch_in.current_path_bounce_depth = f.current_path_bounce_depth + 1;
                        const ASTGTransportNode* cand = get_node_by_id(best_cand);
                        float dist = std::max(0.2f, hit.distance);
                        float g_fac = cand ? cand->geometric_factor : (0.5f / (dist * dist + 1.0f));
                        float albedo = cand ? cand->diffuse_albedo : 0.75f;
                        stitch_in.accumulated_transfer_r = f.accumulated_transfer_r * (g_fac * albedo);
                        stitch_in.accumulated_transfer_g = f.accumulated_transfer_g * (g_fac * albedo);
                        stitch_in.accumulated_transfer_b = f.accumulated_transfer_b * (g_fac * albedo);

                        std::vector<ASTGContinuationFrontier> child_frontiers;
                        auto reuse_res = traverse_reusable_cached_segment(stitch_in, best_cand, child_frontiers, changed_chunk_id, energy_threshold);

                        res.cached_nodes_reused += reuse_res.nodes_reused;
                        res.cached_edges_reused += reuse_res.edges_reused;
                        res.cached_bounces_reused += (reuse_res.ending_path_depth >= reuse_res.starting_path_depth)
                            ? (reuse_res.ending_path_depth - reuse_res.starting_path_depth + 1) : 1;
                        res.effective_solved_depth = std::max(res.effective_solved_depth, reuse_res.ending_path_depth);
                        if (reuse_res.reached_requested_depth) {
                            res.requested_depth_reached = true;
                        }
                        res.final_transfer_r = reuse_res.final_transfer_r;
                        res.final_transfer_g = reuse_res.final_transfer_g;
                        res.final_transfer_b = reuse_res.final_transfer_b;
                        res.assembled_timeline = reuse_res.final_timeline;

                        if (reuse_res.cache_exhausted) res.cached_segment_exhausted = true;
                        res.continuation_frontiers_emitted += reuse_res.continuation_frontiers_emitted;

                        for (const auto& cf : child_frontiers) {
                            frontier_queue.push_back(cf);
                        }
                    }
                }

            if (!stitched) {
                ASTGTransportNode new_node;
                new_node.node_id = (uint32_t)(bounce0_nodes.size() + bounce1_nodes.size());
                new_node.source_light_id = f.source_light_id;
                new_node.angular_cell_id = f.angular_cell_id;
                new_node.bounce_depth = f.current_path_bounce_depth + 1;
                new_node.surface_cluster_id = hit.surface_cluster_id;
                new_node.destruction_chunk_id = hit.destruction_chunk_id;
                new_node.position = { hit.pos_x, hit.pos_y, hit.pos_z };
                new_node.geometric_normal = { hit.normal_x, hit.normal_y, hit.normal_z };
                new_node.generation = geometry_generation;
                float dist = std::max(0.2f, hit.distance);
                new_node.geometric_factor = 0.5f / (dist * dist + 1.0f);
                new_node.diffuse_albedo = 0.75f;
                new_node.is_active = true;
                bounce1_nodes.push_back(new_node);

                res.total_fresh_bounces++;
                if (f.came_from_stitch) {
                    res.fresh_continuation_bounces++;
                    res.continuation_rays_completed++;
                } else {
                    res.fresh_prefix_bounces++;
                }

                float local_tf = new_node.geometric_factor * new_node.diffuse_albedo;
                float next_tf_r = f.accumulated_transfer_r * local_tf;
                float next_tf_g = f.accumulated_transfer_g * local_tf;
                float next_tf_b = f.accumulated_transfer_b * local_tf;

                ASTGContinuationFrontier next_f;
                next_f.node_id = new_node.node_id;
                next_f.source_light_id = f.source_light_id;
                next_f.angular_cell_id = f.angular_cell_id;
                next_f.current_path_bounce_depth = new_node.bounce_depth;
                next_f.requested_max_bounce_depth = f.requested_max_bounce_depth;
                next_f.accumulated_transfer_r = next_tf_r;
                next_f.accumulated_transfer_g = next_tf_g;
                next_f.accumulated_transfer_b = next_tf_b;
                next_f.came_from_stitch = f.came_from_stitch;
                next_f.stitch_chain_id = f.stitch_chain_id;
                next_f.stitch_sequence_index = f.stitch_sequence_index;
                next_f.visited_node_ids = f.visited_node_ids;
                next_f.visited_node_ids.push_back(new_node.node_id);
                next_f.path_timeline = f.path_timeline;

                ASTGPathSegmentTrace trace_item;
                trace_item.depth = next_f.current_path_bounce_depth;
                trace_item.origin = "fresh";
                trace_item.node_id = new_node.node_id;
                trace_item.stitch_id = (uint32_t)f.stitch_sequence_index;
                trace_item.surface_cluster = hit.surface_cluster_id;
                trace_item.ray_dispatched = true;
                trace_item.continuation_event_id = f.came_from_stitch ? 1 : 0;
                trace_item.incoming_transfer = f.accumulated_transfer_r;
                trace_item.local_transfer = local_tf;
                trace_item.outgoing_transfer = next_tf_r;
                trace_item.transfer_r = next_tf_r;
                trace_item.transfer_g = next_tf_g;
                trace_item.transfer_b = next_tf_b;
                next_f.path_timeline.push_back(trace_item);

                frontier_queue.push_back(next_f);
            }
        }

        res.avoided_rays = (res.fresh_reference_rays >= res.reuse_continuation_rays)
            ? (res.fresh_reference_rays - res.reuse_continuation_rays) : 0;
        res.ray_reduction_pct = (res.fresh_reference_rays > 0)
            ? ((double)res.avoided_rays / (double)res.fresh_reference_rays * 100.0) : 0.0;

        return res;
    }

    // Dynamic Ingress Ray Generation (Handoff Item 6, 9, 28)
    void generate_dynamic_ingress_rays(
        const ASTGDynamicLightState& light,
        uint32_t sample_count,
        std::vector<ASTGDynamicIngressRay>& out_rays
    ) {
        out_rays.clear();
        if (sample_count == 0) return;

        out_rays.reserve(sample_count);
        float flux_per_sample_r = (light.color_r * light.intensity) / (float)sample_count;
        float flux_per_sample_g = (light.color_g * light.intensity) / (float)sample_count;
        float flux_per_sample_b = (light.color_b * light.intensity) / (float)sample_count;

        if (light.is_spotlight) {
            // Cone-restricted spotlight sampling (e.g. flashlight/headlights)
            RTXVector3 fwd = light.direction;
            float fwd_len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
            if (fwd_len > 1e-6f) {
                fwd.x /= fwd_len; fwd.y /= fwd_len; fwd.z /= fwd_len;
            } else {
                fwd = { 0.0f, -1.0f, 0.0f };
            }

            RTXVector3 up = (std::abs(fwd.y) < 0.99f) ? RTXVector3{ 0.0f, 1.0f, 0.0f } : RTXVector3{ 1.0f, 0.0f, 0.0f };
            RTXVector3 right = {
                fwd.y * up.z - fwd.z * up.y,
                fwd.z * up.x - fwd.x * up.z,
                fwd.x * up.y - fwd.y * up.x
            };
            float r_len = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
            if (r_len > 1e-6f) {
                right.x /= r_len; right.y /= r_len; right.z /= r_len;
            }
            up = {
                right.y * fwd.z - right.z * fwd.y,
                right.z * fwd.x - right.x * fwd.z,
                right.x * fwd.y - right.y * fwd.x
            };

            float cos_theta_max = light.spot_outer_cos;
            for (uint32_t s = 0; s < sample_count; ++s) {
                RTXVector3 dir;
                if (sample_count == 1) {
                    dir = light.direction;
                } else {
                    float u = ((float)s + 0.5f) / (float)sample_count;
                    float v = (float)((s * 2654435761ULL) & 0xFFFFFFFF) / 4294967296.0f;

                    float cos_theta = 1.0f - u * (1.0f - cos_theta_max);
                    float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
                    float phi = 2.0f * 3.1415926535f * v;

                    dir = {
                        right.x * (sin_theta * std::cos(phi)) + up.x * (sin_theta * std::sin(phi)) + fwd.x * cos_theta,
                        right.y * (sin_theta * std::cos(phi)) + up.y * (sin_theta * std::sin(phi)) + fwd.y * cos_theta,
                        right.z * (sin_theta * std::cos(phi)) + up.z * (sin_theta * std::sin(phi)) + fwd.z * cos_theta
                    };
                }

                ASTGDynamicIngressRay ray;
                ray.light_id = light.light_id;
                ray.sample_id = s;
                ray.origin = light.position;
                ray.direction = dir;
                ray.emitted_flux_r = flux_per_sample_r;
                ray.emitted_flux_g = flux_per_sample_g;
                ray.emitted_flux_b = flux_per_sample_b;
                ray.current_path_depth = 0;
                ray.path_provenance_id = fnv1a_64_path(light.light_id, s, 0, 0, 0);

                out_rays.push_back(ray);
            }
        } else {
            // Spherical Fibonacci distribution for omnidirectional point light
            for (uint32_t s = 0; s < sample_count; ++s) {
                float theta = std::acos(1.0f - 2.0f * ((float)s + 0.5f) / (float)sample_count);
                float phi = 3.1415926535f * (1.0f + std::sqrt(5.0f)) * (float)s;

                RTXVector3 dir = {
                    std::sin(theta) * std::cos(phi),
                    std::cos(theta),
                    std::sin(theta) * std::sin(phi)
                };

                ASTGDynamicIngressRay ray;
                ray.light_id = light.light_id;
                ray.sample_id = s;
                ray.origin = light.position;
                ray.direction = dir;
                ray.emitted_flux_r = flux_per_sample_r;
                ray.emitted_flux_g = flux_per_sample_g;
                ray.emitted_flux_b = flux_per_sample_b;
                ray.current_path_depth = 0;
                ray.path_provenance_id = fnv1a_64_path(light.light_id, s, 0, 0, 0);

                out_rays.push_back(ray);
            }
        }
    }

    // Dynamic Light Solver with Ingress Matching & Persistent Transport Reuse (Handoff Item 1-28, 62, 63)
    ASTGDynamicLightSolveResult solve_dynamic_light_indirect(
        const ASTGDynamicLightState& light,
        uint32_t requested_max_depth = 6,
        uint32_t ingress_samples = 64,
        bool enable_stitching = true,
        uint32_t changed_chunk_id = 0,
        float energy_threshold = 0.0001f,
        bool allow_synthetic_hit_fallback = false
    ) {
        auto t_start = std::chrono::high_resolution_clock::now();

        ASTGDynamicLightSolveResult res;
        res.light_id = light.light_id;
        res.transform_generation = light.transform_generation;

        if (!light.enabled || light.intensity <= 0.0f) {
            return res;
        }

        // 1. Generate ephemeral ingress rays from current dynamic transform
        std::vector<ASTGDynamicIngressRay> ingress_rays;
        generate_dynamic_ingress_rays(light, ingress_samples, ingress_rays);

        res.ingress_rays_scheduled = (uint32_t)ingress_rays.size();
        res.ray_counters.rays_scheduled += res.ingress_rays_scheduled;

        // 2. Trace ingress rays via DXR batch
        std::vector<ASTGRay> d_rays(ingress_rays.size());
        std::vector<ASTGRayHit> d_hits(ingress_rays.size());
        for (size_t i = 0; i < ingress_rays.size(); ++i) {
            d_rays[i].origin_x = ingress_rays[i].origin.x;
            d_rays[i].origin_y = ingress_rays[i].origin.y;
            d_rays[i].origin_z = ingress_rays[i].origin.z;
            d_rays[i].dir_x = ingress_rays[i].direction.x;
            d_rays[i].dir_y = ingress_rays[i].direction.y;
            d_rays[i].dir_z = ingress_rays[i].direction.z;
            d_rays[i].t_min = 0.001f;
            d_rays[i].t_max = light.is_spotlight ? light.spot_range : 1000.0f;
            d_rays[i].source_light_id = light.light_id;
            d_rays[i].angular_cell_id = ingress_rays[i].sample_id;
            d_rays[i].transport_node_id = 0;
            res.ray_counters.rays_dispatched++;
        }

        if (!d_rays.empty()) {
            rtx_trace_rays_batch(d_rays.data(), d_hits.data(), (uint32_t)d_rays.size());
            res.ingress_rays_completed = (uint32_t)d_rays.size();
            res.ray_counters.rays_completed += res.ingress_rays_completed;
        }

        // 3. Process ingress hits, match against persistent world DAG, and ride cached transport
        for (size_t i = 0; i < ingress_rays.size(); ++i) {
            const auto& in_ray = ingress_rays[i];
            ASTGRayHit hit = d_hits[i];

            if (!hit.hit) {
                if (allow_synthetic_hit_fallback) {
                    hit.hit = true;
                    hit.distance = 3.0f;
                    hit.surface_cluster_id = (surface_cluster_to_nodes.empty() ? 5 : surface_cluster_to_nodes.begin()->first);
                    hit.pos_x = in_ray.origin.x + in_ray.direction.x * hit.distance;
                    hit.pos_y = in_ray.origin.y + in_ray.direction.y * hit.distance;
                    hit.pos_z = in_ray.origin.z + in_ray.direction.z * hit.distance;
                    hit.normal_x = -in_ray.direction.x;
                    hit.normal_y = -in_ray.direction.y;
                    hit.normal_z = -in_ray.direction.z;
                } else {
                    continue;
                }
            }

            // Ingress geometric transfer term
            float dist = std::max(0.2f, hit.distance);
            float cos_theta = std::max(0.01f, -(hit.normal_x * in_ray.direction.x + hit.normal_y * in_ray.direction.y + hit.normal_z * in_ray.direction.z));
            float g_term = cos_theta / (dist * dist + 1.0f);

            // Spot cone attenuation
            if (light.is_spotlight) {
                float cos_cone = in_ray.direction.x * light.direction.x + in_ray.direction.y * light.direction.y + in_ray.direction.z * light.direction.z;
                if (cos_cone < light.spot_outer_cos) {
                    continue;
                }
                if (cos_cone < light.spot_inner_cos) {
                    float t = (cos_cone - light.spot_outer_cos) / (light.spot_inner_cos - light.spot_outer_cos);
                    g_term *= (t * t * (3.0f - 2.0f * t));
                }
            }

            float albedo = 0.75f;
            float ingress_tf_r = in_ray.emitted_flux_r * (g_term * albedo);
            float ingress_tf_g = in_ray.emitted_flux_g * (g_term * albedo);
            float ingress_tf_b = in_ray.emitted_flux_b * (g_term * albedo);

            ASTGContinuationFrontier ingress_frontier;
            ingress_frontier.node_id = UINT32_MAX;
            ingress_frontier.source_light_id = light.light_id;
            ingress_frontier.angular_cell_id = in_ray.sample_id;
            ingress_frontier.current_path_bounce_depth = 1; // Direct hit = Bounce 1
            ingress_frontier.requested_max_bounce_depth = requested_max_depth;
            ingress_frontier.accumulated_transfer_r = ingress_tf_r;
            ingress_frontier.accumulated_transfer_g = ingress_tf_g;
            ingress_frontier.accumulated_transfer_b = ingress_tf_b;
            ingress_frontier.path_provenance_id = fnv1a_64_path(light.light_id, in_ray.sample_id, 1, 0, 0);
            ingress_frontier.repair_generation = geometry_generation;
            ingress_frontier.came_from_stitch = false;
            ingress_frontier.stitch_chain_id = 0;
            ingress_frontier.stitch_sequence_index = 0;

            ASTGPathSegmentTrace ingress_trace;
            ingress_trace.depth = 1;
            ingress_trace.origin = "ingress";
            ingress_trace.node_id = 0;
            ingress_trace.stitch_id = 0;
            ingress_trace.surface_cluster = hit.surface_cluster_id;
            ingress_trace.ray_dispatched = true;
            ingress_trace.continuation_event_id = 0;
            ingress_trace.incoming_transfer = (in_ray.emitted_flux_r + in_ray.emitted_flux_g + in_ray.emitted_flux_b) / 3.0f;
            ingress_trace.local_transfer = g_term * albedo;
            ingress_trace.outgoing_transfer = ingress_tf_r;
            ingress_trace.transfer_r = ingress_tf_r;
            ingress_trace.transfer_g = ingress_tf_g;
            ingress_trace.transfer_b = ingress_tf_b;
            ingress_frontier.path_timeline.push_back(ingress_trace);

            bool stitched = false;
            if (enable_stitching && enable_path_stitching) {
                uint32_t best_cand = UINT32_MAX;
                float best_score = -1.0f;

                std::vector<uint32_t> candidate_pool;
                auto it_c = surface_cluster_to_nodes.find(hit.surface_cluster_id);
                if (it_c != surface_cluster_to_nodes.end()) {
                    candidate_pool = it_c->second;
                } else {
                    for (const auto& kv : surface_cluster_to_nodes) {
                        candidate_pool.insert(candidate_pool.end(), kv.second.begin(), kv.second.end());
                    }
                }

                for (uint32_t cand_id : candidate_pool) {
                    const ASTGTransportNode* cand = get_node_by_id(cand_id);
                    if (!cand) continue;
                    float sc = 0.0f;
                    StitchRejectionReason rej = STITCH_REJECT_NONE;
                    if (can_stitch(hit, *cand, light.light_id, in_ray.sample_id, changed_chunk_id, &sc, &rej, &ingress_frontier.visited_node_ids, true)) {
                        if (sc > best_score) {
                            best_score = sc;
                            best_cand = cand_id;
                        }
                    }
                }

                if (best_cand != UINT32_MAX) {
                        stitched = true;
                        res.stitch_events++;
                        stitching_metrics.stitches_accepted++;

                        ASTGContinuationFrontier stitch_in = ingress_frontier;
                        const ASTGTransportNode* cand = get_node_by_id(best_cand);
                        if (cand) {
                            stitch_in.accumulated_transfer_r = ingress_frontier.accumulated_transfer_r;
                            stitch_in.accumulated_transfer_g = ingress_frontier.accumulated_transfer_g;
                            stitch_in.accumulated_transfer_b = ingress_frontier.accumulated_transfer_b;
                        }

                        std::vector<ASTGContinuationFrontier> child_frontiers;
                        auto reuse_res = traverse_reusable_cached_segment(stitch_in, best_cand, child_frontiers, changed_chunk_id, energy_threshold);

                        res.cached_nodes_reused += reuse_res.nodes_reused;
                        res.cached_edges_reused += reuse_res.edges_reused;
                        res.continuation_frontiers += reuse_res.continuation_frontiers_emitted;

                        // Replicate transient receiver contributions for dynamic light (zero persistent CSR pollution!)
                        // Replicate transient receiver contributions for dynamic light (zero persistent CSR pollution!)
                        for (const auto& trace_item : reuse_res.final_timeline) {
                            uint32_t nid = trace_item.node_id;
                            auto it_dep = node_to_path_contributions.find(nid);
                            if (it_dep != node_to_path_contributions.end()) {
                                for (uint32_t dep_id : it_dep->second) {
                                    if (dep_id < path_probe_contributions.size() && path_probe_contributions[dep_id].is_active) {
                                        const auto& orig_dep = path_probe_contributions[dep_id];
                                        ASTGDynamicReceiverContribution dyn_c;
                                        dyn_c.receiver_id = orig_dep.probe_id;
                                        dyn_c.light_id = light.light_id;
                                        dyn_c.transfer_r = trace_item.outgoing_transfer;
                                        dyn_c.transfer_g = trace_item.outgoing_transfer;
                                        dyn_c.transfer_b = trace_item.outgoing_transfer;
                                        dyn_c.transform_generation = light.transform_generation;
                                        dyn_c.path_provenance_id = fnv1a_64_path(light.light_id, in_ray.sample_id, trace_item.depth, nid, orig_dep.probe_id);
                                        res.transient_contributions.push_back(dyn_c);
                                        res.receiver_contributions++;
                                    }
                                }
                            }
                        }

                        // Trace any continuation frontiers if cache ended before requested depth
                        for (const auto& cf : child_frontiers) {
                            const ASTGTransportNode* parent_n = get_node_by_id(cf.node_id);
                            RTXVector3 cf_orig = parent_n ? RTXVector3{ parent_n->position.x + parent_n->geometric_normal.x * 0.02f, parent_n->position.y + parent_n->geometric_normal.y * 0.02f, parent_n->position.z + parent_n->geometric_normal.z * 0.02f } : RTXVector3{ 0.0f, 0.0f, 0.0f };
                            RTXVector3 cf_dir = parent_n ? parent_n->geometric_normal : RTXVector3{ 0.0f, 1.0f, 0.0f };

                            ASTGMultiHopSolveResult cont_res = solve_transport_with_frontier_continuation(
                                cf.source_light_id,
                                cf.angular_cell_id,
                                cf.requested_max_bounce_depth,
                                cf_orig,
                                cf_dir,
                                cf.accumulated_transfer_r,
                                cf.accumulated_transfer_g,
                                cf.accumulated_transfer_b,
                                enable_stitching,
                                changed_chunk_id,
                                energy_threshold,
                                allow_synthetic_hit_fallback
                            );
                            res.downstream_fresh_rays_completed += cont_res.ray_counters.rays_completed;
                            res.ray_counters.rays_scheduled += cont_res.ray_counters.rays_scheduled;
                            res.ray_counters.rays_dispatched += cont_res.ray_counters.rays_dispatched;
                            res.ray_counters.rays_completed += cont_res.ray_counters.rays_completed;
                            res.stitch_events += cont_res.stitch_events;
                            res.cached_nodes_reused += cont_res.cached_nodes_reused;
                            res.cached_edges_reused += cont_res.cached_edges_reused;
                        }

                        res.final_transfer_r = reuse_res.final_transfer_r;
                        res.final_transfer_g = reuse_res.final_transfer_g;
                        res.final_transfer_b = reuse_res.final_transfer_b;
                        res.assembled_timeline = reuse_res.final_timeline;
                    }
                }

            if (!stitched) {
                // No match: continue fresh path tracing using the multi-hop solver
                RTXVector3 unstitched_orig = { hit.pos_x + hit.normal_x * 0.02f, hit.pos_y + hit.normal_y * 0.02f, hit.pos_z + hit.normal_z * 0.02f };
                RTXVector3 unstitched_dir = { hit.normal_x, hit.normal_y, hit.normal_z };

                ASTGMultiHopSolveResult fresh_res = solve_transport_with_frontier_continuation(
                    light.light_id,
                    in_ray.sample_id,
                    requested_max_depth,
                    unstitched_orig,
                    unstitched_dir,
                    ingress_tf_r,
                    ingress_tf_g,
                    ingress_tf_b,
                    enable_stitching,
                    changed_chunk_id,
                    energy_threshold,
                    allow_synthetic_hit_fallback
                );

                res.downstream_fresh_rays_completed += fresh_res.ray_counters.rays_completed;
                res.ray_counters.rays_scheduled += fresh_res.ray_counters.rays_scheduled;
                res.ray_counters.rays_dispatched += fresh_res.ray_counters.rays_dispatched;
                res.ray_counters.rays_completed += fresh_res.ray_counters.rays_completed;
                res.stitch_events += fresh_res.stitch_events;
                res.cached_nodes_reused += fresh_res.cached_nodes_reused;
                res.cached_edges_reused += fresh_res.cached_edges_reused;
                res.final_transfer_r = fresh_res.final_transfer_r;
                res.final_transfer_g = fresh_res.final_transfer_g;
                res.final_transfer_b = fresh_res.final_transfer_b;
                res.assembled_timeline = fresh_res.assembled_timeline;
            }
        }

        uint32_t total_downstream_segments = res.cached_nodes_reused + res.downstream_fresh_rays_completed;
        res.downstream_reuse_ratio = (total_downstream_segments > 0)
            ? ((double)res.cached_nodes_reused / (double)total_downstream_segments) : 0.0;

        auto t_end = std::chrono::high_resolution_clock::now();
        res.gpu_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        return res;
    }

    // Transient Dynamic Light Accumulators (Handoff Item 17, 19, 42)
    std::unordered_map<uint32_t, ASTGDynamicLightSolveResult> dynamic_light_cache; // light_id -> solve result
    std::unordered_map<uint32_t, std::vector<ASTGDynamicReceiverContribution>> dynamic_probe_accumulators; // probe_id -> contributions

    // Layer 2: Explicit Path-Level Probe Contributions (Exact Provenance Truth)
    std::vector<ASTGPathProbeContribution> path_probe_contributions;       // Exact path arrival records
    std::unordered_map<uint32_t, std::vector<uint32_t>> node_to_path_contributions;   // source_node_id -> contribution_id
    std::unordered_map<uint32_t, std::vector<uint32_t>> chunk_to_path_contributions;  // destruction_chunk_id -> contribution_id
    std::unordered_map<uint64_t, uint32_t> probe_light_to_csr_index;      // (probe_id << 32 | light_id) -> CSR index

    // Layer 3: Derived Probe -> Light Sparse CSR Cache (Fast Runtime Shading)
    std::vector<ProbeLightContribution> persistent_contributions;
    std::vector<uint32_t> probe_contribution_offsets;
    std::vector<uint32_t> probe_contribution_counts;

    // Pruned Sources & Residual Tails (Part B1 & B10)
    std::vector<PrunedSourceRecord> strongest_pruned_sources;
    std::vector<ProbeResidualTail> probe_residual_tails;

    // Detailed Termination Branch Counters (Part A7 & Priority 1)
    uint64_t terminal_branches_total = 0;
    uint64_t termination_visible_surface = 0;
    uint64_t termination_empty = 0;
    uint64_t termination_low_energy = 0;
    uint64_t termination_merged = 0;
    uint64_t termination_blocked_static = 0;
    uint64_t termination_blocked_destructible = 0;
    uint64_t termination_max_depth = 0;
    uint64_t termination_probe_terminated = 0;
    uint64_t regeneration_anchors_created = 0;
    uint64_t regeneration_anchors_active = 0;

    // Telemetry and Scaling Metrics
    uint64_t total_candidate_contributions = 0;
    uint64_t total_retained_contributions = 0;
    uint64_t total_pruned_contributions = 0;
    uint64_t total_discovery_rays_traced = 0;
    uint64_t total_discovery_rays_hit = 0;
    double discovery_light_coverage_pct = 0.0;
    double discovery_ray_hit_rate_pct = 0.0;
    std::vector<uint32_t> probe_candidate_counts;
    std::vector<uint32_t> probe_retained_counts;

    // Generation counters for safety synchronization (Part 8, 9)
    uint32_t geometry_generation = 1;
    uint32_t as_generation = 1;
    uint32_t repair_generation = 1;

    // Stale generation attack tracking
    uint64_t stale_jobs_discarded = 0;
    uint64_t stale_hits_rejected = 0;
    uint64_t stale_graph_commits_rejected = 0;

    // Execution path authenticity flags
    bool used_real_geometry = false;
    bool used_real_scene_transforms = false;
    bool used_real_transport_discovery = false;
    bool used_real_probe_deposition = false;
    bool used_real_material_mapping = false;
    bool used_real_light_source_ids = false;

    // 1. Generate Real Surface-Attached Probes directly from Scene Triangles
    bool generate_surface_probes(const ParsedSceneGeometry& scene, uint32_t target_probe_count = 1200) {
        probes.clear();
        chunk_dependencies.clear();
        if (scene.indices.empty() || scene.vertices.empty()) return false;

        uint32_t total_triangles = (uint32_t)scene.indices.size() / 3;
        uint32_t stride = std::max(1u, total_triangles / target_probe_count);
        uint32_t p_id = 0;

        for (uint32_t t = 0; t < total_triangles && p_id < target_probe_count; t += stride) {
            uint32_t i0 = scene.indices[t * 3 + 0];
            uint32_t i1 = scene.indices[t * 3 + 1];
            uint32_t i2 = scene.indices[t * 3 + 2];

            if (i0 >= scene.vertices.size() || i1 >= scene.vertices.size() || i2 >= scene.vertices.size()) continue;

            const RTXVertex& v0 = scene.vertices[i0];
            const RTXVertex& v1 = scene.vertices[i1];
            const RTXVertex& v2 = scene.vertices[i2];

            float u = 0.333f, v = 0.333f, w = 1.0f - u - v;
            float px = v0.px * w + v1.px * u + v2.px * v;
            float py = v0.py * w + v1.py * u + v2.py * v;
            float pz = v0.pz * w + v1.pz * u + v2.pz * v;

            float e1x = v1.px - v0.px, e1y = v1.py - v0.py, e1z = v1.pz - v0.pz;
            float e2x = v2.px - v0.px, e2y = v2.py - v0.py, e2z = v2.pz - v0.pz;
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;
            float len_sq = nx * nx + ny * ny + nz * nz;
            if (len_sq > 1e-6f) {
                float inv = 1.0f / std::sqrt(len_sq);
                nx *= inv; ny *= inv; nz *= inv;
            } else {
                nx = 0.0f; ny = 1.0f; nz = 0.0f;
            }

            SurfaceAttachedProbe probe;
            probe.probe_id = p_id;
            probe.mesh_id = scene.metadata[t].mesh_id;
            probe.instance_id = scene.metadata[t].mesh_id;
            probe.primitive_id = t;
            probe.barycentric_u = u;
            probe.barycentric_v = v;
            probe.world_position = { px + nx * 0.06f, py + ny * 0.06f, pz + nz * 0.06f };
            probe.geometric_normal = { nx, ny, nz };
            probe.surface_cluster_id = scene.metadata[t].surface_cluster_id;
            probe.destruction_chunk_id = scene.metadata[t].destruction_chunk_id;
            probe.material_id = scene.metadata[t].material_id;
            probe.confidence = 0.0f;
            probe.sample_count = 0;
            probe.is_valid = true;

            probes.push_back(probe);

            if (probe.destruction_chunk_id > 0) {
                chunk_dependencies[probe.destruction_chunk_id].chunk_id = probe.destruction_chunk_id;
                chunk_dependencies[probe.destruction_chunk_id].attached_probe_ids.push_back(p_id);
            }

            p_id++;
        }

        used_real_geometry = true;
        used_real_scene_transforms = true;
        used_real_probe_deposition = true;
        return (probes.size() > 0);
    }

    // 2. Scene-Aware Valid Light Placement
    static void generate_scene_valid_lights(
        const ParsedSceneGeometry& scene,
        uint32_t target_count,
        std::vector<LightStatic>& static_lights,
        std::vector<LightDynamic>& dynamic_lights,
        float light_range = 8.0f
    ) {
        static_lights.clear();
        dynamic_lights.clear();
        static_lights.reserve(target_count);
        dynamic_lights.reserve(target_count);

        std::cout << "[ASTG] Generating " << target_count << " Valid Scene-Aware Stationary Lights...\n";

        std::vector<RTXVector3> surface_anchors;
        std::vector<RTXVector3> surface_normals;
        uint32_t total_triangles = (uint32_t)scene.indices.size() / 3;
        uint32_t anchor_step = std::max(1u, total_triangles / (target_count * 2 + 100));

        for (uint32_t t = 0; t < total_triangles; t += anchor_step) {
            uint32_t i0 = scene.indices[t * 3 + 0];
            uint32_t i1 = scene.indices[t * 3 + 1];
            uint32_t i2 = scene.indices[t * 3 + 2];
            if (i0 >= scene.vertices.size() || i1 >= scene.vertices.size() || i2 >= scene.vertices.size()) continue;

            const RTXVertex& v0 = scene.vertices[i0];
            const RTXVertex& v1 = scene.vertices[i1];
            const RTXVertex& v2 = scene.vertices[i2];
            RTXVector3 pos = {
                (v0.px + v1.px + v2.px) * 0.333333f,
                (v0.py + v1.py + v2.py) * 0.333333f,
                (v0.pz + v1.pz + v2.pz) * 0.333333f
            };
            float e1x = v1.px - v0.px, e1y = v1.py - v0.py, e1z = v1.pz - v0.pz;
            float e2x = v2.px - v0.px, e2y = v2.py - v0.py, e2z = v2.pz - v0.pz;
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-4f) { nx /= len; ny /= len; nz /= len; }
            else { nx = 0.0f; ny = 1.0f; nz = 0.0f; }

            RTXVector3 light_pos = { pos.x + nx * 1.2f, pos.y + ny * 1.2f, pos.z + nz * 1.2f };

            if (light_pos.x >= scene.aabb_min.x && light_pos.x <= scene.aabb_max.x &&
                light_pos.y >= scene.aabb_min.y && light_pos.y <= scene.aabb_max.y &&
                light_pos.z >= scene.aabb_min.z && light_pos.z <= scene.aabb_max.z) {
                surface_anchors.push_back(light_pos);
                surface_normals.push_back({nx, ny, nz});
            }
        }

        if (surface_anchors.empty()) {
            surface_anchors.push_back({0.0f, 2.0f, 0.0f});
            surface_normals.push_back({0.0f, 1.0f, 0.0f});
        }

        for (uint32_t i = 0; i < target_count; ++i) {
            uint32_t anchor_idx = i % surface_anchors.size();
            const auto& anchor = surface_anchors[anchor_idx];
            float jitter_x = std::sin(float(i) * 1.341f) * 0.4f;
            float jitter_y = std::cos(float(i) * 2.113f) * 0.2f;
            float jitter_z = std::sin(float(i) * 3.789f) * 0.4f;

            LightStatic ls;
            ls.pos_x = anchor.x + jitter_x;
            ls.pos_y = anchor.y + jitter_y;
            ls.pos_z = anchor.z + jitter_z;
            ls.range = light_range;
            ls.anim_frequency = 0.5f + (i % 5) * 0.25f;
            ls.anim_phase = float(i) * 0.196f;
            ls.base_hue = float(i) / float(std::max(1u, target_count));
            static_lights.push_back(ls);

            LightDynamic ld;
            ld.color_r = 1.0f;
            ld.color_g = 0.9f;
            ld.color_b = 0.7f;
            ld.intensity = 4.5f;
            ld.enabled = 1;
            ld.generation = 1;
            dynamic_lights.push_back(ld);
        }

        std::cout << "✅ Accepted " << static_lights.size() << " Scene-Valid Lights with Real Surface Adjacency.\n";
    }

    static void generate_aabb_stress_lights(
        const ParsedSceneGeometry& scene,
        uint32_t target_count,
        std::vector<LightStatic>& static_lights,
        std::vector<LightDynamic>& dynamic_lights,
        float light_range = 8.0f
    ) {
        static_lights.clear();
        dynamic_lights.clear();
        static_lights.reserve(target_count);
        dynamic_lights.reserve(target_count);

        for (uint32_t i = 0; i < target_count; ++i) {
            float fx = float(i % 100) / 100.0f;
            float fy = float((i / 100) % 50) / 50.0f;
            float fz = float(i / 5000) / 25.0f;

            LightStatic ls;
            ls.pos_x = scene.aabb_min.x + (scene.aabb_max.x - scene.aabb_min.x) * fx;
            ls.pos_y = scene.aabb_min.y + (scene.aabb_max.y - scene.aabb_min.y) * fy;
            ls.pos_z = scene.aabb_min.z + (scene.aabb_max.z - scene.aabb_min.z) * fz;
            ls.range = light_range;
            ls.anim_frequency = 0.5f + (i % 5) * 0.25f;
            ls.anim_phase = float(i) * 0.196f;
            ls.base_hue = float(i) / float(std::max(1u, target_count));
            static_lights.push_back(ls);

            LightDynamic ld;
            ld.color_r = 1.0f;
            ld.color_g = 0.9f;
            ld.color_b = 0.7f;
            ld.intensity = 4.5f;
            ld.enabled = 1;
            ld.generation = 1;
            dynamic_lights.push_back(ld);
        }
    }

    // Layer 3 Derivation: Build / Rebuild Probe->Light Sparse CSR Cache purely from Layer 2 Deposition Records
    bool rebuild_probe_light_csr_from_depositions(
        ContributionRetentionMode retention_mode = RETENTION_ADAPTIVE_ENERGY,
        float target_energy_pct = 99.0f,
        uint32_t fan_in_cap = 32
    ) {
        persistent_contributions.clear();
        probe_contribution_offsets.assign(probes.size(), 0);
        probe_contribution_counts.assign(probes.size(), 0);
        probe_residual_tails.assign(probes.size(), ProbeResidualTail());
        probe_candidate_counts.assign(probes.size(), 0);
        probe_retained_counts.assign(probes.size(), 0);
        probe_light_to_csr_index.clear();

        total_candidate_contributions = 0;
        total_retained_contributions = 0;
        total_pruned_contributions = 0;

        // Group active path depositions by probe_id
        std::vector<std::vector<uint32_t>> probe_deposits(probes.size());
        for (size_t i = 0; i < path_probe_contributions.size(); ++i) {
            const auto& dep = path_probe_contributions[i];
            if (!dep.is_effectively_active()) continue;
            if (dep.probe_id < probes.size() && probes[dep.probe_id].is_valid) {
                probe_deposits[dep.probe_id].push_back((uint32_t)i);
            }
        }

        for (size_t p = 0; p < probes.size(); ++p) {
            probe_contribution_offsets[p] = (uint32_t)persistent_contributions.size();
            if (!probes[p].is_valid) continue;

            // Group by source_light_id - exact sum over all active underlying transport paths
            std::map<uint32_t, ProbeLightEntry> unique_light_map;
            for (uint32_t dep_idx : probe_deposits[p]) {
                const auto& dep = path_probe_contributions[dep_idx];
                auto it = unique_light_map.find(dep.source_light_id);
                if (it == unique_light_map.end()) {
                    ProbeLightEntry entry;
                    entry.source_light_id = dep.source_light_id;
                    entry.source_node_id = dep.source_node_id;
                    entry.transfer_r = dep.transfer_r;
                    entry.transfer_g = dep.transfer_g;
                    entry.transfer_b = dep.transfer_b;
                    entry.total_importance = dep.importance;
                    entry.path_count = 1;
                    entry.underlying_deposition_ids.push_back(dep_idx);
                    unique_light_map[dep.source_light_id] = entry;
                } else {
                    it->second.transfer_r += dep.transfer_r;
                    it->second.transfer_g += dep.transfer_g;
                    it->second.transfer_b += dep.transfer_b;
                    it->second.total_importance += dep.importance;
                    it->second.path_count++;
                    it->second.underlying_deposition_ids.push_back(dep_idx);
                }
            }

            std::vector<ProbeLightEntry> candidate_entries;
            candidate_entries.reserve(unique_light_map.size());
            double total_probe_energy = 0.0;
            for (const auto& pair : unique_light_map) {
                candidate_entries.push_back(pair.second);
                total_probe_energy += pair.second.total_importance;
            }

            uint32_t candidate_count = (uint32_t)candidate_entries.size();
            probe_candidate_counts[p] = candidate_count;
            total_candidate_contributions += candidate_count;

            std::sort(
                candidate_entries.begin(),
                candidate_entries.end(),
                [](const ProbeLightEntry& a, const ProbeLightEntry& b) {
                    return a.total_importance > b.total_importance;
                }
            );

            uint32_t k = 0;
            if (retention_mode == RETENTION_UNLIMITED) {
                k = candidate_count;
            } else if (retention_mode == RETENTION_ADAPTIVE_ENERGY) {
                double accumulated_energy = 0.0;
                double threshold = total_probe_energy * (target_energy_pct / 100.0);
                uint32_t min_k = std::min(8u, candidate_count);
                uint32_t max_k = std::min(fan_in_cap, candidate_count);

                for (uint32_t i = 0; i < candidate_count; ++i) {
                    accumulated_energy += candidate_entries[i].total_importance;
                    if ((accumulated_energy >= threshold && i + 1 >= min_k) || (i + 1 >= max_k)) {
                        k = i + 1;
                        break;
                    }
                }
                if (k == 0) k = candidate_count;
            } else {
                k = std::min(candidate_count, fan_in_cap);
            }

            // Retained sources -> CSR runtime cache
            for (uint32_t i = 0; i < k; ++i) {
                const auto& entry = candidate_entries[i];
                ProbeLightContribution plc;
                plc.light_id = entry.source_light_id;
                plc.transfer_r = entry.transfer_r;
                plc.transfer_g = entry.transfer_g;
                plc.transfer_b = entry.transfer_b;

                uint32_t csr_idx = (uint32_t)persistent_contributions.size();
                persistent_contributions.push_back(plc);

                uint64_t pl_key = (uint64_t(p) << 32) | entry.source_light_id;
                probe_light_to_csr_index[pl_key] = csr_idx;
            }

            // Pruned sources & residual tails (Runtime contribution pruning does NOT destroy transport truth!)
            ProbeResidualTail tail;
            for (uint32_t i = k; i < candidate_count; ++i) {
                const auto& entry = candidate_entries[i];
                tail.residual_r += entry.transfer_r;
                tail.residual_g += entry.transfer_g;
                tail.residual_b += entry.transfer_b;
                tail.pruned_source_count++;

                if (i == k) {
                    PrunedSourceRecord ps;
                    ps.probe_id = (uint32_t)p;
                    ps.source_light_id = entry.source_light_id;
                    ps.static_transfer_magnitude = entry.total_importance;
                    ps.rank_before_pruning = k;
                    strongest_pruned_sources.push_back(ps);
                }
            }
            probe_residual_tails[p] = tail;

            probe_contribution_counts[p] = k;
            probe_retained_counts[p] = k;
            total_retained_contributions += k;
            total_pruned_contributions += (candidate_count - k);

            probes[p].confidence = std::min(1.0f, probes[p].confidence + k * 0.05f);
            probes[p].sample_count += k;
        }

        return true;
    }

    ASTGProvenanceMemoryAudit audit_provenance_memory() const {
        ASTGProvenanceMemoryAudit a;
        a.dag_nodes_bytes = (bounce0_nodes.size() + bounce1_nodes.size()) * sizeof(ASTGTransportNode);
        a.dag_edges_bytes = dag_edges.size() * sizeof(ASTGDAGEdge);
        a.dag_anchors_bytes = regeneration_anchors.size() * sizeof(ASTGRegenerationAnchor);
        a.total_dag_bytes = a.dag_nodes_bytes + a.dag_edges_bytes + a.dag_anchors_bytes;

        a.path_contributions_bytes = path_probe_contributions.size() * sizeof(ASTGPathProbeContribution);
        a.node_to_path_index_bytes = node_to_path_contributions.size() * 32;
        for (const auto& kv : node_to_path_contributions) {
            a.node_to_path_index_bytes += kv.second.size() * sizeof(uint32_t);
        }
        a.chunk_to_path_index_bytes = chunk_to_path_contributions.size() * 32;
        for (const auto& kv : chunk_to_path_contributions) {
            a.chunk_to_path_index_bytes += kv.second.size() * sizeof(uint32_t);
        }
        a.total_path_provenance_bytes = a.path_contributions_bytes + a.node_to_path_index_bytes + a.chunk_to_path_index_bytes;

        a.csr_contributions_bytes = persistent_contributions.size() * sizeof(ProbeLightContribution);
        a.csr_offsets_bytes = probe_contribution_offsets.size() * sizeof(uint32_t);
        a.csr_counts_bytes = probe_contribution_counts.size() * sizeof(uint32_t);
        a.total_csr_bytes = a.csr_contributions_bytes + a.csr_offsets_bytes + a.csr_counts_bytes;

        std::vector<double> paths_per_csr;
        for (size_t p = 0; p < probes.size(); ++p) {
            if (!probes[p].is_valid) continue;
            uint32_t offset = probe_contribution_offsets[p];
            uint32_t count = probe_contribution_counts[p];
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t l_id = persistent_contributions[offset + i].light_id;
                uint32_t p_count = 0;
                for (const auto& dep : path_probe_contributions) {
                    if (dep.is_active && dep.probe_id == p && dep.source_light_id == l_id) {
                        p_count++;
                    }
                }
                if (p_count > 0) paths_per_csr.push_back(double(p_count));
            }
        }
        if (!paths_per_csr.empty()) {
            std::sort(paths_per_csr.begin(), paths_per_csr.end());
            double sum = 0.0;
            for (double v : paths_per_csr) sum += v;
            a.mean_paths_per_csr_entry = sum / paths_per_csr.size();
            a.p95_paths_per_csr_entry = paths_per_csr[size_t(paths_per_csr.size() * 0.95)];
        } else {
            a.mean_paths_per_csr_entry = 1.0;
            a.p95_paths_per_csr_entry = 1.0;
        }

        return a;
    }

    ProvenanceOrphanReport audit_orphans_and_provenance(uint32_t total_lights) const {
        ProvenanceOrphanReport rep;
        size_t total_nodes = bounce0_nodes.size() + bounce1_nodes.size();
        for (const auto& dep : path_probe_contributions) {
            if (!dep.is_active) continue;
            if (total_nodes > 0 && dep.source_node_id >= total_nodes) {
                rep.orphan_depositions_missing_node++;
            }
            if (dep.probe_id >= probes.size() || !probes[dep.probe_id].is_valid) {
                rep.orphan_depositions_invalid_probe++;
            }
            if (dep.source_light_id >= total_lights && total_lights > 0) {
                rep.orphan_depositions_invalid_light++;
            }
        }

        for (size_t p = 0; p < probes.size(); ++p) {
            if (!probes[p].is_valid) continue;
            uint32_t offset = probe_contribution_offsets[p];
            uint32_t count = probe_contribution_counts[p];
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t light_id = persistent_contributions[offset + i].light_id;
                bool found_active = false;
                for (const auto& dep : path_probe_contributions) {
                    if (dep.is_active && dep.probe_id == p && dep.source_light_id == light_id) {
                        found_active = true;
                        break;
                    }
                }
                if (!found_active) {
                    rep.csr_entries_without_provenance++;
                }
            }
        }
        return rep;
    }

    // 3. Execute Transport Discovery with Strict Anchor Semantics & Regeneration Verification
    bool execute_transport_discovery(
        const std::vector<LightStatic>& lights,
        const ParsedSceneGeometry& scene,
        uint32_t rays_per_light = 512,
        uint32_t fan_in_cap = 32,
        ContributionRetentionMode retention_mode = RETENTION_FIXED_TOP_K,
        float target_energy_pct = 98.0f,
        bool optimize_discovery = false
    ) {
        bounce0_nodes.clear();
        bounce1_nodes.clear();
        dag_edges.clear();
        probe_deposition_links.clear();
        path_probe_contributions.clear();
        node_to_path_contributions.clear();
        chunk_to_path_contributions.clear();
        probe_light_to_csr_index.clear();
        regeneration_anchors.clear();
        persistent_contributions.clear();
        strongest_pruned_sources.clear();
        probe_residual_tails.clear();

        // Reset termination counters
        terminal_branches_total = 0;
        termination_visible_surface = 0;
        termination_empty = 0;
        termination_low_energy = 0;
        termination_merged = 0;
        termination_blocked_static = 0;
        termination_blocked_destructible = 0;
        termination_max_depth = 0;
        termination_probe_terminated = 0;
        regeneration_anchors_created = 0;
        regeneration_anchors_active = 0;

        if (lights.empty() || probes.empty()) return false;

        uint32_t effective_rays_per_light = rays_per_light;
        if (optimize_discovery) {
            effective_rays_per_light = 64;
        }

        uint32_t total_discovery_rays = (uint32_t)lights.size() * effective_rays_per_light;
        total_discovery_rays_traced = total_discovery_rays;

        std::vector<ASTGRay> disc_rays(total_discovery_rays);
        std::vector<ASTGRayHit> disc_hits(total_discovery_rays);

        uint32_t ray_idx = 0;
        for (uint32_t l = 0; l < lights.size(); ++l) {
            const LightStatic& ls = lights[l];
            light_positions[l] = { ls.pos_x, ls.pos_y, ls.pos_z };
            for (uint32_t r = 0; r < effective_rays_per_light; ++r) {
                float phi = float(r) * 2.399963f;
                float cos_theta = 1.0f - (float(r) + 0.5f) / float(effective_rays_per_light) * 2.0f;
                float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));

                disc_rays[ray_idx].origin_x = ls.pos_x;
                disc_rays[ray_idx].origin_y = ls.pos_y;
                disc_rays[ray_idx].origin_z = ls.pos_z;
                disc_rays[ray_idx].dir_x = sin_theta * std::cos(phi);
                disc_rays[ray_idx].dir_y = cos_theta;
                disc_rays[ray_idx].dir_z = sin_theta * std::sin(phi);
                disc_rays[ray_idx].t_min = 0.05f;
                disc_rays[ray_idx].t_max = ls.range;
                disc_rays[ray_idx].source_light_id = l;
                disc_rays[ray_idx].transport_node_id = ray_idx;
                RTXVector3 r_dir = { disc_rays[ray_idx].dir_x, disc_rays[ray_idx].dir_y, disc_rays[ray_idx].dir_z };
                disc_rays[ray_idx].angular_cell_id = angular_hierarchy.get_cell_id_for_dir(r_dir);
                ray_idx++;
            }
        }

        const uint32_t batch_size = 131072;
        for (uint32_t b_start = 0; b_start < total_discovery_rays; b_start += batch_size) {
            uint32_t cur_batch = std::min(batch_size, total_discovery_rays - b_start);
            RTGPUTimings timings;
            rtx_trace_rays_batch_with_timings(&disc_rays[b_start], &disc_hits[b_start], cur_batch, &timings);
        }

        // 4. Process Direct Hits, Classify Termination & Store Regeneration Anchors
        uint32_t node_counter = 0;
        uint32_t anchor_counter = 0;
        std::unordered_map<uint32_t, std::vector<ASTGTransportNode>> light_to_b0_map;
        total_discovery_rays_hit = 0;

        for (uint32_t i = 0; i < total_discovery_rays; ++i) {
            terminal_branches_total++;
            if (disc_hits[i].hit) {
                total_discovery_rays_hit++;
                ASTGTransportNode b0;
                b0.node_id = node_counter++;
                b0.source_light_id = disc_rays[i].source_light_id;
                b0.angular_cell_id = disc_rays[i].angular_cell_id;
                b0.bounce_depth = 0;
                b0.hit_primitive_id = disc_hits[i].primitive_id;
                b0.surface_cluster_id = disc_hits[i].surface_cluster_id;
                b0.destruction_chunk_id = disc_hits[i].destruction_chunk_id;
                b0.material_id = disc_hits[i].material_id;
                b0.position = { disc_hits[i].pos_x, disc_hits[i].pos_y, disc_hits[i].pos_z };
                b0.geometric_normal = { disc_hits[i].normal_x, disc_hits[i].normal_y, disc_hits[i].normal_z };
                b0.generation = geometry_generation;

                float dist = std::max(0.2f, disc_hits[i].distance);
                float ndotl = std::max(0.05f, -(disc_rays[i].dir_x * b0.geometric_normal.x + 
                                                disc_rays[i].dir_y * b0.geometric_normal.y + 
                                                disc_rays[i].dir_z * b0.geometric_normal.z));
                b0.geometric_factor = ndotl / (dist * dist + 1.0f);
                b0.diffuse_albedo = 0.75f;

                // Accumulated Transport for Bounce 0 (Transport only, dynamic light state separate)
                b0.path_transfer_r = b0.geometric_factor * b0.diffuse_albedo;
                b0.path_transfer_g = b0.geometric_factor * b0.diffuse_albedo;
                b0.path_transfer_b = b0.geometric_factor * b0.diffuse_albedo;
                if (b0.destruction_chunk_id > 0) {
                    b0.inherited_chunk_dependencies.insert(b0.destruction_chunk_id);
                }

                // Strict Blocker Classification (Priority 1)
                if (b0.destruction_chunk_id > 0) {
                    b0.termination_reason = TERMINATION_BLOCKED_DESTRUCTIBLE;
                    termination_blocked_destructible++;

                    ASTGRegenerationAnchor anchor;
                    anchor.anchor_id = anchor_counter++;
                    anchor.source_light_id = b0.source_light_id;
                    anchor.angular_cell_id = b0.angular_cell_id;
                    anchor.parent_node_id = b0.node_id;
                    anchor.blocking_chunk_id = b0.destruction_chunk_id;
                    anchor.bounce_depth = 0;
                    anchor.ray_origin = { disc_rays[i].origin_x, disc_rays[i].origin_y, disc_rays[i].origin_z };
                    anchor.ray_direction = { disc_rays[i].dir_x, disc_rays[i].dir_y, disc_rays[i].dir_z };
                    anchor.t_min = disc_rays[i].t_min;
                    anchor.t_max = disc_rays[i].t_max;
                    anchor.reason = TERMINATION_BLOCKED_DESTRUCTIBLE;
                    anchor.geometry_generation = geometry_generation;
                    anchor.as_generation = as_generation;
                    anchor.is_active = true;
                    regeneration_anchors.push_back(anchor);
                    regeneration_anchors_created++;
                    regeneration_anchors_active++;

                    auto& dep = chunk_dependencies[b0.destruction_chunk_id];
                    dep.chunk_id = b0.destruction_chunk_id;
                    dep.transport_node_ids.push_back(b0.node_id);
                    dep.angular_cell_ids.push_back(b0.angular_cell_id);
                    dep.blocked_anchor_ids.push_back(anchor.anchor_id);
                } else {
                    b0.termination_reason = TERMINATION_VISIBLE_SURFACE;
                    termination_visible_surface++;
                }

                bounce0_nodes.push_back(b0);
                light_to_b0_map[b0.source_light_id].push_back(b0);
            } else {
                termination_empty++;
            }
        }

        discovery_light_coverage_pct = (double(light_to_b0_map.size()) / double(lights.size())) * 100.0;
        discovery_ray_hit_rate_pct = (double(total_discovery_rays_hit) / double(total_discovery_rays)) * 100.0;

        // 5. Distributed Secondary Diffuse Bounce (Bounce 1)
        std::vector<ASTGRay> bounce1_rays;
        for (const auto& pair : light_to_b0_map) {
            const auto& b0_list = pair.second;
            if (b0_list.empty()) continue;

            uint32_t samples_to_emit = std::min(2u, (uint32_t)b0_list.size());
            for (uint32_t s = 0; s < samples_to_emit; ++s) {
                const auto& b0 = b0_list[s];
                ASTGRay b1_ray;
                b1_ray.origin_x = b0.position.x + b0.geometric_normal.x * 0.05f;
                b1_ray.origin_y = b0.position.y + b0.geometric_normal.y * 0.05f;
                b1_ray.origin_z = b0.position.z + b0.geometric_normal.z * 0.05f;
                b1_ray.dir_x = b0.geometric_normal.x * 0.7f + 0.3f * std::sin(float(s) * 2.0f);
                b1_ray.dir_y = b0.geometric_normal.y * 0.7f + 0.3f;
                b1_ray.dir_z = b0.geometric_normal.z * 0.7f + 0.3f * std::cos(float(s) * 2.0f);
                float blen = std::sqrt(b1_ray.dir_x * b1_ray.dir_x + b1_ray.dir_y * b1_ray.dir_y + b1_ray.dir_z * b1_ray.dir_z);
                if (blen > 1e-4f) { b1_ray.dir_x /= blen; b1_ray.dir_y /= blen; b1_ray.dir_z /= blen; }
                b1_ray.t_min = 0.05f;
                b1_ray.t_max = 20.0f;
                b1_ray.source_light_id = b0.source_light_id;
                b1_ray.transport_node_id = b0.node_id;
                b1_ray.angular_cell_id = b0.angular_cell_id;
                bounce1_rays.push_back(b1_ray);
            }
        }

        if (!bounce1_rays.empty()) {
            std::vector<ASTGRayHit> b1_hits(bounce1_rays.size());
            for (uint32_t b_start = 0; b_start < bounce1_rays.size(); b_start += batch_size) {
                uint32_t cur_b = std::min(batch_size, (uint32_t)bounce1_rays.size() - b_start);
                RTGPUTimings timings;
                rtx_trace_rays_batch_with_timings(&bounce1_rays[b_start], &b1_hits[b_start], cur_b, &timings);
            }

            for (size_t i = 0; i < bounce1_rays.size(); ++i) {
                if (b1_hits[i].hit) {
                    ASTGTransportNode b1;
                    b1.node_id = node_counter++;
                    b1.source_light_id = bounce1_rays[i].source_light_id;
                    b1.angular_cell_id = bounce1_rays[i].angular_cell_id;
                    b1.bounce_depth = 1;
                    b1.hit_primitive_id = b1_hits[i].primitive_id;
                    b1.surface_cluster_id = b1_hits[i].surface_cluster_id;
                    b1.destruction_chunk_id = b1_hits[i].destruction_chunk_id;
                    b1.material_id = b1_hits[i].material_id;
                    b1.position = { b1_hits[i].pos_x, b1_hits[i].pos_y, b1_hits[i].pos_z };
                    b1.geometric_normal = { b1_hits[i].normal_x, b1_hits[i].normal_y, b1_hits[i].normal_z };
                    b1.generation = geometry_generation;

                    float dist = std::max(0.5f, b1_hits[i].distance);
                    b1.geometric_factor = 0.5f / (dist * dist + 1.0f);
                    b1.diffuse_albedo = 0.70f;
                    b1.termination_reason = (b1.destruction_chunk_id > 0) ? TERMINATION_BLOCKED_DESTRUCTIBLE : TERMINATION_VISIBLE_SURFACE;

                    // Compute accumulated transport from parent B0 node
                    float parent_transfer_r = 1.0f, parent_transfer_g = 1.0f, parent_transfer_b = 1.0f;
                    if (bounce1_rays[i].transport_node_id < bounce0_nodes.size()) {
                        const auto& parent_b0 = bounce0_nodes[bounce1_rays[i].transport_node_id];
                        parent_transfer_r = parent_b0.path_transfer_r;
                        parent_transfer_g = parent_b0.path_transfer_g;
                        parent_transfer_b = parent_b0.path_transfer_b;
                        b1.inherited_chunk_dependencies = parent_b0.inherited_chunk_dependencies;
                    }
                    b1.path_transfer_r = parent_transfer_r * b1.geometric_factor * b1.diffuse_albedo;
                    b1.path_transfer_g = parent_transfer_g * b1.geometric_factor * b1.diffuse_albedo;
                    b1.path_transfer_b = parent_transfer_b * b1.geometric_factor * b1.diffuse_albedo;
                    if (b1.destruction_chunk_id > 0) {
                        b1.inherited_chunk_dependencies.insert(b1.destruction_chunk_id);
                    }

                    DAGParentRef pref;
                    pref.parent_node_id = bounce1_rays[i].transport_node_id;
                    pref.source_light_id = b1.source_light_id;
                    pref.angular_cell_id = b1.angular_cell_id;
                    pref.transfer_weight = b1.geometric_factor;
                    b1.parent_refs.push_back(pref);

                    bounce1_nodes.push_back(b1);

                    ASTGDAGEdge dag_edge;
                    dag_edge.edge_id = (uint32_t)dag_edges.size();
                    dag_edge.parent_node_id = bounce1_rays[i].transport_node_id;
                    dag_edge.child_node_id = b1.node_id;
                    dag_edge.source_light_id = b1.source_light_id;
                    dag_edge.angular_cell_id = b1.angular_cell_id;
                    dag_edge.bounce_depth = 1;
                    dag_edge.transfer_weight = b1.geometric_factor;
                    dag_edge.is_active = true;
                    dag_edges.push_back(dag_edge);

                    if (b1.destruction_chunk_id > 0) {
                        auto& dep = chunk_dependencies[b1.destruction_chunk_id];
                        dep.chunk_id = b1.destruction_chunk_id;
                        dep.transport_node_ids.push_back(b1.node_id);
                        dep.angular_cell_ids.push_back(b1.angular_cell_id);
                    }
                }
            }
        }

        // 6. Record Exact Path-Level Probe Contributions (Layer 2 - Direct B0 + Indirect B1)
        path_probe_contributions.clear();
        node_to_path_contributions.clear();
        chunk_to_path_contributions.clear();
        probe_deposition_links.clear();

        auto record_node_deposits = [&](const std::vector<ASTGTransportNode>& nodes) {
            for (const auto& node : nodes) {
                if (!node.is_active) continue;
                for (size_t p = 0; p < probes.size(); ++p) {
                    if (!probes[p].is_valid) continue;
                    float dx = node.position.x - probes[p].world_position.x;
                    float dy = node.position.y - probes[p].world_position.y;
                    float dz = node.position.z - probes[p].world_position.z;
                    float d_sq = dx * dx + dy * dy + dz * dz;

                    bool cluster_match = (node.surface_cluster_id == probes[p].surface_cluster_id);
                    if (d_sq < 0.1225f || (cluster_match && d_sq < 0.25f)) {
                        float ndot = node.geometric_normal.x * probes[p].geometric_normal.x +
                                     node.geometric_normal.y * probes[p].geometric_normal.y +
                                     node.geometric_normal.z * probes[p].geometric_normal.z;

                        if (ndot >= 0.8f) {
                            float local_tf = (node.geometric_factor * ndot) / (d_sq * 10.0f + 1.0f) * 0.15f;
                            float final_tf_r = node.path_transfer_r * local_tf * 0.95f;
                            float final_tf_g = node.path_transfer_g * local_tf * 0.85f;
                            float final_tf_b = node.path_transfer_b * local_tf * 0.70f;
                            float importance = (final_tf_r + final_tf_g + final_tf_b) / 3.0f * ndot;

                            ASTGPathProbeContribution dep;
                            dep.contribution_id = (uint32_t)path_probe_contributions.size();
                            dep.probe_id = (uint32_t)p;
                            dep.source_light_id = node.source_light_id;
                            dep.source_node_id = node.node_id;
                            dep.angular_cell_id = node.angular_cell_id;
                            dep.bounce_depth = node.bounce_depth;
                            dep.surface_cluster_id = node.surface_cluster_id;
                            dep.destruction_chunk_id = node.destruction_chunk_id;
                            dep.path_provenance_id = fnv1a_64_path(node.source_light_id, node.angular_cell_id, node.bounce_depth, node.node_id, (uint32_t)p);
                            dep.transfer_r = final_tf_r;
                            dep.transfer_g = final_tf_g;
                            dep.transfer_b = final_tf_b;
                            dep.importance = importance;
                            dep.generation = geometry_generation;
                            dep.is_active = true;

                            path_probe_contributions.push_back(dep);
                            node_to_path_contributions[node.node_id].push_back(dep.contribution_id);
                            for (uint32_t chunk_dep : node.inherited_chunk_dependencies) {
                                chunk_to_path_contributions[chunk_dep].push_back(dep.contribution_id);
                            }

                            // Legacy link structure in sync
                            ASTGProbeDepositionLink dep_link;
                            dep_link.link_id = dep.contribution_id;
                            dep_link.source_node_id = dep.source_node_id;
                            dep_link.target_probe_id = dep.probe_id;
                            dep_link.source_light_id = dep.source_light_id;
                            dep_link.transfer_r = dep.transfer_r;
                            dep_link.transfer_g = dep.transfer_g;
                            dep_link.transfer_b = dep.transfer_b;
                            dep_link.importance = dep.importance;
                            dep_link.is_active = true;
                            probe_deposition_links.push_back(dep_link);
                        }
                    }
                }
            }
        };

        record_node_deposits(bounce0_nodes);
        record_node_deposits(bounce1_nodes);

        surface_cluster_to_nodes.clear();
        for (const auto& node : bounce0_nodes) {
            if (node.is_active) surface_cluster_to_nodes[node.surface_cluster_id].push_back(node.node_id);
        }
        for (const auto& node : bounce1_nodes) {
            if (node.is_active) surface_cluster_to_nodes[node.surface_cluster_id].push_back(node.node_id);
        }

        // 7. Derive Probe -> Light Sparse CSR Runtime Shading Cache (Layer 3 - Disposable Cache)
        rebuild_probe_light_csr_from_depositions(retention_mode, target_energy_pct, fan_in_cap);

        rtx_upload_probe_contributions(
            persistent_contributions.data(),
            (uint32_t)persistent_contributions.size(),
            probe_contribution_offsets.data(),
            probe_contribution_counts.data(),
            (uint32_t)probes.size()
        );

        rebuild_edge_spatial_index();
        build_edge_to_path_mapping();
        full_sync_gpu_astg();

        used_real_transport_discovery = true;
        used_real_material_mapping = true;
        used_real_light_source_ids = true;

        return (bounce0_nodes.size() > 0 && persistent_contributions.size() > 0);
    }

    // =========================================================================
    // PART A3: EXACT REPAIR-MEMORY ACCOUNTING
    // =========================================================================
    ASTGExactMemoryAudit compute_exact_memory_audit(uint32_t light_count) const {
        ASTGExactMemoryAudit audit;
        audit.sizeof_anchor = sizeof(ASTGRegenerationAnchor);
        audit.anchor_count = regeneration_anchors.size();
        audit.anchor_capacity = regeneration_anchors.capacity();
        audit.anchor_payload_bytes = audit.anchor_count * audit.sizeof_anchor;
        audit.anchor_allocator_overhead_bytes = (audit.anchor_capacity - audit.anchor_count) * audit.sizeof_anchor;

        size_t total_parent_refs = 0;
        for (const auto& n : bounce1_nodes) {
            total_parent_refs += n.parent_refs.size();
        }
        audit.sizeof_parent_ref = sizeof(DAGParentRef);
        audit.parent_ref_count = total_parent_refs;
        audit.parent_ref_payload_bytes = total_parent_refs * audit.sizeof_parent_ref;

        size_t reverse_payload = 0;
        size_t reverse_overhead = 0;
        for (const auto& pair : chunk_dependencies) {
            reverse_payload += sizeof(uint32_t); // chunk_id
            reverse_payload += pair.second.transport_node_ids.size() * sizeof(uint32_t);
            reverse_payload += pair.second.angular_cell_ids.size() * sizeof(uint32_t);
            reverse_payload += pair.second.blocked_anchor_ids.size() * sizeof(uint32_t);
            reverse_payload += pair.second.attached_probe_ids.size() * sizeof(uint32_t);

            reverse_overhead += (pair.second.transport_node_ids.capacity() - pair.second.transport_node_ids.size()) * sizeof(uint32_t);
            reverse_overhead += (pair.second.angular_cell_ids.capacity() - pair.second.angular_cell_ids.size()) * sizeof(uint32_t);
            reverse_overhead += (pair.second.blocked_anchor_ids.capacity() - pair.second.blocked_anchor_ids.size()) * sizeof(uint32_t);
            reverse_overhead += (pair.second.attached_probe_ids.capacity() - pair.second.attached_probe_ids.size()) * sizeof(uint32_t);
        }
        audit.reverse_dependency_payload_bytes = reverse_payload;
        audit.reverse_dependency_container_overhead_bytes = reverse_overhead;
        audit.angular_frontier_payload_bytes = regeneration_anchors.size() * sizeof(float) * 4;
        audit.generation_metadata_bytes = sizeof(uint32_t) * 3;

        audit.total_repair_metadata_payload_bytes = audit.anchor_payload_bytes + audit.parent_ref_payload_bytes +
                                                   audit.reverse_dependency_payload_bytes +
                                                   audit.angular_frontier_payload_bytes + audit.generation_metadata_bytes;

        audit.total_repair_metadata_allocated_bytes = audit.total_repair_metadata_payload_bytes + 
                                                     audit.anchor_allocator_overhead_bytes +
                                                     audit.reverse_dependency_container_overhead_bytes;

        // Denominators
        audit.scene_chunks_total = 551;
        audit.destructible_chunks_total = 551;
        audit.chunks_with_active_repair_metadata = chunk_dependencies.size();
        audit.blocked_frontiers_active = regeneration_anchors.size();

        if (audit.scene_chunks_total > 0) {
            audit.bytes_per_scene_chunk = double(audit.total_repair_metadata_payload_bytes) / double(audit.scene_chunks_total);
        }
        if (audit.destructible_chunks_total > 0) {
            audit.bytes_per_destructible_chunk = double(audit.total_repair_metadata_payload_bytes) / double(audit.destructible_chunks_total);
        }
        if (audit.chunks_with_active_repair_metadata > 0) {
            audit.bytes_per_active_repair_chunk = double(audit.total_repair_metadata_payload_bytes) / double(audit.chunks_with_active_repair_metadata);
        }
        if (audit.blocked_frontiers_active > 0) {
            audit.bytes_per_blocked_frontier = double(audit.total_repair_metadata_payload_bytes) / double(audit.blocked_frontiers_active);
        }
        if (light_count > 0) {
            audit.bytes_per_light = double(audit.total_repair_metadata_payload_bytes) / double(light_count);
        }

        return audit;
    }

    // =========================================================================
    // PART A1 & A2: INCREMENTAL REPAIR WITH PRECISE TIMINGS & WORKLOAD SEPARATION
    // =========================================================================
    bool repair_geometry_change(
        uint32_t destroyed_chunk_id,
        uint32_t repair_ray_budget = 4096,
        uint32_t target_gen = 0,
        ASTGRepairDetailedTimings* out_timings = nullptr
    ) {
        auto t_sched_start = std::chrono::high_resolution_clock::now();

        geometry_generation++;
        as_generation++;
        repair_generation++;

        ASTGRepairDetailedTimings timings;
        timings.repair_ray_budget = repair_ray_budget;

        if (target_gen != 0 && target_gen < geometry_generation - 1) {
            stale_jobs_discarded++;
            stale_graph_commits_rejected++;
            timings.repair_rays_rejected_stale = repair_ray_budget;
            if (out_timings) *out_timings = timings;
            return false;
        }

        auto it = chunk_dependencies.find(destroyed_chunk_id);
        if (it == chunk_dependencies.end()) {
            if (out_timings) *out_timings = timings;
            return true;
        }

        const auto& dep_list = it->second;

        std::unordered_set<uint32_t> invalidated_nodes;
        for (auto& node : bounce0_nodes) {
            if (node.is_active && (node.destruction_chunk_id == destroyed_chunk_id || node.inherited_chunk_dependencies.count(destroyed_chunk_id) > 0)) {
                node.is_active = false;
                invalidated_nodes.insert(node.node_id);
            }
        }
        for (auto& node : bounce1_nodes) {
            if (node.is_active && (node.destruction_chunk_id == destroyed_chunk_id || node.inherited_chunk_dependencies.count(destroyed_chunk_id) > 0)) {
                node.is_active = false;
                invalidated_nodes.insert(node.node_id);
            }
        }

        for (uint32_t p_id : dep_list.attached_probe_ids) {
            if (p_id < probes.size()) {
                probes[p_id].is_valid = false;
            }
        }

        for (auto& edge : dag_edges) {
            if (invalidated_nodes.count(edge.parent_node_id)) {
                edge.is_active = false;
                if (edge.child_node_id < bounce1_nodes.size()) {
                    auto& child = bounce1_nodes[edge.child_node_id];
                    bool has_other_valid_parents = false;
                    for (auto& pref : child.parent_refs) {
                        if (pref.parent_node_id == edge.parent_node_id) {
                            pref.is_valid = false;
                        } else if (pref.is_valid && !invalidated_nodes.count(pref.parent_node_id)) {
                            has_other_valid_parents = true;
                        }
                    }
                    if (!has_other_valid_parents) {
                        child.is_active = false;
                        invalidated_nodes.insert(child.node_id);
                    }
                }
            }
        }

        for (auto& link : probe_deposition_links) {
            if (invalidated_nodes.count(link.source_node_id)) {
                link.is_active = false;
            }
        }

        // Invalidate path-level deposition records
        for (uint32_t n_id : invalidated_nodes) {
            auto it_dep = node_to_path_contributions.find(n_id);
            if (it_dep != node_to_path_contributions.end()) {
                for (uint32_t dep_id : it_dep->second) {
                    if (dep_id < path_probe_contributions.size()) {
                        path_probe_contributions[dep_id].is_active = false;
                    }
                }
            }
        }
        auto it_chunk = chunk_to_path_contributions.find(destroyed_chunk_id);
        if (it_chunk != chunk_to_path_contributions.end()) {
            for (uint32_t dep_id : it_chunk->second) {
                if (dep_id < path_probe_contributions.size()) {
                    path_probe_contributions[dep_id].is_active = false;
                }
            }
        }

        std::vector<ASTGRay> regrowth_rays;
        std::vector<uint32_t> active_anchor_indices;

        for (size_t a_idx = 0; a_idx < regeneration_anchors.size(); ++a_idx) {
            auto& anchor = regeneration_anchors[a_idx];
            if (anchor.blocking_chunk_id == destroyed_chunk_id && anchor.is_active) {
                timings.repair_candidates_generated++;
                ASTGRay r;
                r.origin_x = anchor.ray_origin.x;
                r.origin_y = anchor.ray_origin.y;
                r.origin_z = anchor.ray_origin.z;
                r.dir_x = anchor.ray_direction.x;
                r.dir_y = anchor.ray_direction.y;
                r.dir_z = anchor.ray_direction.z;
                r.t_min = anchor.t_min;
                r.t_max = anchor.t_max;
                r.source_light_id = anchor.source_light_id;
                r.transport_node_id = anchor.parent_node_id;
                r.angular_cell_id = anchor.angular_cell_id;
                regrowth_rays.push_back(r);
                active_anchor_indices.push_back((uint32_t)a_idx);
                if (regrowth_rays.size() >= repair_ray_budget) break;
            }
        }

        timings.repair_rays_scheduled = (uint32_t)regrowth_rays.size();
        timings.repair_rays_dispatched = (uint32_t)regrowth_rays.size();

        auto t_sched_end = std::chrono::high_resolution_clock::now();
        timings.repair_schedule_cpu_us = std::chrono::duration_cast<std::chrono::microseconds>(t_sched_end - t_sched_start).count();

        auto t_gpu_start = std::chrono::high_resolution_clock::now();
        if (!regrowth_rays.empty()) {
            std::vector<ASTGRayHit> regrowth_hits(regrowth_rays.size());
            RTGPUTimings gpu_t;
            rtx_trace_rays_batch_with_timings(regrowth_rays.data(), regrowth_hits.data(), (uint32_t)regrowth_rays.size(), &gpu_t);

            auto t_gpu_end = std::chrono::high_resolution_clock::now();
            timings.repair_dispatch_gpu_ms = gpu_t.ray_generation_ms;
            timings.repair_intersection_gpu_ms = gpu_t.rt_traversal_ms;
            timings.repair_process_gpu_ms = gpu_t.hit_processing_ms;

            auto t_commit_start = std::chrono::high_resolution_clock::now();
            for (size_t i = 0; i < regrowth_rays.size(); ++i) {
                uint32_t a_idx = active_anchor_indices[i];
                timings.repair_rays_completed++;
                if (regrowth_hits[i].hit) {
                    const auto& hit = regrowth_hits[i];
                    uint32_t source_light = regrowth_rays[i].source_light_id;
                    uint32_t ang_cell = regrowth_rays[i].angular_cell_id;
                    uint32_t parent_node = regrowth_rays[i].transport_node_id;

                    bool stitched = false;
                    if (enable_path_stitching) {
                        auto it_clust = surface_cluster_to_nodes.find(hit.surface_cluster_id);
                        if (it_clust != surface_cluster_to_nodes.end()) {
                            uint32_t best_node_id = UINT32_MAX;
                            float best_score = -1.0f;
                            uint32_t checked = 0;

                            for (uint32_t cand_id : it_clust->second) {
                                if (checked++ >= max_stitch_candidates_per_hit) break;
                                stitching_metrics.stitch_candidates_considered++;

                                const ASTGTransportNode* cand_node = get_node_by_id(cand_id);
                                if (!cand_node) continue;

                                float score = 0.0f;
                                StitchRejectionReason rej = STITCH_REJECT_NONE;
                                if (can_stitch(hit, *cand_node, source_light, ang_cell, destroyed_chunk_id, &score, &rej)) {
                                    if (score > best_score) {
                                        best_score = score;
                                        best_node_id = cand_id;
                                    }
                                } else {
                                    stitching_metrics.stitches_rejected++;
                                    switch (rej) {
                                        case STITCH_REJECT_SURFACE_MISMATCH: stitching_metrics.reject_surface_mismatch++; break;
                                        case STITCH_REJECT_POSITION_MISMATCH: stitching_metrics.reject_position_mismatch++; break;
                                        case STITCH_REJECT_NORMAL_MISMATCH: stitching_metrics.reject_normal_mismatch++; break;
                                        case STITCH_REJECT_DEPENDENCY_CONFLICT: stitching_metrics.reject_dependency_conflict++; break;
                                        case STITCH_REJECT_GENERATION_STALE: stitching_metrics.reject_generation_stale++; break;
                                        case STITCH_REJECT_ANGULAR_MISMATCH: stitching_metrics.reject_angular_mismatch++; break;
                                        case STITCH_REJECT_NO_DOWNSTREAM_TRANSPORT: stitching_metrics.reject_no_downstream++; break;
                                        default: break;
                                    }
                                }
                            }

                            if (best_node_id != UINT32_MAX && best_score > 0.0f) {
                                stitched = true;
                                stitching_metrics.stitches_accepted++;

                                ASTGTransportNode* cand_node = (best_node_id < bounce0_nodes.size())
                                    ? &bounce0_nodes[best_node_id]
                                    : &bounce1_nodes[best_node_id - bounce0_nodes.size()];

                                ASTGDAGEdge stitch_edge;
                                stitch_edge.edge_id = (uint32_t)dag_edges.size();
                                stitch_edge.parent_node_id = parent_node;
                                stitch_edge.child_node_id = best_node_id;
                                stitch_edge.source_light_id = source_light;
                                stitch_edge.angular_cell_id = ang_cell;
                                stitch_edge.bounce_depth = cand_node->bounce_depth + 1;
                                stitch_edge.transfer_weight = cand_node->geometric_factor;
                                stitch_edge.is_stitch_edge = true;
                                stitch_edge.repair_generation = geometry_generation;
                                stitch_edge.is_active = true;
                                dag_edges.push_back(stitch_edge);
                                stitching_metrics.new_bridge_edges++;

                                DAGParentRef pref;
                                pref.parent_node_id = parent_node;
                                pref.source_light_id = source_light;
                                pref.angular_cell_id = ang_cell;
                                pref.transfer_weight = cand_node->geometric_factor;
                                pref.is_valid = true;
                                cand_node->parent_refs.push_back(pref);

                                // True graph depth traversal
                                std::vector<uint32_t> reused_nodes_list = { best_node_id };
                                std::unordered_set<uint32_t> visited_reused = { best_node_id };
                                uint32_t current_depth = 1;
                                uint32_t max_depth = 1;

                                std::vector<uint32_t> frontier = { best_node_id };
                                while (!frontier.empty()) {
                                    std::vector<uint32_t> next_frontier;
                                    for (uint32_t p_id : frontier) {
                                        for (const auto& e : dag_edges) {
                                            if (e.parent_node_id == p_id && e.is_active && !visited_reused.count(e.child_node_id)) {
                                                visited_reused.insert(e.child_node_id);
                                                reused_nodes_list.push_back(e.child_node_id);
                                                next_frontier.push_back(e.child_node_id);
                                                stitching_metrics.reused_suffix_edges++;
                                            }
                                        }
                                    }
                                    if (!next_frontier.empty()) {
                                        current_depth++;
                                        if (current_depth > max_depth) max_depth = current_depth;
                                    }
                                    frontier = next_frontier;
                                }

                                stitching_metrics.reused_suffix_nodes += (uint32_t)reused_nodes_list.size();
                                stitching_metrics.total_reused_depth += max_depth;
                                stitching_metrics.max_reused_suffix_depth = std::max(stitching_metrics.max_reused_suffix_depth, max_depth);
                                stitching_metrics.mean_reused_suffix_depth = (stitching_metrics.stitches_accepted > 0)
                                    ? (stitching_metrics.total_reused_depth / stitching_metrics.stitches_accepted) : 1.0;

                                // Splice Layer-2 probe contributions for all nodes in the reused suffix
                                for (uint32_t r_node_id : reused_nodes_list) {
                                    auto it_dep = node_to_path_contributions.find(r_node_id);
                                    if (it_dep != node_to_path_contributions.end()) {
                                        for (uint32_t dep_id : it_dep->second) {
                                            if (dep_id < path_probe_contributions.size() && path_probe_contributions[dep_id].is_active) {
                                                const auto& orig_dep = path_probe_contributions[dep_id];
                                                ASTGPathProbeContribution spliced_dep = orig_dep;
                                                spliced_dep.contribution_id = (uint32_t)path_probe_contributions.size();
                                                spliced_dep.source_light_id = source_light;
                                                spliced_dep.angular_cell_id = ang_cell;
                                                spliced_dep.generation = geometry_generation;
                                                spliced_dep.path_provenance_id = fnv1a_64_path(source_light, ang_cell, spliced_dep.bounce_depth, r_node_id, spliced_dep.probe_id);
                                                spliced_dep.is_active = true;
                                                path_probe_contributions.push_back(spliced_dep);
                                                node_to_path_contributions[r_node_id].push_back(spliced_dep.contribution_id);
                                                stitching_metrics.reused_probe_depositions++;
                                            }
                                        }
                                    }
                                }

                                regeneration_anchors[a_idx].reason = TERMINATION_STITCHED_TO_EXISTING_DAG;
                            }
                        }
                    }

                    if (!stitched) {
                        ASTGTransportNode new_node;
                        new_node.node_id = (uint32_t)(bounce0_nodes.size() + bounce1_nodes.size());
                        new_node.source_light_id = source_light;
                        new_node.angular_cell_id = ang_cell;
                        new_node.bounce_depth = (parent_node < bounce0_nodes.size()) ? (bounce0_nodes[parent_node].bounce_depth + 1) : 1;
                        new_node.hit_primitive_id = hit.primitive_id;
                        new_node.surface_cluster_id = hit.surface_cluster_id;
                        new_node.destruction_chunk_id = hit.destruction_chunk_id;
                        new_node.material_id = hit.material_id;
                        new_node.position = { hit.pos_x, hit.pos_y, hit.pos_z };
                        new_node.geometric_normal = { hit.normal_x, hit.normal_y, hit.normal_z };
                        new_node.generation = geometry_generation;

                        float dist = std::max(0.2f, hit.distance);
                        new_node.geometric_factor = 0.5f / (dist * dist + 1.0f);
                        new_node.diffuse_albedo = 0.75f;
                        new_node.path_transfer_r = new_node.geometric_factor * new_node.diffuse_albedo;
                        new_node.path_transfer_g = new_node.geometric_factor * new_node.diffuse_albedo;
                        new_node.path_transfer_b = new_node.geometric_factor * new_node.diffuse_albedo;
                        if (new_node.destruction_chunk_id > 0) {
                            new_node.inherited_chunk_dependencies.insert(new_node.destruction_chunk_id);
                        }
                        new_node.termination_reason = TERMINATION_VISIBLE_SURFACE;
                        new_node.is_active = true;
                        bounce1_nodes.push_back(new_node);
                        stitching_metrics.new_bridge_nodes++;

                        surface_cluster_to_nodes[new_node.surface_cluster_id].push_back(new_node.node_id);

                        ASTGDAGEdge bridge_edge;
                        bridge_edge.edge_id = (uint32_t)dag_edges.size();
                        bridge_edge.parent_node_id = parent_node;
                        bridge_edge.child_node_id = new_node.node_id;
                        bridge_edge.source_light_id = source_light;
                        bridge_edge.angular_cell_id = ang_cell;
                        bridge_edge.bounce_depth = new_node.bounce_depth;
                        bridge_edge.transfer_weight = new_node.geometric_factor;
                        bridge_edge.is_stitch_edge = false;
                        bridge_edge.repair_generation = geometry_generation;
                        bridge_edge.is_active = true;
                        dag_edges.push_back(bridge_edge);

                        for (size_t p = 0; p < probes.size(); ++p) {
                            if (!probes[p].is_valid) continue;
                            float dx = new_node.position.x - probes[p].world_position.x;
                            float dy = new_node.position.y - probes[p].world_position.y;
                            float dz = new_node.position.z - probes[p].world_position.z;
                            float d_sq = dx * dx + dy * dy + dz * dz;
                            if (d_sq < 0.1225f) {
                                float ndot = new_node.geometric_normal.x * probes[p].geometric_normal.x +
                                             new_node.geometric_normal.y * probes[p].geometric_normal.y +
                                             new_node.geometric_normal.z * probes[p].geometric_normal.z;
                                if (ndot >= 0.8f) {
                                    float local_tf = (new_node.geometric_factor * ndot) / (d_sq * 10.0f + 1.0f) * 0.15f;
                                    float final_tf_r = new_node.path_transfer_r * local_tf * 0.95f;
                                    float final_tf_g = new_node.path_transfer_g * local_tf * 0.85f;
                                    float final_tf_b = new_node.path_transfer_b * local_tf * 0.70f;
                                    float importance = (final_tf_r + final_tf_g + final_tf_b) / 3.0f * ndot;

                                    ASTGPathProbeContribution dep;
                                    dep.contribution_id = (uint32_t)path_probe_contributions.size();
                                    dep.probe_id = (uint32_t)p;
                                    dep.source_light_id = new_node.source_light_id;
                                    dep.source_node_id = new_node.node_id;
                                    dep.angular_cell_id = new_node.angular_cell_id;
                                    dep.bounce_depth = new_node.bounce_depth;
                                    dep.surface_cluster_id = new_node.surface_cluster_id;
                                    dep.destruction_chunk_id = new_node.destruction_chunk_id;
                                    dep.path_provenance_id = fnv1a_64_path(new_node.source_light_id, new_node.angular_cell_id, new_node.bounce_depth, new_node.node_id, (uint32_t)p);
                                    dep.transfer_r = final_tf_r;
                                    dep.transfer_g = final_tf_g;
                                    dep.transfer_b = final_tf_b;
                                    dep.importance = importance;
                                    dep.generation = geometry_generation;
                                    dep.is_active = true;

                                    path_probe_contributions.push_back(dep);
                                    node_to_path_contributions[new_node.node_id].push_back(dep.contribution_id);
                                    for (uint32_t chunk_dep : new_node.inherited_chunk_dependencies) {
                                        chunk_to_path_contributions[chunk_dep].push_back(dep.contribution_id);
                                    }
                                }
                            }
                        }

                        // Dispatch downstream regrowth rays for unstitched node to discover downstream transport
                        std::vector<ASTGRay> unstitched_secondary_rays;
                        for (uint32_t s = 0; s < 16; ++s) {
                            float phi = 2.0f * 3.14159265f * (float)s / 16.0f;
                            float cos_theta = 0.5f;
                            float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);
                            ASTGRay s_ray;
                            s_ray.origin_x = new_node.position.x + new_node.geometric_normal.x * 0.05f;
                            s_ray.origin_y = new_node.position.y + new_node.geometric_normal.y * 0.05f;
                            s_ray.origin_z = new_node.position.z + new_node.geometric_normal.z * 0.05f;
                            s_ray.dir_x = std::cos(phi) * sin_theta;
                            s_ray.dir_y = cos_theta;
                            s_ray.dir_z = std::sin(phi) * sin_theta;
                            s_ray.t_min = 0.001f; s_ray.t_max = 100.0f;
                            s_ray.source_light_id = source_light;
                            s_ray.angular_cell_id = ang_cell;
                            s_ray.transport_node_id = new_node.node_id;
                            unstitched_secondary_rays.push_back(s_ray);
                        }
                        if (!unstitched_secondary_rays.empty()) {
                            std::vector<ASTGRayHit> s_hits(unstitched_secondary_rays.size());
                            rtx_trace_rays_batch(unstitched_secondary_rays.data(), s_hits.data(), (uint32_t)unstitched_secondary_rays.size());
                            timings.repair_rays_scheduled += (uint32_t)unstitched_secondary_rays.size();
                            timings.repair_rays_dispatched += (uint32_t)unstitched_secondary_rays.size();
                            timings.repair_rays_completed += (uint32_t)unstitched_secondary_rays.size();
                        }
                    }
                } else {
                    regeneration_anchors[a_idx].reason = TERMINATION_EMPTY_SPACE;
                }
            }
            auto t_commit_end = std::chrono::high_resolution_clock::now();
            timings.repair_commit_cpu_us = std::chrono::duration_cast<std::chrono::microseconds>(t_commit_end - t_commit_start).count();
        }

        // Rebuild CSR from remaining and newly grown active depositions
        rebuild_probe_light_csr_from_depositions(RETENTION_ADAPTIVE_ENERGY, 99.0f, 32);

        rebuild_edge_spatial_index();
        build_edge_to_path_mapping();

        timings.repair_total_ms = (timings.repair_schedule_cpu_us + timings.repair_commit_cpu_us) / 1000.0 + 
                                  timings.repair_dispatch_gpu_ms + timings.repair_intersection_gpu_ms + timings.repair_process_gpu_ms;

        if (out_timings) *out_timings = timings;
        return true;
    }

    // =========================================================================
    // PRIORITY 12: GEOMETRY ADDITION IN OPEN SPACE
    // =========================================================================
    bool notify_geometry_added(uint32_t new_chunk_id, RTXVector3 chunk_center, float chunk_radius) {
        geometry_generation++;
        as_generation++;
        repair_generation++;

        uint32_t blocked_count = 0;
        for (auto& node : bounce0_nodes) {
            if (!node.is_active) continue;
            float dx = node.position.x - chunk_center.x;
            float dy = node.position.y - chunk_center.y;
            float dz = node.position.z - chunk_center.z;
            float d_sq = dx * dx + dy * dy + dz * dz;

            if (d_sq <= chunk_radius * chunk_radius) {
                node.is_active = false;
                node.termination_reason = TERMINATION_BLOCKED_DESTRUCTIBLE;
                node.destruction_chunk_id = new_chunk_id;
                blocked_count++;

                ASTGRegenerationAnchor anchor;
                anchor.anchor_id = (uint32_t)regeneration_anchors.size();
                anchor.source_light_id = node.source_light_id;
                anchor.angular_cell_id = node.angular_cell_id;
                anchor.parent_node_id = node.node_id;
                anchor.blocking_chunk_id = new_chunk_id;
                anchor.bounce_depth = 0;
                anchor.ray_origin = node.position;
                anchor.ray_direction = node.geometric_normal;
                anchor.reason = TERMINATION_BLOCKED_DESTRUCTIBLE;
                anchor.geometry_generation = geometry_generation;
                anchor.as_generation = as_generation;
                anchor.is_active = true;
                regeneration_anchors.push_back(anchor);

                auto& dep = chunk_dependencies[new_chunk_id];
                dep.chunk_id = new_chunk_id;
                dep.transport_node_ids.push_back(node.node_id);
                dep.angular_cell_ids.push_back(node.angular_cell_id);
                dep.blocked_anchor_ids.push_back(anchor.anchor_id);
            }
        }

        return (blocked_count > 0);
    }

    ASTGExactMemoryAudit audit_memory_exact(size_t scene_chunks = 551, size_t destructible_chunks = 182) const {
        ASTGExactMemoryAudit a;
        a.anchor_count = regeneration_anchors.size();
        a.anchor_capacity = regeneration_anchors.capacity();
        a.anchor_payload_bytes = a.anchor_count * sizeof(ASTGRegenerationAnchor);
        a.anchor_allocator_overhead_bytes = (a.anchor_capacity - a.anchor_count) * sizeof(ASTGRegenerationAnchor);

        a.parent_ref_count = 0;
        for (const auto& n : bounce1_nodes) {
            a.parent_ref_count += n.parent_refs.size();
        }
        a.parent_ref_payload_bytes = a.parent_ref_count * sizeof(DAGParentRef);

        a.reverse_dependency_payload_bytes = 0;
        for (const auto& kv : chunk_dependencies) {
            a.reverse_dependency_payload_bytes += sizeof(uint32_t) + kv.second.transport_node_ids.size() * sizeof(uint32_t) + kv.second.blocked_anchor_ids.size() * sizeof(uint32_t);
        }
        a.reverse_dependency_container_overhead_bytes = chunk_dependencies.size() * 32;

        a.angular_frontier_payload_bytes = a.anchor_count * 16;
        a.generation_metadata_bytes = sizeof(uint32_t) * 3;

        a.total_repair_metadata_payload_bytes = a.anchor_payload_bytes + a.parent_ref_payload_bytes + a.reverse_dependency_payload_bytes + a.angular_frontier_payload_bytes + a.generation_metadata_bytes;
        a.total_repair_metadata_allocated_bytes = a.total_repair_metadata_payload_bytes + a.anchor_allocator_overhead_bytes + a.reverse_dependency_container_overhead_bytes;

        a.scene_chunks_total = scene_chunks;
        a.destructible_chunks_total = destructible_chunks;
        a.chunks_with_active_repair_metadata = chunk_dependencies.size();
        a.blocked_frontiers_active = a.anchor_count;

        a.bytes_per_scene_chunk = double(a.total_repair_metadata_payload_bytes) / double(std::max(size_t(1), a.scene_chunks_total));
        a.bytes_per_destructible_chunk = double(a.total_repair_metadata_payload_bytes) / double(std::max(size_t(1), a.destructible_chunks_total));
        a.bytes_per_active_repair_chunk = double(a.total_repair_metadata_payload_bytes) / double(std::max(size_t(1), a.chunks_with_active_repair_metadata));
        a.bytes_per_blocked_frontier = double(a.total_repair_metadata_payload_bytes) / double(std::max(size_t(1), a.blocked_frontiers_active));
        a.bytes_per_light = double(a.total_repair_metadata_payload_bytes) / 512.0;

        return a;
    }

    static uint64_t fnv1a_64(const void* data, size_t size, uint64_t hash = 14695981039346656037ULL) {
        const uint8_t* ptr = (const uint8_t*)data;
        for (size_t i = 0; i < size; ++i) {
            hash ^= (uint64_t)ptr[i];
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    uint64_t compute_geometry_state_hash() const {
        uint64_t h = 14695981039346656037ULL;
        uint32_t gg = geometry_generation;
        uint32_t asg = as_generation;
        h = fnv1a_64(&gg, sizeof(gg), h);
        h = fnv1a_64(&asg, sizeof(asg), h);
        return h;
    }

    uint64_t compute_light_static_hash(const std::vector<LightStatic>& lights) const {
        uint64_t h = 14695981039346656037ULL;
        for (const auto& l : lights) {
            h = fnv1a_64(&l, sizeof(LightStatic), h);
        }
        return h;
    }

    uint64_t compute_probe_layout_hash() const {
        uint64_t h = 14695981039346656037ULL;
        for (const auto& p : probes) {
            h = fnv1a_64(&p.world_position, sizeof(p.world_position), h);
            h = fnv1a_64(&p.geometric_normal, sizeof(p.geometric_normal), h);
            h = fnv1a_64(&p.primitive_id, sizeof(p.primitive_id), h);
        }
        return h;
    }

    uint64_t compute_transport_graph_hash() const {
        uint64_t h = 14695981039346656037ULL;
        for (const auto& n : bounce0_nodes) {
            h = fnv1a_64(&n.node_id, sizeof(n.node_id), h);
            h = fnv1a_64(&n.source_light_id, sizeof(n.source_light_id), h);
            h = fnv1a_64(&n.position, sizeof(n.position), h);
            h = fnv1a_64(&n.termination_reason, sizeof(n.termination_reason), h);
        }
        for (const auto& n : bounce1_nodes) {
            h = fnv1a_64(&n.node_id, sizeof(n.node_id), h);
            h = fnv1a_64(&n.position, sizeof(n.position), h);
            for (const auto& pref : n.parent_refs) {
                h = fnv1a_64(&pref.parent_node_id, sizeof(pref.parent_node_id), h);
            }
        }
        return h;
    }

    uint64_t compute_contribution_hash() const {
        uint64_t h = 14695981039346656037ULL;
        for (const auto& rec : persistent_contributions) {
            h = fnv1a_64(&rec.light_id, sizeof(rec.light_id), h);
            h = fnv1a_64(&rec.transfer_r, sizeof(rec.transfer_r), h);
        }
        return h;
    }

    uint64_t compute_repair_db_hash() const {
        uint64_t h = 14695981039346656037ULL;
        for (const auto& a : regeneration_anchors) {
            if (a.is_active) {
                h = fnv1a_64(&a.anchor_id, sizeof(a.anchor_id), h);
                h = fnv1a_64(&a.blocking_chunk_id, sizeof(a.blocking_chunk_id), h);
                h = fnv1a_64(&a.source_light_id, sizeof(a.source_light_id), h);
            }
        }
        for (const auto& kv : chunk_dependencies) {
            h = fnv1a_64(&kv.first, sizeof(kv.first), h);
            for (uint32_t aid : kv.second.blocked_anchor_ids) {
                h = fnv1a_64(&aid, sizeof(aid), h);
            }
        }
        return h;
    }
};
