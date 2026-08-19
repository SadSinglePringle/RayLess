#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstdint>
#include <cstdlib>

enum BenchmarkCategory {
    BENCHMARK_KERNEL = 0,     // Isolated DXR traversal, light animation, compute kernels
    BENCHMARK_SUBSYSTEM = 1,  // Controlled geometry tests (angular hierarchy, graph repair, pruning)
    BENCHMARK_FULL_SCENE = 2  // Authentic imported geometry, materials, transport, probes, and repair
};

enum LightPlacementMode {
    PLACEMENT_SCENE_VALID = 0, // Validated non-intersecting, surface-bounded scene positions
    PLACEMENT_AABB_STRESS = 1  // Raw linear grid across full scene AABB (stress test)
};

inline const char* get_placement_mode_name(LightPlacementMode mode) {
    switch (mode) {
        case PLACEMENT_SCENE_VALID: return "SCENE_VALID";
        case PLACEMENT_AABB_STRESS: return "AABB_STRESS";
        default: return "UNKNOWN";
    }
}

enum RayOutcomeCategory {
    RAY_OUTCOME_CONFIRMED_EXISTING = 0,
    RAY_OUTCOME_CHANGED_RELATIONSHIP = 1,
    RAY_OUTCOME_BLOCKED_TO_VISIBLE = 2,
    RAY_OUTCOME_VISIBLE_TO_BLOCKED = 3,
    RAY_OUTCOME_NEW_TRANSPORT_BRANCH = 4,
    RAY_OUTCOME_NEW_BLOCKER_DISCOVERED = 5,
    RAY_OUTCOME_PROBE_DEPOSIT = 6,
    RAY_OUTCOME_MERGED_INTO_NODE = 7,
    RAY_OUTCOME_LOW_ENERGY_TERM = 8,
    RAY_OUTCOME_PROBE_MERGE_TERM = 9,
    RAY_OUTCOME_MAX_DEPTH_TERM = 10,
    RAY_OUTCOME_ESCAPED_SCENE = 11,
    RAY_OUTCOME_DUPLICATE_OR_WASTED = 12,
    RAY_OUTCOME_COUNT = 13
};

inline const char* get_ray_outcome_name(RayOutcomeCategory cat) {
    switch (cat) {
        case RAY_OUTCOME_CONFIRMED_EXISTING: return "Confirmed Existing Relationship";
        case RAY_OUTCOME_CHANGED_RELATIONSHIP: return "Changed Relationship";
        case RAY_OUTCOME_BLOCKED_TO_VISIBLE: return "Blocked -> Visible Transition";
        case RAY_OUTCOME_VISIBLE_TO_BLOCKED: return "Visible -> Blocked Transition";
        case RAY_OUTCOME_NEW_TRANSPORT_BRANCH: return "New Transport Branch";
        case RAY_OUTCOME_NEW_BLOCKER_DISCOVERED: return "New Blocker Discovered";
        case RAY_OUTCOME_PROBE_DEPOSIT: return "Probe Deposit";
        case RAY_OUTCOME_MERGED_INTO_NODE: return "Merged into Existing Node";
        case RAY_OUTCOME_LOW_ENERGY_TERM: return "Low Energy Termination";
        case RAY_OUTCOME_PROBE_MERGE_TERM: return "Probe Merge Termination";
        case RAY_OUTCOME_MAX_DEPTH_TERM: return "Maximum Depth Termination";
        case RAY_OUTCOME_ESCAPED_SCENE: return "Escaped Scene / Sky";
        case RAY_OUTCOME_DUPLICATE_OR_WASTED: return "Duplicate / Wasted Ray";
        default: return "Unknown";
    }
}

struct BenchmarkManifest {
    std::string run_id = "default_run";
    BenchmarkCategory category = BENCHMARK_FULL_SCENE;
    LightPlacementMode placement_mode = PLACEMENT_SCENE_VALID;
    std::string scene_name = "NVIDIA Bistro";
    std::string commit_hash = "4aa9600";
    std::string build_timestamp = __DATE__ " " __TIME__;
    std::string build_type = "Release (D3D12/DXR 1.1 Native)";
    std::string gpu_name = "NVIDIA GeForce RTX 4070 Laptop GPU";
    std::string gpu_driver = "572.16 (DirectX 12.2 / DXR Tier 1.1)";
    std::string cpu_name = "AMD Ryzen / Intel Core";
    uint64_t ram_bytes = 34359738368ULL;
    std::string godot_version = "Godot Engine v4.7.1-stable";
    std::string dxr_version = "DirectX Raytracing (DXR) Tier 1.1 / Shader Model 6.5";
    std::string resolution = "1920x1080 (Offscreen Headless Compute)";

    uint64_t triangle_count = 0;
    uint32_t instance_count = 0;
    uint32_t material_count = 0;
    uint32_t light_count = 0;
    uint32_t probe_count = 1200;
    uint32_t fan_in_cap = 32;

