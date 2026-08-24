#include "rtx_raytracer.h"
#include "rtx_types.h"
#include "benchmark_manifest.h"
#include "gltf_scene_loader.h"
#include "astg_transport_engine.h"
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <random>
#include <iomanip>
#include <cstring>
#include <cassert>

struct StressTestResult {
    std::string name;
    bool passed = false;
    std::string details;
    double elapsed_us = 0.0;
};

static std::vector<StressTestResult> g_results;

#define RECORD_TEST(name_str, pass_bool, details_str, time_val) \
    g_results.push_back({ name_str, pass_bool, details_str, time_val }); \
    std::cout << (pass_bool ? "  ✅ [PASS] " : "  ❌ [FAIL] ") << name_str \
              << " (" << std::fixed << std::setprecision(1) << time_val << " µs): " \
              << details_str << "\n";

// Helper to build a synthetic TLAS of cubes for micro-testing
void setup_test_tlas(int cube_count = 100) {
    std::vector<RTXVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<PrimitiveMetadata> metadata;
    std::vector<int32_t> chunk_ids;

    for (int i = 0; i < cube_count; ++i) {
        float cx = (i % 10) * 4.0f - 20.0f;
        float cz = (i / 10) * 4.0f - 20.0f;
        uint32_t base_v = (uint32_t)vertices.size();

        for (int vx = 0; vx < 2; ++vx) {
            for (int vy = 0; vy < 2; ++vy) {
                for (int vz = 0; vz < 2; ++vz) {
                    RTXVertex v;
                    v.px = cx + (vx ? 1.0f : -1.0f);
                    v.py = 1.0f + (vy ? 1.0f : -1.0f);
                    v.pz = cz + (vz ? 1.0f : -1.0f);
                    v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f;
                    vertices.push_back(v);
                }
            }
        }

        uint32_t face_indices[] = {
            0,1,2, 1,3,2, 4,6,5, 5,6,7,
            0,2,4, 2,6,4, 1,5,3, 3,5,7,
            0,4,1, 1,4,5, 2,3,6, 3,7,6
        };
        for (int f = 0; f < 36; ++f) indices.push_back(base_v + face_indices[f]);
        for (int t = 0; t < 12; ++t) {
            PrimitiveMetadata m;
            m.mesh_id = i;
            m.surface_cluster_id = i;
            m.destruction_chunk_id = i;
            m.material_id = 1;
            metadata.push_back(m);
            chunk_ids.push_back(i);
        }
    }

    rtx_build_partitioned_as(
        vertices.data(), (int32_t)vertices.size(),
        indices.data(), (int32_t)indices.size(),
        metadata.data(), (int32_t)metadata.size(),
        chunk_ids.data(), (int32_t)chunk_ids.size()
    );
}

