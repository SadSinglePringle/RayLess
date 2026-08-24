#define NOMINMAX
#include "rtx_raytracer.h"
#include "rtx_types.h"
#include "benchmark_manifest.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cassert>
#include <random>

// ==============================================================================
// CHALLENGER 1 HARNESS: MILESTONE 2 EMPIRICAL ADVERSARIAL STRESS TEST
// ==============================================================================

struct TestStats {
    int total_tests = 0;
    int passed_tests = 0;
    int failed_tests = 0;

    void record(bool pass, const std::string& name) {
        total_tests++;
        if (pass) {
            passed_tests++;
            std::cout << "  ✅ [PASS] " << name << "\n";
        } else {
            failed_tests++;
            std::cerr << "  ❌ [FAIL] " << name << "\n";
        }
    }
};

void build_test_geometry() {
    int cube_count = 10;
    std::vector<RTXVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<PrimitiveMetadata> metadata;
    std::vector<int32_t> chunk_ids;

    for (int i = 0; i < cube_count; ++i) {
        float cx = float(i) * 3.0f - 15.0f;
        float cz = 0.0f;
        uint32_t base_v = (uint32_t)vertices.size();

        for (int vx = 0; vx < 2; ++vx) {
            for (int vy = 0; vy < 2; ++vy) {
                for (int vz = 0; vz < 2; ++vz) {
                    RTXVertex v;
                    v.px = cx + (vx ? 0.5f : -0.5f);
                    v.py = 1.0f + (vy ? 0.5f : -0.5f);
                    v.pz = cz + (vz ? 0.5f : -0.5f);
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
// SUITE 1: PATHOLOGICAL ACTIVE LANE MASKS & WARP COMPACTION STRESS
// -----------------------------------------------------------------------------
void run_pathological_lane_mask_tests(TestStats& stats) {
    std::cout << "\n================================================================================\n";
    std::cout << "🧪 SUITE 1: PATHOLOGICAL ACTIVE LANE MASKS & WARP COMPACTION STRESS\n";
    std::cout << "================================================================================\n";

    // 1.1: 0 Survivors in entire wave (100% Stage 1 culled)
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        uint32_t N = 64;
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        for (uint32_t i = 0; i < N; ++i) {
            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), 2.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0xFFFFFFFF, 999 }; // Gen mismatch -> Stage 1 cull
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 64) && (cnt.generation_rejected == 64) &&
                    (cnt.angular_rejected == 0) && (cnt.broadphase_rejected == 0) &&
                    (cnt.rayquery_candidates == 0);
        stats.record(pass, "1.1: 0 Survivors in Wave (100% Stage 1 Rejected)");
    }

    // 1.2: 0 Survivors in entire wave (100% Stage 2 Angular culled)
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        uint32_t N = 64;
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        for (uint32_t i = 0; i < N; ++i) {
            // Normal (0,1,0), ray going (0,-2,0) -> antipodal
            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), -2.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0xFFFFFFFF, 1 };
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 64) && (cnt.angular_rejected == 64) && (cnt.rayquery_candidates == 0);
        stats.record(pass, "1.2: 0 Survivors in Wave (100% Stage 2 Angular Rejected)");
    }

    // 1.3: 0 Survivors in entire wave (100% Stage 3 Broadphase culled)
    {
        std::vector<ASTGGPUOccluderAABB> occs(1);
        occs[0].min_x = 100.0f; occs[0].min_y = 100.0f; occs[0].min_z = 100.0f;
        occs[0].max_x = 110.0f; occs[0].max_y = 110.0f; occs[0].max_z = 110.0f;
        occs[0].group_id = 0;
        occs[0].flags = 1;
        rtx_set_dynamic_occluders_gpu(occs.data(), 1);

        uint32_t N = 64;
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        for (uint32_t i = 0; i < N; ++i) {
            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), 2.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0, 1 }; // Target occluder 0 -> misses -> broadphase culled
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 64) && (cnt.broadphase_rejected == 64) && (cnt.rayquery_candidates == 0);
        stats.record(pass, "1.3: 0 Survivors in Wave (100% Stage 3 Broadphase Rejected)");
    }

    // 1.4: Single Survivor at Lane 31 (Warp 0 edge lane)
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        uint32_t N = 32; // Exactly 1 warp
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        for (uint32_t i = 0; i < N; ++i) {
            bool is_survivor = (i == 31);
            float dy = is_survivor ? 2.0f : -2.0f; // i=31 front-facing, i=0..30 back-facing
            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), dy, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0xFFFFFFFF, 1 };
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 32) && (cnt.angular_rejected == 31) &&
                    (cnt.rayquery_candidates == 1) && ((cnt.rayquery_blocked + cnt.rayquery_visible) == 1);
        stats.record(pass, "1.4: Single Survivor at Lane 31 (Warp 0 Edge Lane)");
    }

    // 1.5: Single Survivor at Lane 0 (Warp 0 first lane)
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        uint32_t N = 32;
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        for (uint32_t i = 0; i < N; ++i) {
            bool is_survivor = (i == 0);
            float dy = is_survivor ? 2.0f : -2.0f;
            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), dy, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0xFFFFFFFF, 1 };
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 32) && (cnt.angular_rejected == 31) &&
                    (cnt.rayquery_candidates == 1) && ((cnt.rayquery_blocked + cnt.rayquery_visible) == 1);
        stats.record(pass, "1.5: Single Survivor at Lane 0 (Warp 0 First Lane)");
    }

    // 1.6: Single Survivor at Lane 63 (Warp 1 last lane in 64-threadgroup)
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        uint32_t N = 64;
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        for (uint32_t i = 0; i < N; ++i) {
            bool is_survivor = (i == 63);
            float dy = is_survivor ? 2.0f : -2.0f;
            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), dy, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0xFFFFFFFF, 1 };
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 64) && (cnt.angular_rejected == 63) &&
                    (cnt.rayquery_candidates == 1) && ((cnt.rayquery_blocked + cnt.rayquery_visible) == 1);
        stats.record(pass, "1.6: Single Survivor at Lane 63 (Warp 1 Last Lane)");
    }

    // 1.7: Exhaustive Single-Lane Survivor Sweep Across All 64 Lanes (0 to 63)
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        bool all_lanes_pass = true;
        uint32_t N = 64; // 2 warps
        for (uint32_t target_lane = 0; target_lane < 64; ++target_lane) {
            std::vector<ASTGGPUNode> nodes(N * 2);
            std::vector<ASTGGPUDAGEdge> edges(N);
            std::vector<ASTGGPUVisibilityCandidate> cands(N);

            for (uint32_t i = 0; i < N; ++i) {
                bool is_survivor = (i == target_lane);
                float dy = is_survivor ? 2.0f : -2.0f;
                nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
                nodes[i*2+1] = { float(i), dy, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
                edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
                cands[i] = { i, 0xFFFFFFFF, 1 };
            }
            rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
            rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

            std::vector<ASTGEdgeVisibilityResult> results(N);
            ASTGVisibilityCounters cnt = {};
            rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

            if (cnt.edges_considered != 64 || cnt.angular_rejected != 63 || cnt.rayquery_candidates != 1 ||
                (cnt.rayquery_blocked + cnt.rayquery_visible) != 1) {
                all_lanes_pass = false;
                std::cerr << "❌ Sweep failed at target lane " << target_lane 
                          << " (Considered=" << cnt.edges_considered << " Ang=" << cnt.angular_rejected 
                          << " RQ=" << cnt.rayquery_candidates << ")\n";
                break;
            }
        }
        stats.record(all_lanes_pass, "1.7: Exhaustive Single-Lane Survivor Sweep (Lanes 0..63)");
    }

    // 1.8: Prime-Numbered Lane Survivors (18 of 64 active)
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        uint32_t N = 64;
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        auto is_prime = [](uint32_t n) {
            if (n < 2) return false;
            for (uint32_t d = 2; d * d <= n; ++d) {
                if (n % d == 0) return false;
            }
            return true;
        };

        uint32_t expected_survivors = 0;
        for (uint32_t i = 0; i < N; ++i) {
            bool prime = is_prime(i);
            if (prime) expected_survivors++;
            float dy = prime ? 2.0f : -2.0f;
            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), dy, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0xFFFFFFFF, 1 };
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 64) && (cnt.rayquery_candidates == expected_survivors) &&
                    (cnt.angular_rejected == (64 - expected_survivors));
        stats.record(pass, "1.8: Prime-Numbered Lane Mask (18 Prime Survivors out of 64)");
    }

    // 1.9: 100% Saturation Wave (All 64 Lanes Survive)
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        uint32_t N = 64;
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        for (uint32_t i = 0; i < N; ++i) {
            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), 2.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0xFFFFFFFF, 1 };
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 64) && (cnt.rayquery_candidates == 64) &&
                    (cnt.generation_rejected == 0) && (cnt.angular_rejected == 0) && (cnt.broadphase_rejected == 0) &&
                    ((cnt.rayquery_blocked + cnt.rayquery_visible) == 64);
        stats.record(pass, "1.9: 100% Saturated Wave (All 64 Lanes Traversed to RayQuery)");
    }

    // 1.10: Mixed 4-Wave Divergence (Wave 0: 0 surv, Wave 1: 1 surv, Wave 2: 32 surv, Wave 3: 16 alt surv)
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        uint32_t N = 128; // 4 waves of 32
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        for (uint32_t i = 0; i < N; ++i) {
            uint32_t wave_idx = i / 32;
            uint32_t lane_in_wave = i % 32;

            uint32_t cand_gen = 1;
            float dy = 2.0f;

            if (wave_idx == 0) {
                // Wave 0: All Stage 1 rejected
                cand_gen = 999;
            } else if (wave_idx == 1) {
                // Wave 1: Only lane 31 survives (lanes 0..30 Stage 2 rejected)
                dy = (lane_in_wave == 31) ? 2.0f : -2.0f;
            } else if (wave_idx == 2) {
                // Wave 2: All 32 survive
                dy = 2.0f;
            } else {
                // Wave 3: Alternating (even lanes survive, odd lanes Stage 2 rejected)
                dy = (lane_in_wave % 2 == 0) ? 2.0f : -2.0f;
            }

            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), dy, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0xFFFFFFFF, cand_gen };
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 128) &&
                    (cnt.generation_rejected == 32) &&
                    (cnt.angular_rejected == 47) &&
                    (cnt.rayquery_candidates == 49) &&
                    ((cnt.rayquery_blocked + cnt.rayquery_visible) == 49);
        stats.record(pass, "1.10: Mixed 4-Wave Divergent Topology (0, 1, 32, 16 Survivors)");
    }
}

