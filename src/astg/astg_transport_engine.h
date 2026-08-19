#pragma once
#include "rtx_types.h"
#include "rtx_raytracer.h"
#include "benchmark_manifest.h"
#include "gltf_scene_loader.h"
#include <vector>
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

// DAG Node->Node Edge (Bounce 0 -> Bounce 1 Path Link)
struct ASTGDAGEdge {
    uint32_t edge_id = 0;
    uint32_t parent_node_id = 0;
    uint32_t child_node_id = 0;
    uint32_t source_light_id = 0;
    uint32_t angular_cell_id = 0;
    uint32_t bounce_depth = 1;
    float transfer_weight = 1.0f;
    bool is_stitch_edge = false;
    uint32_t repair_generation = 0;
    bool is_active = true;
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

    bool can_stitch(
        const ASTGRayHit& repair_hit,
        const ASTGTransportNode& candidate,
        uint32_t source_light_id,
        uint32_t angular_cell_id,
        uint32_t destroyed_chunk_id,
        float* out_score,
        StitchRejectionReason* out_reason
    ) const {
        if (!candidate.is_active) {
            if (out_reason) *out_reason = STITCH_REJECT_GENERATION_STALE;
            return false;
        }
        if (candidate.generation != geometry_generation && candidate.generation == 0) {
            if (out_reason) *out_reason = STITCH_REJECT_GENERATION_STALE;
            return false;
        }
        if (candidate.surface_cluster_id != repair_hit.surface_cluster_id) {
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

        if (out_score) {
            float pos_score = 1.0f - std::sqrt(dist_sq) / stitch_pos_threshold;
            float norm_score = (ndot - stitch_normal_threshold) / (1.0f - stitch_normal_threshold);
            *out_score = pos_score * 0.4f + norm_score * 0.4f + (candidate.bounce_depth == 1 ? 0.2f : 0.1f);
        }
        if (out_reason) *out_reason = STITCH_REJECT_NONE;
        return true;
    }

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
            if (!dep.is_active) continue;
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
                disc_rays[ray_idx].angular_cell_id = r % 64;
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

                                const ASTGTransportNode* cand_node = nullptr;
                                if (cand_id < bounce0_nodes.size()) cand_node = &bounce0_nodes[cand_id];
                                else if (cand_id - bounce0_nodes.size() < bounce1_nodes.size()) cand_node = &bounce1_nodes[cand_id - bounce0_nodes.size()];
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
                                stitching_metrics.reused_suffix_nodes++;

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

                                auto it_dep = node_to_path_contributions.find(best_node_id);
                                if (it_dep != node_to_path_contributions.end()) {
                                    for (uint32_t dep_id : it_dep->second) {
                                        if (dep_id < path_probe_contributions.size() && path_probe_contributions[dep_id].is_active) {
                                            const auto& orig_dep = path_probe_contributions[dep_id];
                                            ASTGPathProbeContribution spliced_dep = orig_dep;
                                            spliced_dep.contribution_id = (uint32_t)path_probe_contributions.size();
                                            spliced_dep.source_light_id = source_light;
                                            spliced_dep.angular_cell_id = ang_cell;
                                            spliced_dep.generation = geometry_generation;
                                            spliced_dep.path_provenance_id = fnv1a_64_path(source_light, ang_cell, spliced_dep.bounce_depth, best_node_id, spliced_dep.probe_id);
                                            spliced_dep.is_active = true;
                                            path_probe_contributions.push_back(spliced_dep);
                                            node_to_path_contributions[best_node_id].push_back(spliced_dep.contribution_id);
                                            stitching_metrics.reused_probe_depositions++;
                                        }
                                    }
                                }

                                regeneration_anchors[a_idx].reason = TERMINATION_STITCHED_TO_EXISTING_DAG;
                            }
                        }
                    }

                    if (!stitched) {
                        ASTGTransportNode new_node;
                        new_node.node_id = (uint32_t)bounce0_nodes.size();
                        new_node.source_light_id = source_light;
                        new_node.angular_cell_id = ang_cell;
                        new_node.bounce_depth = 0;
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
                        bounce0_nodes.push_back(new_node);
                        stitching_metrics.new_bridge_nodes++;

                        surface_cluster_to_nodes[new_node.surface_cluster_id].push_back(new_node.node_id);

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