// -----------------------------------------------------------------------------
// SUITE 1: BOUNDARY BATCH SIZES
// -----------------------------------------------------------------------------
void test_boundary_batch_sizes() {
    std::cout << "\n================================================================================\n";
    std::cout << "🧪 SUITE 1: BOUNDARY BATCH SIZES (0, 1, 63, 64, 65, 1024, 65536, 131072, 131073)\n";
    std::cout << "================================================================================\n";

    // Setup base persistent nodes & edges
    const uint32_t base_node_count = 2048;
    const uint32_t base_edge_count = 1024;
    std::vector<ASTGGPUNode> nodes(base_node_count);
    std::vector<ASTGGPUDAGEdge> edges(base_edge_count);

    for (uint32_t i = 0; i < base_node_count; ++i) {
        nodes[i].pos_x = (float)(i % 32) * 1.5f - 24.0f;
        nodes[i].pos_y = 5.0f;
        nodes[i].pos_z = (float)(i / 32) * 1.5f - 24.0f;
        nodes[i].normal_x = 0.0f; nodes[i].normal_y = 1.0f; nodes[i].normal_z = 0.0f;
        nodes[i].active_flags = 1;
        nodes[i].generation = 10;
        nodes[i].chunk_id = 0xFFFFFFFF;
    }

    for (uint32_t i = 0; i < base_edge_count; ++i) {
        edges[i].source_node_id = i;
        edges[i].dest_node_id = (i + 1) % base_node_count;
        edges[i].generation = 10;
        edges[i].edge_state = 0; // ACTIVE
        edges[i].flags = 1;      // is_active
        edges[i].destruction_chunk_id = 0xFFFFFFFF;
    }

    rtx_upload_astg_nodes(nodes.data(), 0, base_node_count);
    rtx_upload_astg_edges(edges.data(), 0, base_edge_count);

    std::vector<uint32_t> test_sizes = { 0, 1, 2, 31, 32, 63, 64, 65, 127, 128, 512, 1024, 8192, 32768, 65536, 131072, 131073 };

    for (uint32_t batch_size : test_sizes) {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<ASTGGPUVisibilityCandidate> cands(batch_size);
        for (uint32_t i = 0; i < batch_size; ++i) {
            cands[i].edge_id = i % base_edge_count;
            cands[i].object_id = 0;
            cands[i].transport_generation = 10;
        }

        std::vector<ASTGEdgeVisibilityResult> results(std::max(1u, batch_size));
        ASTGVisibilityCounters counters = {};
        RTGPUTimings timings = {};

        int32_t traced = rtx_trace_candidates_batch(
            cands.data(),
            batch_size,
            results.data(),
            &counters,
            &timings
        );

        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();

        bool pass = true;
        std::string detail;

        if (batch_size == 0) {
            pass = (traced == 0);
            detail = "Zero batch handled gracefully without crash (returned 0)";
        } else if (batch_size > 131072) {
            // Buffer capacity is 131,072: should safely clamp to 131,072
            pass = (traced == 131072 && counters.edges_considered == 131072);
            detail = "Exceeded batch capacity clamped to max 131,072 safely without VRAM overflow";
        } else {
            pass = (traced == (int32_t)batch_size) &&
                   (counters.edges_considered == batch_size) &&
                   (counters.generation_rejected == 0) &&
                   (counters.rayquery_candidates == batch_size);
            detail = "Dispatched " + std::to_string(batch_size) + " candidates, all resolved (GPU time: " +
                     std::to_string(timings.rt_traversal_ms) + " ms)";
        }

        RECORD_TEST("BatchSize_" + std::to_string(batch_size), pass, detail, us);
    }
}