// -----------------------------------------------------------------------------
// SUITE 2: DENSE DYNAMIC OCCLUDER SCALING & SLAB EDGE CASES
// -----------------------------------------------------------------------------
void run_dense_dynamic_occluder_tests(TestStats& stats) {
    std::cout << "\n================================================================================\n";
    std::cout << "🧪 SUITE 2: DENSE DYNAMIC OCCLUDER SCALING & BROADPHASE SLAB EDGE CASES\n";
    std::cout << "================================================================================\n";

    // 2.1: Dense Dynamic Occluder Count Scaling (up to 4,096 Occluders)
    {
        std::vector<uint32_t> occluder_counts = { 1, 16, 64, 256, 1024, 4096 };
        bool all_occ_pass = true;

        for (uint32_t num_occs : occluder_counts) {
            std::vector<ASTGGPUOccluderAABB> occ_list(num_occs);
            for (uint32_t o = 0; o < num_occs; ++o) {
                float center_x = float(o) * 5.0f;
                occ_list[o].min_x = center_x - 1.0f; occ_list[o].min_y = -1.0f; occ_list[o].min_z = -1.0f;
                occ_list[o].max_x = center_x + 1.0f; occ_list[o].max_y =  1.0f; occ_list[o].max_z =  1.0f;
                occ_list[o].group_id = o;
                occ_list[o].flags = 1; // active
            }
            rtx_set_dynamic_occluders_gpu(occ_list.data(), num_occs);

            // Test 128 rays:
            // Rays with i < 64 intersect occluder i
            // Rays with i >= 64 are in empty gaps between occluders
            uint32_t N = 128;
            std::vector<ASTGGPUNode> nodes(N * 2);
            std::vector<ASTGGPUDAGEdge> edges(N);
            std::vector<ASTGGPUVisibilityCandidate> cands(N);

            for (uint32_t i = 0; i < N; ++i) {
                float rx = (i < 64 && i < num_occs) ? float(i) * 5.0f : (float(i) * 5.0f + 2.5f); // 2.5 is gap
                nodes[i*2] = { rx, -5.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
                nodes[i*2+1] = { rx,  5.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
                edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
                uint32_t target_occ = (i < num_occs) ? i : 0;
                cands[i] = { i, target_occ, 1 };
            }
            rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
            rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

            std::vector<ASTGEdgeVisibilityResult> results(N);
            ASTGVisibilityCounters cnt = {};
            rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

            uint32_t expected_survivors = std::min(64u, num_occs);
            uint32_t expected_culled = N - expected_survivors;

            if (cnt.edges_considered != N || cnt.rayquery_candidates != expected_survivors ||
                cnt.broadphase_rejected != expected_culled) {
                all_occ_pass = false;
                std::cerr << "❌ Dense Occluder Test Failed for " << num_occs << " occluders! Considered=" 
                          << cnt.edges_considered << " Surv=" << cnt.rayquery_candidates 
                          << " Culled=" << cnt.broadphase_rejected << "\n";
                break;
            }
        }
        stats.record(all_occ_pass, "2.1: Dense Dynamic Occluder Scaling (1, 16, 64, 256, 1024, 4096 Occluders)");
    }

    // 2.2: Extreme Slab Geometry Edge Cases
    {
        // Single occluder box [-1, 1]^3
        std::vector<ASTGGPUOccluderAABB> occs(1);
        occs[0].min_x = -1.0f; occs[0].min_y = -1.0f; occs[0].min_z = -1.0f;
        occs[0].max_x =  1.0f; occs[0].max_y =  1.0f; occs[0].max_z =  1.0f;
        occs[0].group_id = 0;
        occs[0].flags = 1;
        rtx_set_dynamic_occluders_gpu(occs.data(), 1);

        struct EdgeTestCase {
            float p0[3];
            float p1[3];
            bool expect_intersect;
            const char* desc;
        };

        EdgeTestCase edge_cases[] = {
            // Internal segment entirely inside box
            { { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f }, true, "Contained Inside Box" },
            // Start inside, end outside
            { { 0.0f, 0.0f, 0.0f }, { 5.0f, 0.0f, 0.0f }, true, "Start Inside, End Outside" },
            // Start outside, end inside
            { { -5.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, true, "Start Outside, End Inside" },
            // Corner grazing within 1e-4 margin
            { { 0.999f, 0.999f, -5.0f }, { 0.999f, 0.999f, 5.0f }, true, "Grazing Corner (Inside Bounds)" },
            // Corner grazing just outside 1e-3 margin
            { { 1.002f, 1.002f, -5.0f }, { 1.002f, 1.002f, 5.0f }, false, "Grazing Corner (Outside Bounds)" },
            // Exact face-parallel segment just outside (+Y face at y=1.005)
            { { -5.0f, 1.005f, 0.0f }, { 5.0f, 1.005f, 0.0f }, false, "Parallel to Face Outside" },
            // Exact face-parallel segment just inside (+Y face at y=0.995)
            { { -5.0f, 0.995f, 0.0f }, { 5.0f, 0.995f, 0.0f }, true, "Parallel to Face Inside" },
            // Degenerate zero-length point inside box
            { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, true, "Degenerate Point Inside" },
            // Degenerate zero-length point outside box
            { { 5.0f, 5.0f, 5.0f }, { 5.0f, 5.0f, 5.0f }, false, "Degenerate Point Outside" },
            // Ray pointing away from box
            { { 2.0f, 0.0f, 0.0f }, { 5.0f, 0.0f, 0.0f }, false, "Collinear Ray Pointing Away" }
        };

        uint32_t num_cases = (uint32_t)(sizeof(edge_cases) / sizeof(edge_cases[0]));
        std::vector<ASTGGPUNode> nodes(num_cases * 2);
        std::vector<ASTGGPUDAGEdge> edges(num_cases);
        std::vector<ASTGGPUVisibilityCandidate> cands(num_cases);

        uint32_t expected_hits = 0;
        for (uint32_t i = 0; i < num_cases; ++i) {
            if (edge_cases[i].expect_intersect) expected_hits++;
            nodes[i*2] = { edge_cases[i].p0[0], edge_cases[i].p0[1], edge_cases[i].p0[2], 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { edge_cases[i].p1[0], edge_cases[i].p1[1], edge_cases[i].p1[2], 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0, 1 };
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(num_cases);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), num_cases, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == num_cases) &&
                    (cnt.rayquery_candidates == expected_hits) &&
                    (cnt.broadphase_rejected == (num_cases - expected_hits));
        stats.record(pass, "2.2: Extreme Broadphase Slab Geometric Edge Cases (10 Boundary Scenarios)");
    }
}

// -----------------------------------------------------------------------------
// SUITE 3: MASSIVE SCALING & TELEMETRY COUNTER CONSERVATION (1 TO 131,072)
// -----------------------------------------------------------------------------
void run_massive_scaling_conservation_tests(TestStats& stats) {
    std::cout << "\n================================================================================\n";
    std::cout << "🧪 SUITE 3: MASSIVE SCALING & ZERO COUNTER LEAKAGE CONSERVATION (1 TO 131,072)\n";
    std::cout << "================================================================================\n";

    // Setup an occluder at [50, 70]^3
    std::vector<ASTGGPUOccluderAABB> occs(1);
    occs[0].min_x = 50.0f; occs[0].min_y = 50.0f; occs[0].min_z = 50.0f;
    occs[0].max_x = 70.0f; occs[0].max_y = 70.0f; occs[0].max_z = 70.0f;
    occs[0].group_id = 0;
    occs[0].flags = 1;
    rtx_set_dynamic_occluders_gpu(occs.data(), 1);

    const uint32_t max_test_size = 131072;
    std::vector<ASTGGPUNode> sweep_nodes(max_test_size * 2);
    std::vector<ASTGGPUDAGEdge> sweep_edges(max_test_size);
    std::vector<ASTGGPUVisibilityCandidate> sweep_cands(max_test_size);

    for (uint32_t i = 0; i < max_test_size; ++i) {
        uint32_t outcome = i % 4;
        float dy = (outcome == 1) ? -2.0f : 2.0f; // Angular cull if 1
        uint32_t cand_gen = (outcome == 0) ? 999 : 1; // Gen mismatch if 0
        uint32_t obj_id = (outcome == 2) ? 0 : 0xFFFFFFFF; // Broadphase cull if 2 (ray at [-5,5] misses occ at [50,70])

        float px = float(i % 100) * 0.1f - 5.0f;
        float py = 2.0f;
        float pz = float(i / 100) * 0.1f - 5.0f;

        sweep_nodes[i*2] = { px, py, pz, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        sweep_nodes[i*2+1] = { px, py + dy, pz, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        sweep_edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
        sweep_cands[i] = { i, obj_id, cand_gen };
    }

    rtx_upload_astg_nodes(sweep_nodes.data(), 0, (uint32_t)sweep_nodes.size());
    rtx_upload_astg_edges(sweep_edges.data(), 0, (uint32_t)sweep_edges.size());

    std::vector<uint32_t> test_scales = {
        1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 33,
        63, 64, 65, 127, 128, 129, 255, 256, 257,
        500, 511, 512, 513, 1023, 1024, 1025,
        2048, 4096, 8192, 16384, 32768, 65536, 100000, 131071, 131072
    };

    bool all_scales_pass = true;
    for (uint32_t b_size : test_scales) {
        std::vector<ASTGEdgeVisibilityResult> results(b_size);
        ASTGVisibilityCounters cnt = {};
        RTGPUTimings timings = {};

        rtx_trace_candidates_batch(sweep_cands.data(), b_size, results.data(), &cnt, &timings);

        uint64_t total = cnt.edges_considered;
        uint64_t sum_b = (uint64_t)cnt.generation_rejected +
                         (uint64_t)cnt.angular_rejected +
                         (uint64_t)cnt.broadphase_rejected +
                         (uint64_t)cnt.rayquery_candidates;

        int64_t leak_b = (int64_t)total - (int64_t)sum_b;
        int64_t leak_d = (int64_t)cnt.rayquery_candidates - ((int64_t)cnt.rayquery_blocked + (int64_t)cnt.rayquery_visible);

        if (total != b_size || leak_b != 0 || leak_d != 0) {
            all_scales_pass = false;
            std::cerr << "❌ Conservation Invariant Failure at Batch Size " << b_size 
                      << ": Dispatched=" << b_size << " Total=" << total 
                      << " LeakB=" << leak_b << " LeakD=" << leak_d << "\n";
            break;
        }
    }
    stats.record(all_scales_pass, "3.1: Strict Conservation Invariant Across 36 Batch Sizes (1 to 131,072)");
}

// -----------------------------------------------------------------------------
// SUITE 4: RAPID CONCURRENCY, RE-DISPATCH & ZERO COUNTER RESIDUAL STRESS
// -----------------------------------------------------------------------------
void run_concurrency_stress_tests(TestStats& stats) {
    std::cout << "\n================================================================================\n";
    std::cout << "🧪 SUITE 4: RAPID CONCURRENCY & ZERO COUNTER RESIDUAL STRESS (100 ITERATIONS)\n";
    std::cout << "================================================================================\n";

    const uint32_t max_size = 65536;
    std::vector<ASTGGPUNode> nodes(max_size * 2);
    std::vector<ASTGGPUDAGEdge> edges(max_size);
    std::vector<ASTGGPUVisibilityCandidate> cands(max_size);

    std::mt19937 rng(1337);
    std::uniform_int_distribution<uint32_t> size_dist(1, max_size);
    std::uniform_int_distribution<uint32_t> outcome_dist(0, 3);

    for (uint32_t i = 0; i < max_size; ++i) {
        nodes[i*2] = { float(i % 100), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        nodes[i*2+1] = { float(i % 100), 2.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
        cands[i] = { i, 0xFFFFFFFF, 1 };
    }
    rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
    rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

    bool concurrency_pass = true;
    int num_iters = 100;

    for (int iter = 0; iter < num_iters; ++iter) {
        uint32_t b_size = size_dist(rng);

        // Perturb candidates dynamically
        for (uint32_t i = 0; i < b_size; ++i) {
            uint32_t out = outcome_dist(rng);
            cands[i].transport_generation = (out == 0) ? 999 : 1;
        }

        std::vector<ASTGEdgeVisibilityResult> results(b_size);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), b_size, results.data(), &cnt, nullptr);

        uint64_t total = cnt.edges_considered;
        uint64_t sum_b = (uint64_t)cnt.generation_rejected +
                         (uint64_t)cnt.angular_rejected +
                         (uint64_t)cnt.broadphase_rejected +
                         (uint64_t)cnt.rayquery_candidates;

        int64_t leak_b = (int64_t)total - (int64_t)sum_b;
        int64_t leak_d = (int64_t)cnt.rayquery_candidates - ((int64_t)cnt.rayquery_blocked + (int64_t)cnt.rayquery_visible);

        if (total != b_size || leak_b != 0 || leak_d != 0) {
            concurrency_pass = false;
            std::cerr << "❌ Concurrency Failure at Iteration " << iter << " (Batch Size " << b_size 
                      << "): Total=" << total << " LeakB=" << leak_b << " LeakD=" << leak_d << "\n";
            break;
        }
    }
    stats.record(concurrency_pass, "4.1: 100 Rapid Back-to-Back Randomized Dispatches (Strict Zero Residual Leakage)");
}

// -----------------------------------------------------------------------------
// SUITE 5: ADVANCED ADVERSARIAL OUT-OF-BOUNDS & DYNAMIC INVARIANTS
// -----------------------------------------------------------------------------
void run_adversarial_invariants_tests(TestStats& stats) {
    std::cout << "\n================================================================================\n";
    std::cout << "🧪 SUITE 5: ADVANCED ADVERSARIAL OUT-OF-BOUNDS & DYNAMIC INVARIANTS\n";
    std::cout << "================================================================================\n";

    // 5.1: Inactive Occluder Flags (flags = 0 -> ray passes through without broadphase cull)
    {
        std::vector<ASTGGPUOccluderAABB> occs(1);
        occs[0].min_x = -1.0f; occs[0].min_y = -1.0f; occs[0].min_z = -1.0f;
        occs[0].max_x =  1.0f; occs[0].max_y =  1.0f; occs[0].max_z =  1.0f;
        occs[0].group_id = 0;
        occs[0].flags = 0; // INACTIVE!
        rtx_set_dynamic_occluders_gpu(occs.data(), 1);

        uint32_t N = 4;
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        for (uint32_t i = 0; i < N; ++i) {
            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), 2.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
            cands[i] = { i, 0, 1 }; // queries occluder 0, which is inactive -> flags&1 == 0
        }
        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        // Since occluder 0 is inactive, it is ignored and candidate survives broadphase to Stage 4
        bool pass = (cnt.edges_considered == 4) && (cnt.rayquery_candidates == 4) && (cnt.broadphase_rejected == 0);
        stats.record(pass, "5.1: Inactive Occluder Ignored in Broadphase (flags = 0)");
    }

    // 5.2: Structural Out-of-Bounds Node & Edge Guarding
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        uint32_t N = 4;
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        for (uint32_t i = 0; i < N; ++i) {
            nodes[i*2] = { float(i), 0.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            nodes[i*2+1] = { float(i), 2.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
            edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
        }
        // Candidate 0: Valid
        cands[0] = { 0, 0xFFFFFFFF, 1 };
        // Candidate 1: Out-of-bounds edge_id
        cands[1] = { 999999, 0xFFFFFFFF, 1 };
        // Candidate 2: Valid
        cands[2] = { 2, 0xFFFFFFFF, 1 };
        // Candidate 3: Edge pointing to out-of-bounds node
        edges[3].dest_node_id = 999999;
        cands[3] = { 3, 0xFFFFFFFF, 1 };

        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 4) && (cnt.generation_rejected == 2) &&
                    (cnt.rayquery_candidates == 2) &&
                    (results[1].visibility_state == 2) && (results[3].visibility_state == 2);
        stats.record(pass, "5.2: Structural Out-of-Bounds Node & Edge Safety (Stage 1 Gen Guard)");
    }

    // 5.3: Hardware DXR Triangle Hit vs Miss Classification Accuracy
    {
        rtx_set_dynamic_occluders_gpu(nullptr, 0);
        // The test BLAS has 10 cubes at x = i * 3.0 - 15.0 (for i=0..9, so cx = -15, -12, -9, -6, -3, 0, 3, 6, 9, 12), y in [0.5, 1.5], z in [-0.5, 0.5]
        uint32_t N = 4;
        std::vector<ASTGGPUNode> nodes(N * 2);
        std::vector<ASTGGPUDAGEdge> edges(N);
        std::vector<ASTGGPUVisibilityCandidate> cands(N);

        // Ray 0: Shoots through Cube 5 at cx=0: from (0, 3, 0) to (0, -1, 0) -> HITS TRIANGLES (BLOCKED, state=1)
        nodes[0] = { 0.0f, 3.0f, 0.0f, 1, 0.0f, -1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        nodes[1] = { 0.0f, -1.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        edges[0] = { 0, 1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
        cands[0] = { 0, 0xFFFFFFFF, 1 };

        // Ray 1: Shoots through gap at x=1.5: from (1.5, 3, 0) to (1.5, -1, 0) -> MISSES ALL CUBES (VISIBLE, state=0)
        nodes[2] = { 1.5f, 3.0f, 0.0f, 1, 0.0f, -1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        nodes[3] = { 1.5f, -1.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        edges[1] = { 2, 3, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
        cands[1] = { 1, 0xFFFFFFFF, 1 };

        // Ray 2: Shoots through Cube 6 at cx=3: from (3, 3, 0) to (3, -1, 0) -> HITS TRIANGLES (BLOCKED, state=1)
        nodes[4] = { 3.0f, 3.0f, 0.0f, 1, 0.0f, -1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        nodes[5] = { 3.0f, -1.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        edges[2] = { 4, 5, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
        cands[2] = { 2, 0xFFFFFFFF, 1 };

        // Ray 3: Shoots through gap at x=4.5: from (4.5, 3, 0) to (4.5, -1, 0) -> MISSES ALL CUBES (VISIBLE, state=0)
        nodes[6] = { 4.5f, 3.0f, 0.0f, 1, 0.0f, -1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        nodes[7] = { 4.5f, -1.0f, 0.0f, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        edges[3] = { 6, 7, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
        cands[3] = { 3, 0xFFFFFFFF, 1 };

        rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
        rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

        std::vector<ASTGEdgeVisibilityResult> results(N);
        ASTGVisibilityCounters cnt = {};
        rtx_trace_candidates_batch(cands.data(), N, results.data(), &cnt, nullptr);

        bool pass = (cnt.edges_considered == 4) && (cnt.rayquery_candidates == 4) &&
                    (cnt.rayquery_blocked == 2) && (cnt.rayquery_visible == 2) &&
                    (results[0].visibility_state == 1) && (results[1].visibility_state == 0) &&
                    (results[2].visibility_state == 1) && (results[3].visibility_state == 0);
        stats.record(pass, "5.3: Hardware DXR Triangle Hit vs Miss Classification Accuracy");
    }
}

// -----------------------------------------------------------------------------
// SUITE 6: HARDWARE DXR TRAVERSAL BENCHMARK (131,072 CANDIDATES)
// -----------------------------------------------------------------------------
void run_hardware_benchmark(TestStats& stats) {
    std::cout << "\n================================================================================\n";
    std::cout << "🚀 SUITE 6: HARDWARE DXR 1.1 PERFORMANCE BENCHMARK (RTX 4070)\n";
    std::cout << "================================================================================\n";

    const uint32_t bench_count = 131072;
    std::vector<ASTGGPUNode> nodes(bench_count * 2);
    std::vector<ASTGGPUDAGEdge> edges(bench_count);
    std::vector<ASTGGPUVisibilityCandidate> cands(bench_count);

    for (uint32_t i = 0; i < bench_count; ++i) {
        float px = float(i % 100) * 0.1f - 5.0f;
        float py = 2.0f;
        float pz = float(i / 100) * 0.1f - 5.0f;
        nodes[i*2] = { px, py, pz, 1, 0.0f, 1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        nodes[i*2+1] = { px, py - 2.0f, pz, 1, 0.0f, -1.0f, 0.0f, 1, 0.8f, 0.8f, 0.8f, 0xFFFFFFFF };
        edges[i] = { i*2, i*2+1, 1, 0, 0, 0, 0xFFFFFFFF, 1 };
        cands[i] = { i, 0xFFFFFFFF, 1 };
    }
    rtx_upload_astg_nodes(nodes.data(), 0, (uint32_t)nodes.size());
    rtx_upload_astg_edges(edges.data(), 0, (uint32_t)edges.size());

    // Reset occluders to none
    rtx_set_dynamic_occluders_gpu(nullptr, 0);

    std::vector<ASTGEdgeVisibilityResult> results(bench_count);
    std::vector<double> timings;
    ASTGVisibilityCounters final_cnt = {};

    for (int iter = 0; iter < 30; ++iter) {
        RTGPUTimings t = {};
        rtx_trace_candidates_batch(cands.data(), bench_count, results.data(), &final_cnt, &t);
        timings.push_back(t.rt_traversal_ms);
    }

    std::sort(timings.begin(), timings.end());
    double mean = 0.0;
    for (double v : timings) mean += v;
    mean /= timings.size();
    double median = timings[timings.size() / 2];
    double p99 = timings[(size_t)(timings.size() * 0.99)];
    double mrays_sec = (bench_count / (median / 1000.0)) / 1000000.0;

    std::cout << "  • Hardware Device:             " << rtx_get_device_name() << "\n";
    std::cout << "  • Batch Size:                  " << bench_count << " Candidates\n";
    std::cout << "  • Dispatch Time:               Mean " << std::fixed << std::setprecision(3) << mean 
              << " ms | Median " << median << " ms | P99 " << p99 << " ms\n";
    std::cout << "  • Traversal Throughput:        " << std::fixed << std::setprecision(2) << mrays_sec << " Million Candidates / Sec\n";
    std::cout << "  • RayQuery Blocked:            " << final_cnt.rayquery_blocked << "\n";
    std::cout << "  • RayQuery Visible:            " << final_cnt.rayquery_visible << "\n";

    bool pass = (mrays_sec > 100.0); // At least 100 MRays/sec
    stats.record(pass, "6.1: High-Throughput Traversal Benchmark (>100 MRays/sec)");
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "⚔️  CHALLENGER 1: MILESTONE 2 (R3) EMPIRICAL ADVERSARIAL HARNESS\n";
    std::cout << "================================================================================\n\n";

    if (!rtx_init()) {
        std::cerr << "❌ Failed to initialize DXR 1.1 Hardware RT Cores.\n";
        return 1;
    }

    build_test_geometry();

    TestStats stats;
    run_pathological_lane_mask_tests(stats);
    run_dense_dynamic_occluder_tests(stats);
    run_massive_scaling_conservation_tests(stats);
    run_concurrency_stress_tests(stats);
    run_adversarial_invariants_tests(stats);
    run_hardware_benchmark(stats);

    rtx_shutdown();

    std::cout << "\n================================================================================\n";
    std::cout << "SUMMARY: " << stats.passed_tests << " / " << stats.total_tests << " Passed ("
              << (stats.failed_tests == 0 ? "100% SUCCESS" : "FAILURES DETECTED") << ")\n";
    std::cout << "================================================================================\n";

    return (stats.failed_tests == 0) ? 0 : 1;
}