    // Explicit Discovery Telemetry (Disambiguated Coverage vs Ray Hit Rate)
    uint64_t discovery_rays_traced = 0;
    uint64_t discovery_rays_hit = 0;
    double discovery_light_coverage_pct = 0.0; // Percentage of lights that hit >= 1 surface
    double discovery_ray_hit_rate_pct = 0.0;    // Percentage of traced rays that hit geometry

    // Disambiguated Light Coverage Counts
    uint32_t lights_with_discovery_hit = 0;
    uint32_t lights_with_bounce0 = 0;
    uint32_t lights_with_bounce1 = 0;
    uint32_t lights_with_probe_deposition = 0;
    uint32_t lights_with_persistent_contribution = 0;

    // Disambiguated Graph Element Counts
    uint32_t bounce0_nodes = 0;
    uint32_t bounce1_nodes = 0;
    uint32_t dag_edges = 0;
    uint32_t probe_deposition_links = 0;
    uint32_t persistent_contribution_records = 0;
    uint32_t merge_links = 0;
    uint32_t reverse_dependency_links = 0;

    // Fan-in and Contributor Statistics
    uint64_t candidate_contributions = 0;
    uint64_t retained_contributions = 0;
    uint64_t pruned_contributions = 0;
    double mean_candidate_fanin = 0.0;
    double mean_retained_fanin = 0.0;
    double max_candidate_fanin = 0.0;
    double max_retained_fanin = 0.0;
    double p95_retained_fanin = 0.0;

    // GPU Buffer Capacities
    uint32_t gpu_rays_batch_capacity = 131072;
    uint32_t gpu_lights_capacity = 131072;
    uint32_t gpu_probes_capacity = 65536;
    uint32_t gpu_contributions_capacity = 1048576;

    uint32_t angular_allocated_cells = 0;
    uint32_t angular_constructed_cells = 0;
    uint32_t angular_leaf_count = 0;

    uint32_t max_bounce_depth = 1;
    uint32_t angular_subdiv_depth = 4;
    float energy_pruning_threshold = 0.005f;
    float probe_spacing_meters = 1.0f;
    uint32_t repair_ray_budget = 4096;

    bool synthetic_geometry = false;
    bool synthetic_transport = false;
    bool synthetic_probe_contributions = false;

    void print_startup_banner() const {
        std::cout << "================================================================================\n";
        std::cout << "🚀 RAYLESS / ASTG BENCHMARK SUITE\n";
        std::cout << "================================================================================\n";
        std::cout << "Run ID:                        " << run_id << "\n";
        std::cout << "Commit Hash:                   " << commit_hash << "\n";
        std::cout << "Build Timestamp:               " << build_timestamp << "\n";
        std::cout << "Benchmark Type:                " << (category == BENCHMARK_KERNEL ? "KERNEL MICROBENCHMARK" : (category == BENCHMARK_SUBSYSTEM ? "ASTG SUBSYSTEM TEST" : "FULL-SCENE BENCHMARK")) << "\n";
        std::cout << "Scene:                         " << scene_name << "\n";
        std::cout << "Placement Mode:                " << get_placement_mode_name(placement_mode) << "\n";
        std::cout << "Active GPU:                    " << gpu_name << "\n";
        std::cout << "DXR Tier:                      " << dxr_version << "\n";
        std::cout << "Triangle Count:                " << triangle_count << "\n";
        std::cout << "Instance Count:                " << instance_count << "\n";
        std::cout << "Material Count:                " << material_count << "\n";
        std::cout << "Stationary Light Count:        " << light_count << "\n";
        std::cout << "Surface Probe Count:           " << probe_count << "\n";
        std::cout << "Fan-In Cap (Top-K):            " << (fan_in_cap >= 4096 ? "UNLIMITED" : std::to_string(fan_in_cap)) << "\n";
        std::cout << "================================================================================\n\n";
        std::cout.flush();
    }

    void enforce_no_synthetic_hard_fail() const {
        if (category == BENCHMARK_FULL_SCENE) {
            if (synthetic_geometry || synthetic_transport || synthetic_probe_contributions) {
                std::cerr << "\n❌ [FATAL ERROR] Hard-Fail: Full-scene benchmark '" << scene_name 
                          << "' attempted to execute with synthetic components!\n";
                if (synthetic_geometry) std::cerr << "  - Synthetic Geometry detected.\n";
                if (synthetic_transport) std::cerr << "  - Synthetic Transport detected.\n";
                if (synthetic_probe_contributions) std::cerr << "  - Synthetic Probe Contributions detected.\n";
                std::cerr << "Full-scene benchmarks MUST execute against verified authentic geometry and transport.\n";
                std::cerr << "Terminating benchmark immediately.\n";
                std::exit(1);
            }
        }
    }