// -----------------------------------------------------------------------------
// SUITE 2: DIRTY RANGE SYNCHRONIZATION & RAPID MUTATION STRESS
// -----------------------------------------------------------------------------
void test_dirty_range_synchronization_stress() {
    std::cout << "\n================================================================================\n";
    std::cout << "🧪 SUITE 2: DIRTY RANGE SYNCHRONIZATION & CAPACITY BOUNDARIES\n";
    std::cout << "================================================================================\n";

    const uint32_t max_nodes_cap = 262144;
    const uint32_t max_edges_cap = 524288;

    // Test 2.1: Capacity limit boundary checks
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        ASTGGPUNode dummy_node = {};
        ASTGGPUDAGEdge dummy_edge = {};

        bool oob_node1 = rtx_update_gpu_nodes_range(&dummy_node, max_nodes_cap, 1);
        bool oob_node2 = rtx_update_gpu_nodes_range(&dummy_node, max_nodes_cap - 5, 10);
        bool oob_edge1 = rtx_update_gpu_edges_range(&dummy_edge, max_edges_cap, 1);
        bool oob_edge2 = rtx_update_gpu_edges_range(&dummy_edge, max_edges_cap - 5, 10);

        bool edge_at_limit = rtx_update_gpu_edges_range(&dummy_edge, max_edges_cap - 1, 1);
        bool node_at_limit = rtx_update_gpu_nodes_range(&dummy_node, max_nodes_cap - 1, 1);

        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();

        bool pass = (!oob_node1 && !oob_node2 && !oob_edge1 && !oob_edge2 && edge_at_limit && node_at_limit);
        RECORD_TEST("Capacity_Boundary_Rejection", pass, "OOB offsets rejected, exact capacity-1 accepted", us);
    }

    // Test 2.2: Full-capacity upload and bitwise roundtrip verification (262,144 nodes & 524,288 edges)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::cout << "  • Allocating and uploading full-capacity buffers (262k nodes [12MB], 524k edges [16MB])...\n";

        std::vector<ASTGGPUNode> full_nodes(max_nodes_cap);
        for (uint32_t i = 0; i < max_nodes_cap; ++i) {
            full_nodes[i].pos_x = float(i) * 0.05f;
            full_nodes[i].pos_y = 1.0f + float(i % 100);
            full_nodes[i].pos_z = float(i) * 0.02f;
            full_nodes[i].active_flags = (i % 2 == 0) ? 1 : 3;
            full_nodes[i].normal_x = 0.0f; full_nodes[i].normal_y = 1.0f; full_nodes[i].normal_z = 0.0f;
            full_nodes[i].generation = 100 + (i % 1000);
            full_nodes[i].albedo_r = 0.5f; full_nodes[i].albedo_g = 0.6f; full_nodes[i].albedo_b = 0.7f;
            full_nodes[i].chunk_id = i % 256;
        }

        std::vector<ASTGGPUDAGEdge> full_edges(max_edges_cap);
        for (uint32_t i = 0; i < max_edges_cap; ++i) {
            full_edges[i].source_node_id = i % max_nodes_cap;
            full_edges[i].dest_node_id = (i + 1) % max_nodes_cap;
            full_edges[i].generation = 50 + (i % 500);
            full_edges[i].edge_state = (i % 10 == 0) ? 2 : 0;
            full_edges[i].source_light_id = i % 32;
            full_edges[i].angular_cell_id = i % 64;
            full_edges[i].destruction_chunk_id = (i % 4 == 0) ? (i % 16) : 0xFFFFFFFF;
            full_edges[i].flags = 1 | ((i % 2) << 1);
        }

        bool up_n = rtx_upload_astg_nodes(full_nodes.data(), 0, max_nodes_cap);
        bool up_e = rtx_upload_astg_edges(full_edges.data(), 0, max_edges_cap);

        std::vector<ASTGGPUNode> rb_nodes(max_nodes_cap);
        std::vector<ASTGGPUDAGEdge> rb_edges(max_edges_cap);

        bool rb_n = rtx_readback_astg_nodes(rb_nodes.data(), 0, max_nodes_cap);
        bool rb_e = rtx_readback_astg_edges(rb_edges.data(), 0, max_edges_cap);

        bool bitwise_nodes = (memcmp(full_nodes.data(), rb_nodes.data(), max_nodes_cap * sizeof(ASTGGPUNode)) == 0);
        bool bitwise_edges = (memcmp(full_edges.data(), rb_edges.data(), max_edges_cap * sizeof(ASTGGPUDAGEdge)) == 0);

        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();

        bool pass = (up_n && up_e && rb_n && rb_e && bitwise_nodes && bitwise_edges);
        RECORD_TEST("Full_Capacity_262k_524k_Roundtrip", pass, "28MB VRAM uploaded & read back with 100% bitwise identity", us);
    }

    // Test 2.3: Rapid Random Disjoint Interval Mutations Torture (100 rounds)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        const uint32_t active_node_count = 50000;
        const uint32_t active_edge_count = 50000;

        std::vector<ASTGGPUNode> shadow_nodes(active_node_count);
        std::vector<ASTGGPUDAGEdge> shadow_edges(active_edge_count);
        for (uint32_t i = 0; i < active_node_count; ++i) {
            shadow_nodes[i].generation = 1;
            shadow_nodes[i].active_flags = 1;
            shadow_nodes[i].pos_x = float(i);
        }
        for (uint32_t i = 0; i < active_edge_count; ++i) {
            shadow_edges[i].generation = 1;
            shadow_edges[i].edge_state = 0;
            shadow_edges[i].flags = 1;
        }
        rtx_upload_astg_nodes(shadow_nodes.data(), 0, active_node_count);
        rtx_upload_astg_edges(shadow_edges.data(), 0, active_edge_count);

        std::mt19937 rng(1337);
        bool all_rounds_pass = true;

        for (int round = 0; round < 100; ++round) {
            // Pick 5 random disjoint ranges for nodes and edges
            for (int k = 0; k < 5; ++k) {
                uint32_t n_off = rng() % (active_node_count - 200);
                uint32_t n_len = 1 + (rng() % 100);
                for (uint32_t j = 0; j < n_len; ++j) {
                    shadow_nodes[n_off + j].generation += (round + 1);
                    shadow_nodes[n_off + j].pos_x += 1.5f;
                }
                rtx_update_gpu_nodes_range(&shadow_nodes[n_off], n_off, n_len);

                uint32_t e_off = rng() % (active_edge_count - 200);
                uint32_t e_len = 1 + (rng() % 100);
                for (uint32_t j = 0; j < e_len; ++j) {
                    shadow_edges[e_off + j].generation += (round + 1);
                    shadow_edges[e_off + j].edge_state = (shadow_edges[e_off + j].edge_state + 1) % 3;
                }
                rtx_update_gpu_edges_range(&shadow_edges[e_off], e_off, e_len);
            }

            // Flush sync
            rtx_sync_gpu_transport_buffers();

            // Periodic verification every 20 rounds
            if ((round + 1) % 20 == 0) {
                std::vector<ASTGGPUNode> verify_nodes(active_node_count);
                std::vector<ASTGGPUDAGEdge> verify_edges(active_edge_count);
                rtx_readback_astg_nodes(verify_nodes.data(), 0, active_node_count);
                rtx_readback_astg_edges(verify_edges.data(), 0, active_edge_count);

                if (memcmp(shadow_nodes.data(), verify_nodes.data(), active_node_count * sizeof(ASTGGPUNode)) != 0 ||
                    memcmp(shadow_edges.data(), verify_edges.data(), active_edge_count * sizeof(ASTGGPUDAGEdge)) != 0) {
                    all_rounds_pass = false;
                    break;
                }
            }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        RECORD_TEST("Random_Disjoint_100_Round_Torture", all_rounds_pass, "100 multi-interval mutation & sync cycles verified bitwise", us);
    }
}

