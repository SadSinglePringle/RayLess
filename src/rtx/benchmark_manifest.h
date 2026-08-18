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
    BenchmarkCategory category = BENCHMARK_FULL_SCENE;
    std::string scene_name = "NVIDIA Bistro";
    std::string commit_hash = "8228598";
    std::string build_type = "Release (D3D12/DXR 1.1 Native)";
    std::string gpu_name = "NVIDIA GeForce RTX 4070";
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
    uint32_t probe_count = 0;
    uint32_t transport_node_count = 0;
    uint32_t transport_edge_count = 0;
    uint32_t angular_leaf_count = 0;

    uint32_t max_bounce_depth = 3;
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
        std::cout << "Benchmark Type:                " << (category == BENCHMARK_KERNEL ? "KERNEL MICROBENCHMARK" : (category == BENCHMARK_SUBSYSTEM ? "ASTG SUBSYSTEM TEST" : "FULL-SCENE BENCHMARK")) << "\n";
        std::cout << "Scene:                         " << scene_name << "\n";
        std::cout << "Synthetic Geometry:            " << (synthetic_geometry ? "YES" : "NO") << "\n";
        std::cout << "Synthetic Transport:           " << (synthetic_transport ? "YES" : "NO") << "\n";
        std::cout << "Synthetic Probe Contributions: " << (synthetic_probe_contributions ? "YES" : "NO") << "\n";
        std::cout << "Active GPU:                    " << gpu_name << "\n";
        std::cout << "DXR Tier:                      " << dxr_version << "\n";
        std::cout << "Triangle Count:                " << triangle_count << "\n";
        std::cout << "Instance Count:                " << instance_count << "\n";
        std::cout << "Material Count:                " << material_count << "\n";
        std::cout << "Stationary Light Count:        " << light_count << "\n";
        std::cout << "Surface Probe Count:           " << probe_count << "\n";
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
        ss << "    \"benchmark_category\": \"" << (category == BENCHMARK_KERNEL ? "KERNEL" : (category == BENCHMARK_SUBSYSTEM ? "SUBSYSTEM" : "FULL_SCENE")) << "\",\n";
        ss << "    \"scene\": \"" << scene_name << "\",\n";
        ss << "    \"commit_hash\": \"" << commit_hash << "\",\n";
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
        ss << "    \"transport_node_count\": " << transport_node_count << ",\n";
        ss << "    \"transport_edge_count\": " << transport_edge_count << ",\n";
        ss << "    \"angular_leaf_count\": " << angular_leaf_count << ",\n";
        ss << "    \"max_bounce_depth\": " << max_bounce_depth << ",\n";
        ss << "    \"angular_subdivision_depth\": " << angular_subdiv_depth << ",\n";
        ss << "    \"energy_pruning_threshold\": " << energy_pruning_threshold << ",\n";
        ss << "    \"probe_spacing_meters\": " << probe_spacing_meters << ",\n";
        ss << "    \"repair_ray_budget\": " << repair_ray_budget << ",\n";
        ss << "    \"synthetic_geometry\": " << (synthetic_geometry ? "true" : "false") << ",\n";
        ss << "    \"synthetic_transport\": " << (synthetic_transport ? "true" : "false") << ",\n";
        ss << "    \"synthetic_probe_contributions\": " << (synthetic_probe_contributions ? "true" : "false") << "\n";
        ss << "  },\n";
        return ss.str();
    }
};