    std::string to_json_header() const {
        std::ostringstream ss;
        ss << "{\n";
        ss << "  \"manifest\": {\n";
        ss << "    \"run_id\": \"" << run_id << "\",\n";
        ss << "    \"benchmark_category\": \"" << (category == BENCHMARK_KERNEL ? "KERNEL" : (category == BENCHMARK_SUBSYSTEM ? "SUBSYSTEM" : "FULL_SCENE")) << "\",\n";
        ss << "    \"placement_mode\": \"" << get_placement_mode_name(placement_mode) << "\",\n";
        ss << "    \"scene\": \"" << scene_name << "\",\n";
        ss << "    \"commit_hash\": \"" << commit_hash << "\",\n";
        ss << "    \"build_timestamp\": \"" << build_timestamp << "\",\n";
        ss << "    \"build_type\": \"" << build_type << "\",\n";
        ss << "    \"gpu\": \"" << gpu_name << "\",\n";
        ss << "    \"gpu_driver\": \"" << gpu_driver << "\",\n";
        ss << "    \"cpu\": \"" << cpu_name << "\",\n";
        ss << "    \"ram_bytes\": " << ram_bytes << ",\n";
        ss << "    \"godot_version\": \"" << godot_version << "\",\n";
        ss << "    \"dxr_version\": \"" << dxr_version << "\",\n";
        ss << "    \"resolution\": \"" << resolution << "\",\n";
        ss << "    \"triangle_count\": " << triangle_count << ",\n";
        ss << "    \"instance_count\": " << instance_count << ",\n";
        ss << "    \"material_count\": " << material_count << ",\n";
        ss << "    \"light_count\": " << light_count << ",\n";
        ss << "    \"probe_count\": " << probe_count << ",\n";
        ss << "    \"fan_in_cap\": " << fan_in_cap << ",\n";
        ss << "    \"discovery_rays_traced\": " << discovery_rays_traced << ",\n";
        ss << "    \"discovery_rays_hit\": " << discovery_rays_hit << ",\n";
        ss << "    \"discovery_light_coverage_pct\": " << std::fixed << std::setprecision(2) << discovery_light_coverage_pct << ",\n";
        ss << "    \"discovery_ray_hit_rate_pct\": " << std::setprecision(4) << discovery_ray_hit_rate_pct << ",\n";
        ss << "    \"bounce0_nodes\": " << bounce0_nodes << ",\n";
        ss << "    \"bounce1_nodes\": " << bounce1_nodes << ",\n";
        ss << "    \"dag_edges\": " << dag_edges << ",\n";
        ss << "    \"probe_deposition_links\": " << probe_deposition_links << ",\n";
        ss << "    \"persistent_contribution_records\": " << persistent_contribution_records << ",\n";
        ss << "    \"merge_links\": " << merge_links << ",\n";
        ss << "    \"reverse_dependency_links\": " << reverse_dependency_links << ",\n";
        ss << "    \"lights_with_discovery_hit\": " << lights_with_discovery_hit << ",\n";
        ss << "    \"lights_with_bounce0\": " << lights_with_bounce0 << ",\n";
        ss << "    \"lights_with_bounce1\": " << lights_with_bounce1 << ",\n";
        ss << "    \"lights_with_probe_deposition\": " << lights_with_probe_deposition << ",\n";
        ss << "    \"lights_with_persistent_contribution\": " << lights_with_persistent_contribution << ",\n";
        ss << "    \"candidate_contributions\": " << candidate_contributions << ",\n";
        ss << "    \"retained_contributions\": " << retained_contributions << ",\n";
        ss << "    \"pruned_contributions\": " << pruned_contributions << ",\n";
        ss << "    \"mean_candidate_fanin\": " << mean_candidate_fanin << ",\n";
        ss << "    \"mean_retained_fanin\": " << mean_retained_fanin << ",\n";
        ss << "    \"max_candidate_fanin\": " << max_candidate_fanin << ",\n";
        ss << "    \"max_retained_fanin\": " << max_retained_fanin << ",\n";
        ss << "    \"p95_retained_fanin\": " << p95_retained_fanin << ",\n";
        ss << "    \"gpu_rays_batch_capacity\": " << gpu_rays_batch_capacity << ",\n";
        ss << "    \"gpu_lights_capacity\": " << gpu_lights_capacity << ",\n";
        ss << "    \"gpu_probes_capacity\": " << gpu_probes_capacity << ",\n";
        ss << "    \"gpu_contributions_capacity\": " << gpu_contributions_capacity << ",\n";
        ss << "    \"synthetic_geometry\": " << (synthetic_geometry ? "true" : "false") << ",\n";
        ss << "    \"synthetic_transport\": " << (synthetic_transport ? "true" : "false") << ",\n";
        ss << "    \"synthetic_probe_contributions\": " << (synthetic_probe_contributions ? "true" : "false") << "\n";
        ss << "  },\n";
        return ss.str();
    }
};