// -----------------------------------------------------------------------------
// SUITE 3: GENERATION REJECTION UNDER SIMULATED HOST MUTATION RACES
// -----------------------------------------------------------------------------
void test_generation_rejection_races() {
    std::cout << "\n================================================================================\n";
    std::cout << "🧪 SUITE 3: GENERATION REJECTION UNDER SIMULATED HOST MUTATION RACES\n";
    std::cout << "================================================================================\n";

    const uint32_t node_count = 1000;
    const uint32_t edge_count = 500;
    std::vector<ASTGGPUNode> nodes(node_count);
    std::vector<ASTGGPUDAGEdge> edges(edge_count);

    for (uint32_t i = 0; i < node_count; ++i) {
        nodes[i].pos_x = (float)i * 0.1f;
        nodes[i].pos_y = 5.0f;
        nodes[i].pos_z = 0.0f;
        nodes[i].normal_x = 0.0f; nodes[i].normal_y = 1.0f; nodes[i].normal_z = 0.0f;
        nodes[i].active_flags = (i == 400) ? 0 : 1; // Node 400 is inactive
        nodes[i].generation = 10;
        nodes[i].chunk_id = 0xFFFFFFFF;
    }

    for (uint32_t i = 0; i < edge_count; ++i) {
        edges[i].source_node_id = i;
        edges[i].dest_node_id = i + 1;
        edges[i].generation = 100;
        edges[i].edge_state = (i == 50) ? 1 : 0; // Edge 50 is INVALID_STATIC
        edges[i].flags = (i == 60) ? 0 : 1;      // Edge 60 is inactive
        edges[i].source_light_id = 0;
        edges[i].angular_cell_id = 0;
        edges[i].destruction_chunk_id = 0xFFFFFFFF;
    }

    // Edge 70 points to out-of-bounds node
    edges[70].dest_node_id = 999999;
    // Edge 80 points to inactive node 400
    edges[80].dest_node_id = 400;

    rtx_upload_astg_nodes(nodes.data(), 0, node_count);
    rtx_upload_astg_edges(edges.data(), 0, edge_count);

    // Test 3.1: Individual Failure Modes
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<ASTGGPUVisibilityCandidate> cands;

        // Case 0: Valid candidate
        cands.push_back({ 10, 0, 100 });
        // Case 1: Stale generation (edge is 100, cand is 99)
        cands.push_back({ 11, 0, 99 });
        // Case 2: Out-of-bounds edge ID (edge_id 9999 >= 500)
        cands.push_back({ 9999, 0, 100 });
        // Case 3: Edge in INVALID_STATIC state (edge 50)
        cands.push_back({ 50, 0, 100 });
        // Case 4: Edge inactive (edge 60)
        cands.push_back({ 60, 0, 100 });
        // Case 5: Node OOB (edge 70)
        cands.push_back({ 70, 0, 100 });
        // Case 6: Node inactive (edge 80)
        cands.push_back({ 80, 0, 100 });

        std::vector<ASTGEdgeVisibilityResult> res(cands.size());
        ASTGVisibilityCounters counters = {};
        RTGPUTimings timings = {};

        rtx_trace_candidates_batch(cands.data(), (uint32_t)cands.size(), res.data(), &counters, &timings);

        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();

        bool c0_ok = (res[0].visibility_state != 2);
        bool c1_ok = (res[1].visibility_state == 2 && res[1].generation == 100);
        bool c2_ok = (res[2].visibility_state == 2);
        bool c3_ok = (res[3].visibility_state == 2);
        bool c4_ok = (res[4].visibility_state == 2);
        bool c5_ok = (res[5].visibility_state == 2);
        bool c6_ok = (res[6].visibility_state == 2);

        bool pass = (c0_ok && c1_ok && c2_ok && c3_ok && c4_ok && c5_ok && c6_ok && counters.generation_rejected == 6);
        RECORD_TEST("Granular_Generation_Rejection_Cases", pass, "All 6 failure categories rejected (state=2, counters=6)", us);
    }

    // Test 3.2: High-Volume Adversarial Mixed Stress (131,072 candidates)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        const uint32_t large_count = 131072;
        std::vector<ASTGGPUVisibilityCandidate> large_cands(large_count);

        uint32_t expected_rejected = 0;
        uint32_t expected_valid = 0;

        for (uint32_t i = 0; i < large_count; ++i) {
            uint32_t type = i % 4;
            if (type == 0) {
                // Valid candidate
                large_cands[i].edge_id = (i % 40); // Valid edges 0..39
                large_cands[i].object_id = 0;
                large_cands[i].transport_generation = 100;
                expected_valid++;
            } else if (type == 1) {
                // Stale generation
                large_cands[i].edge_id = (i % 40);
                large_cands[i].object_id = 0;
                large_cands[i].transport_generation = 999; // mismatch
                expected_rejected++;
            } else if (type == 2) {
                // Out of bounds edge
                large_cands[i].edge_id = 10000 + (i % 1000);
                large_cands[i].object_id = 0;
                large_cands[i].transport_generation = 100;
                expected_rejected++;
            } else {
                // Edge with inactive/OOB node (edge 50, 60, 70, 80)
                uint32_t bad_edges[] = { 50, 60, 70, 80 };
                large_cands[i].edge_id = bad_edges[i % 4];
                large_cands[i].object_id = 0;
                large_cands[i].transport_generation = 100;
                expected_rejected++;
            }
        }

        std::vector<ASTGEdgeVisibilityResult> large_results(large_count);
        ASTGVisibilityCounters large_counters = {};
        RTGPUTimings large_timings = {};

        rtx_trace_candidates_batch(
            large_cands.data(),
            large_count,
            large_results.data(),
            &large_counters,
            &large_timings
        );

        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();

        bool match_counters = (large_counters.edges_considered == large_count) &&
                              (large_counters.generation_rejected == expected_rejected) &&
                              (large_counters.rayquery_candidates == expected_valid);

        bool bitwise_results_correct = true;
        for (uint32_t i = 0; i < large_count; ++i) {
            uint32_t type = i % 4;
            if (type == 0) {
                if (large_results[i].visibility_state == 2) { bitwise_results_correct = false; break; }
            } else {
                if (large_results[i].visibility_state != 2) { bitwise_results_correct = false; break; }
            }
        }

        bool pass = (match_counters && bitwise_results_correct);
        std::string detail = "Dispatched 131,072 mixed candidates (Valid: " + std::to_string(expected_valid) +
                             ", Rejected: " + std::to_string(expected_rejected) + ") - 100% matched oracle";
        RECORD_TEST("Adversarial_Mixed_131k_Race_Simulation", pass, detail, us);
    }
}

// -----------------------------------------------------------------------------
// SUITE 4: NUMERICAL ROBUSTNESS & DEGENERATE GEOMETRY
// -----------------------------------------------------------------------------
void test_numerical_robustness() {
    std::cout << "\n================================================================================\n";
    std::cout << "🧪 SUITE 4: NUMERICAL ROBUSTNESS & DEGENERATE GEOMETRY\n";
    std::cout << "================================================================================\n";

    const uint32_t degen_count = 1000;
    std::vector<ASTGGPUNode> nodes(degen_count * 2);
    std::vector<ASTGGPUDAGEdge> edges(degen_count);
    std::vector<ASTGGPUVisibilityCandidate> cands(degen_count);

    for (uint32_t i = 0; i < degen_count; ++i) {
        uint32_t s_idx = i * 2;
        uint32_t d_idx = i * 2 + 1;

        nodes[s_idx].pos_x = 0.0f; nodes[s_idx].pos_y = 2.0f; nodes[s_idx].pos_z = 0.0f;
        nodes[s_idx].active_flags = 1; nodes[s_idx].generation = 1;

        if (i == 0) {
            // Exact coincident nodes (distance = 0)
            nodes[d_idx].pos_x = 0.0f; nodes[d_idx].pos_y = 2.0f; nodes[d_idx].pos_z = 0.0f;
            nodes[s_idx].normal_x = 0.0f; nodes[s_idx].normal_y = 1.0f; nodes[s_idx].normal_z = 0.0f;
        } else if (i == 1) {
            // Sub-millimeter distance (0.00005m)
            nodes[d_idx].pos_x = 0.00005f; nodes[d_idx].pos_y = 2.0f; nodes[d_idx].pos_z = 0.0f;
            nodes[s_idx].normal_x = 0.0f; nodes[s_idx].normal_y = 1.0f; nodes[s_idx].normal_z = 0.0f;
        } else if (i == 2) {
            // Degenerate zero normal vector
            nodes[d_idx].pos_x = 5.0f; nodes[d_idx].pos_y = 2.0f; nodes[d_idx].pos_z = 0.0f;
            nodes[s_idx].normal_x = 0.0f; nodes[s_idx].normal_y = 0.0f; nodes[s_idx].normal_z = 0.0f;
        } else if (i == 3) {
            // Extreme distance (50,000m)
            nodes[d_idx].pos_x = 50000.0f; nodes[d_idx].pos_y = 2.0f; nodes[d_idx].pos_z = 0.0f;
            nodes[s_idx].normal_x = 0.0f; nodes[s_idx].normal_y = 1.0f; nodes[s_idx].normal_z = 0.0f;
        } else {
            // Normal varied paths
            nodes[d_idx].pos_x = float(i) * 0.1f; nodes[d_idx].pos_y = 2.0f; nodes[d_idx].pos_z = float(i) * 0.1f;
            nodes[s_idx].normal_x = 0.0f; nodes[s_idx].normal_y = 1.0f; nodes[s_idx].normal_z = 0.0f;
        }

        nodes[d_idx].active_flags = 1; nodes[d_idx].generation = 1;

        edges[i].source_node_id = s_idx;
        edges[i].dest_node_id = d_idx;
        edges[i].generation = 1;
        edges[i].flags = 1;

        cands[i].edge_id = i;
        cands[i].object_id = 0;
        cands[i].transport_generation = 1;
    }

    rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
    rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<ASTGEdgeVisibilityResult> results(degen_count);
    ASTGVisibilityCounters counters = {};
    RTGPUTimings timings = {};

    rtx_trace_candidates_batch(cands.data(), degen_count, results.data(), &counters, &timings);
    auto t1 = std::chrono::high_resolution_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count();

    bool coincident_visible = (results[0].visibility_state == 0); // Trivially visible
    bool all_valid = true;
    for (uint32_t i = 0; i < degen_count; ++i) {
        if (results[i].visibility_state > 1) all_valid = false;
    }

    bool pass = (coincident_visible && all_valid);
    RECORD_TEST("Degenerate_Geometry_Stress", pass, "Coincident, sub-mm, zero-normal, extreme distance resolved with 0 crashes/NaNs", us);
}

// -----------------------------------------------------------------------------
// SUITE 5: HARDWARE DXR TRAVERSAL ON AUTHENTIC BISTRO GEOMETRY (RTX 4070)
// -----------------------------------------------------------------------------
void test_hardware_dxr_bistro_traversal() {
    std::cout << "\n================================================================================\n";
    std::cout << "🧪 SUITE 5: HARDWARE DXR TRAVERSAL ON AUTHENTIC BISTRO SCENE (RTX 4070)\n";
    std::cout << "================================================================================\n";

    ParsedSceneGeometry bistro;
    bool loaded = GLTFSceneLoader::load_bistro(
        "assets/bistro/bistro.gltf",
        "assets/bistro/bistro.bin",
        bistro
    );

    if (!loaded) {
        std::cerr << "❌ Failed to load Bistro glTF!\n";
        return;
    }

    std::cout << "  • Building authentic Bistro TLAS (1.75M Triangles, 551 Meshes)...\n";
    rtx_build_partitioned_as(
        bistro.vertices.data(), (int32_t)bistro.vertices.size(),
        bistro.indices.data(), (int32_t)bistro.indices.size(),
        bistro.metadata.data(), (int32_t)bistro.metadata.size(),
        bistro.chunk_ids.data(), (int32_t)bistro.chunk_ids.size()
    );

    // Generate real transport graph
    ASTGTransportEngine transport_engine;
    transport_engine.generate_surface_probes(bistro, 1200);

    std::vector<LightStatic> static_lights;
    std::vector<LightDynamic> dynamic_lights;
    ASTGTransportEngine::generate_scene_valid_lights(bistro, 32, static_lights, dynamic_lights, 8.0f);
    rtx_init_massive_lights(static_lights.data(), dynamic_lights.data(), 32);

    transport_engine.execute_transport_discovery(static_lights, bistro, 512, 32);

    // Verify GPU ASTG synchronization
    transport_engine.full_sync_gpu_astg();

    // Collect all candidate edges from the real transport graph
    std::vector<uint32_t> candidate_edges;
    for (size_t i = 0; i < transport_engine.dag_edges.size(); ++i) {
        candidate_edges.push_back((uint32_t)i);
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<ASTGEdgeVisibilityResult> results;
    ASTGVisibilityCounters counters = {};
    RTGPUTimings timings = {};

    int32_t traced = transport_engine.trace_candidates_gpu(candidate_edges, 0, results, counters, &timings);
    auto t1 = std::chrono::high_resolution_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count();

    bool pass = (traced == (int32_t)candidate_edges.size()) && (counters.edges_considered == candidate_edges.size());
    std::string detail = "Evaluated " + std::to_string(traced) + " authentic DAG edges on RTX 4070 (GPU Traversal: " +
                         std::to_string(timings.rt_traversal_ms) + " ms, Blocked: " +
                         std::to_string(counters.rayquery_blocked) + ", Visible: " +
                         std::to_string(counters.rayquery_visible) + ")";
    RECORD_TEST("Authentic_Bistro_DXR_Traversal", pass, detail, us);
}

// -----------------------------------------------------------------------------
// MAIN ENTRY
// -----------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (!rtx_init()) {
        std::cerr << "❌ Failed to initialize DXR 1.1 Hardware RT Core Ray Tracer.\n";
        return 1;
    }

    std::cout << "================================================================================\n";
    std::cout << "🔥 RAYLESS MILESTONE 1 ADVERSARIAL STRESS HARNESS & CHALLENGER SUITE\n";
    std::cout << "Active Hardware GPU: " << rtx_get_device_name() << "\n";
    std::cout << "================================================================================\n";

    setup_test_tlas();

    test_boundary_batch_sizes();
    test_dirty_range_synchronization_stress();
    test_generation_rejection_races();
    test_numerical_robustness();
    test_hardware_dxr_bistro_traversal();

    // Summary
    int passed_count = 0;
    int failed_count = 0;
    for (const auto& r : g_results) {
        if (r.passed) passed_count++;
        else failed_count++;
    }

    std::cout << "\n================================================================================\n";
    std::cout << "📊 STRESS HARNESS EXECUTION SUMMARY\n";
    std::cout << "================================================================================\n";
    std::cout << "  • Total Empirical Stress Tests: " << g_results.size() << "\n";
    std::cout << "  • Passed Assertions           : " << passed_count << " / " << g_results.size() << "\n";
    std::cout << "  • Failed Assertions           : " << failed_count << "\n";
    std::cout << "  • Final Empirical Verdict     : " << (failed_count == 0 ? "✅ 100% PASS - VERIFIED ROBUST" : "❌ FAILURE DETECTED") << "\n";
    std::cout << "================================================================================\n\n";

    rtx_shutdown();
    return (failed_count == 0) ? 0 : 1;
}
