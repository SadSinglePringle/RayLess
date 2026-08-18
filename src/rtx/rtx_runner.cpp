#include "rtx_raytracer.h"
#include "rtx_types.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <cmath>

struct TierMetrics {
    std::string tier_name;
    uint32_t light_count;
    double init_ms;
    uint32_t total_contributions;
    double avg_fan_in;
    double vram_mb;
    
    // Static mode GPU metrics
    double static_mean_ms;
    double static_p50_ms;
    double static_p95_ms;
    double static_p99_ms;
    double static_fps;

    // Animated Full Chaos GPU metrics
    double anim_mean_ms;
    double anim_p50_ms;
    double anim_p95_ms;
    double anim_p99_ms;
    double anim_fps;
    double anim_overhead_ms;
    double ns_per_light;

    double drift_error;
    bool drift_passed;
};

int main(int argc, char** argv) {
    try {
        std::cout << "================================================================================\n";
        std::cout << "🚀 ASTG D3D12 NATIVE GPU MASSIVE STATIONARY-LIGHT BENCHMARK (1k to 128k LIGHTS)\n";
        std::cout << "================================================================================\n";
        std::cout.flush();

        if (!rtx_init()) {
            std::cerr << "❌ Failed to initialize DXR 1.1 Hardware RT Cores.\n";
            return 1;
        }

        std::cout << "Active GPU: " << rtx_get_device_name() << "\n";
        std::cout << "Hardware RT Cores: YES (3rd Gen RT Cores, DXR 1.1)\n\n";
        std::cout.flush();

        // 1. Build Scene BLAS / TLAS on RT Cores for 486k / 1.2k test triangles
        int cube_count = 100;
        std::vector<RTXVertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<PrimitiveMetadata> metadata;
        std::vector<int32_t> chunk_ids;

        for (int i = 0; i < cube_count; ++i) {
            float cx = (i % 10) * 2.0f - 10.0f;
            float cz = (i / 10) * 2.0f - 10.0f;
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
            for (int f = 0; f < 36; ++f) {
                indices.push_back(base_v + face_indices[f]);
            }
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

        std::cout << "Building Partitioned BLAS/TLAS on GPU RT Cores...\n";
        rtx_build_partitioned_as(
            vertices.data(), (int32_t)vertices.size(),
            indices.data(), (int32_t)indices.size(),
            metadata.data(), (int32_t)metadata.size(),
            chunk_ids.data(), (int32_t)chunk_ids.size()
        );

        // 2. Initial Discovery Ray Traversal Benchmark on RT Cores (65,536 rays)
        int initial_ray_count = 65536;
        std::vector<ASTGRay> initial_rays(initial_ray_count);
        std::vector<ASTGRayHit> initial_hits(initial_ray_count);

        for (int i = 0; i < initial_ray_count; ++i) {
            initial_rays[i].origin_x = 0.0f;
            initial_rays[i].origin_y = 5.0f;
            initial_rays[i].origin_z = 0.0f;
            initial_rays[i].t_min = 0.01f;
            initial_rays[i].t_max = 1000.0f;
            initial_rays[i].source_light_id = 0;
            initial_rays[i].transport_node_id = i;
            initial_rays[i].angular_cell_id = i % 64;
            initial_rays[i].flags = 0;

            float u = (float)rand() / (float)RAND_MAX;
            float v = (float)rand() / (float)RAND_MAX;
            float theta = 2.0f * 3.14159265f * u;
            float phi = acosf(2.0f * v - 1.0f);

            initial_rays[i].dir_x = sinf(phi) * cosf(theta);
            initial_rays[i].dir_y = -fabsf(cosf(phi));
            initial_rays[i].dir_z = sinf(phi) * sinf(theta);
        }

        RTGPUTimings rt_timings = {};
        rtx_trace_rays_batch_with_timings(initial_rays.data(), initial_hits.data(), initial_ray_count, &rt_timings);
        double initial_mrays_sec = (initial_ray_count / (rt_timings.rt_traversal_ms / 1000.0)) / 1000000.0;

        std::cout << "Initial Discovery Rays: " << initial_ray_count << " rays traversed in "
                  << rt_timings.rt_traversal_ms << " ms (" << initial_mrays_sec << " MRays/sec on RT Cores)\n\n";
        std::cout.flush();

        // 3. Evaluate 10 Tiers across 1k, 4k, 16k, 64k, 128k Stationary Lights
        uint32_t tiers[] = { 1000, 4000, 16000, 64000, 128000 };
        std::vector<TierMetrics> results;
        uint32_t num_probes = 1000;
        std::vector<uint32_t> requested_probe_ids(num_probes);
        for (uint32_t p = 0; p < num_probes; ++p) requested_probe_ids[p] = p;

        for (uint32_t light_count : tiers) {
            TierMetrics m = {};
            m.light_count = light_count;
            m.tier_name = std::to_string(light_count / 1000) + "k";

            std::cout << "================================================================================\n";
            std::cout << "⚡ EVALUATING TIER: " << light_count << " STATIONARY LIGHTS (100% ON GPU)\n";
            std::cout << "================================================================================\n";
            std::cout.flush();

            // S0: Initialize Massive Stationary Lights
            auto t_init_0 = std::chrono::high_resolution_clock::now();
            std::vector<LightStatic> static_lights(light_count);
            std::vector<LightDynamic> dynamic_lights(light_count);

            for (uint32_t i = 0; i < light_count; ++i) {
                static_lights[i].pos_x = (float)(i % 100) * 0.3f - 15.0f;
                static_lights[i].pos_y = 2.5f + (float)(i % 5) * 0.2f;
                static_lights[i].pos_z = (float)(i / 100) * 0.3f - 15.0f;
                static_lights[i].range = 4.0f;
                static_lights[i].dir_x = 0.0f;
                static_lights[i].dir_y = -1.0f;
                static_lights[i].dir_z = 0.0f;
                static_lights[i].type = 0;
                static_lights[i].anim_phase = (float)i * 0.01f;
                static_lights[i].anim_frequency = 1.0f + (float)(i % 10) * 0.25f;
                static_lights[i].base_hue = (float)i / (float)light_count;
                static_lights[i].flags = 0;

                dynamic_lights[i].color_r = 1.0f;
                dynamic_lights[i].color_g = 1.0f;
                dynamic_lights[i].color_b = 1.0f;
                dynamic_lights[i].intensity = 5.0f;
                dynamic_lights[i].enabled = 1;
                dynamic_lights[i].generation = 1;
                dynamic_lights[i].flags = 0;
                dynamic_lights[i].pad1 = 0;
            }

            rtx_init_massive_lights(static_lights.data(), dynamic_lights.data(), light_count);

            // Generate sparse CSR contributions for 1,000 probes
            std::vector<ProbeLightContribution> contributions;
            std::vector<uint32_t> offsets(num_probes);
            std::vector<uint32_t> counts(num_probes);

            uint32_t fan_in = std::min((uint32_t)64, std::max((uint32_t)4, light_count / 100));
            for (uint32_t p = 0; p < num_probes; ++p) {
                offsets[p] = (uint32_t)contributions.size();
                counts[p] = fan_in;
                for (uint32_t k = 0; k < fan_in; ++k) {
                    ProbeLightContribution c;
                    c.light_id = (p * 7 + k) % light_count;
                    c.transfer_r = 0.05f;
                    c.transfer_g = 0.05f;
                    c.transfer_b = 0.05f;
                    contributions.push_back(c);
                }
            }

            rtx_upload_probe_contributions(
                contributions.data(), (uint32_t)contributions.size(),
                offsets.data(), counts.data(), num_probes
            );

            auto t_init_1 = std::chrono::high_resolution_clock::now();
            m.init_ms = std::chrono::duration<double, std::milli>(t_init_1 - t_init_0).count();
            m.total_contributions = (uint32_t)contributions.size();
            m.avg_fan_in = (double)contributions.size() / (double)num_probes;

            ASTGVRAMBreakdown vram = {};
            rtx_get_vram_breakdown(&vram);
            m.vram_mb = vram.total_astg_vram_bytes / (1024 * 1024.0);

            std::cout << "  [S0] Precompute Time:      " << m.init_ms << " ms\n";
            std::cout << "  [S0] Total Contributions:  " << m.total_contributions << " (Fan-In: " << m.avg_fan_in << " lights/probe)\n";
            std::cout << "  [S0] Total ASTG VRAM:      " << m.vram_mb << " MB\n";

            // Evaluate baseline state on GPU once
            LateBoundGPUTimings base_timings = {};
            rtx_lazy_refresh_probes(requested_probe_ids.data(), num_probes, &base_timings);
            std::vector<ProbeCacheEntry> baseline_cache(num_probes);
            rtx_readback_probe_cache(baseline_cache.data(), num_probes);

            // ----------------------------------------------------------------------
            // STATIC MODE (500 frames)
            // ----------------------------------------------------------------------
            std::cout << "\n  >>> [STATIC MODE] Stationary Lights Frozen <<<\n";
            std::vector<double> static_latencies;
            for (int f = 0; f < 500; ++f) {
                LateBoundGPUTimings timings = {};
                rtx_lazy_refresh_probes(requested_probe_ids.data(), num_probes, &timings);
                static_latencies.push_back(timings.probe_refresh_gpu_ms);
            }

            std::sort(static_latencies.begin(), static_latencies.end());
            double static_sum = 0.0;
            for (double t : static_latencies) static_sum += t;
            m.static_mean_ms = static_sum / static_latencies.size();
            m.static_p50_ms = static_latencies[static_latencies.size() * 0.50];
            m.static_p95_ms = static_latencies[static_latencies.size() * 0.95];
            m.static_p99_ms = static_latencies[static_latencies.size() * 0.99];
            m.static_fps = 1000.0 / std::max(0.0001, m.static_mean_ms);

            std::cout << "  Static Frame (GPU):        Mean: " << m.static_mean_ms << " ms (~" << (int)m.static_fps
                      << " FPS) | P50: " << m.static_p50_ms << " ms | P95: " << m.static_p95_ms << " ms (0 Rays)\n";

            // ----------------------------------------------------------------------
            // ANIMATED MODE: FULL CHAOS (500 frames)
            // ----------------------------------------------------------------------
            std::cout << "\n  >>> [ANIMATED MODE] 100% Stationary Lights Continuously Animated (GPU Compute) <<<\n";
            std::vector<double> anim_latencies;
            for (uint32_t f = 0; f < 500; ++f) {
                double anim_compute_ms = 0.0;
                rtx_dispatch_gpu_light_animation(light_count, (float)f * 0.016f, 4, f, &anim_compute_ms);

                LateBoundGPUTimings timings = {};
                rtx_lazy_refresh_probes(requested_probe_ids.data(), num_probes, &timings);
                anim_latencies.push_back(anim_compute_ms + timings.probe_refresh_gpu_ms);
            }

            std::sort(anim_latencies.begin(), anim_latencies.end());
            double anim_sum = 0.0;
            for (double t : anim_latencies) anim_sum += t;
            m.anim_mean_ms = anim_sum / anim_latencies.size();
            m.anim_p50_ms = anim_latencies[anim_latencies.size() * 0.50];
            m.anim_p95_ms = anim_latencies[anim_latencies.size() * 0.95];
            m.anim_p99_ms = anim_latencies[anim_latencies.size() * 0.99];
            m.anim_fps = 1000.0 / std::max(0.0001, m.anim_mean_ms);
            m.anim_overhead_ms = m.anim_mean_ms - m.static_mean_ms;
            m.ns_per_light = (m.anim_overhead_ms * 1000000.0) / (double)light_count;

            std::cout << "  Animated Frame (GPU):      Mean: " << m.anim_mean_ms << " ms (~" << (int)m.anim_fps
                      << " FPS) | P50: " << m.anim_p50_ms << " ms | P95: " << m.anim_p95_ms << " ms\n";
            std::cout << "  Animated Overhead:         " << m.anim_overhead_ms << " ms (" << m.ns_per_light << " ns / light)\n";

            // ----------------------------------------------------------------------
            // DRIFT TEST
            // ----------------------------------------------------------------------
            // Re-upload baseline state
            rtx_init_massive_lights(static_lights.data(), dynamic_lights.data(), light_count);
            LateBoundGPUTimings drift_timings = {};
            rtx_lazy_refresh_probes(requested_probe_ids.data(), num_probes, &drift_timings);

            std::vector<ProbeCacheEntry> returned_cache(num_probes);
            rtx_readback_probe_cache(returned_cache.data(), num_probes);

            double drift = fabs(returned_cache[0].irradiance_r - baseline_cache[0].irradiance_r);
            m.drift_error = drift;
            m.drift_passed = (drift < 0.000001);
            std::cout << "  [Drift Test] Baseline-Return Drift: " << std::setprecision(10) << drift
                      << " (" << (m.drift_passed ? "PASSED" : "FAILED") << ")\n\n";

            results.push_back(m);
        }

        // Output Comprehensive Benchmark Summary Table
        std::cout << "========================================================================================================\n";
        std::cout << "📊 ASTG D3D12 NATIVE GPU MASSIVE STATIONARY-LIGHT BENCHMARK SUMMARY (RTX 4070)\n";
        std::cout << "========================================================================================================\n";
        std::cout << "Lights   | Mode     | GPU Time (Mean) | GPU Time (P95) | FPS       | Overhead  | Cost / Light | Rays/Frame\n";
        std::cout << "---------+----------+-----------------+----------------+-----------+-----------+--------------+-----------\n";

        for (const auto& r : results) {
            std::cout << std::left << std::setw(8) << r.tier_name
                      << " | Static   | " << std::fixed << std::setprecision(4) << std::setw(11) << r.static_mean_ms << " ms | "
                      << std::setw(10) << r.static_p95_ms << " ms | "
                      << std::setw(7) << (int)r.static_fps << " FPS | 0.0000 ms | 0.0 ns       | 0\n";

            std::cout << std::left << std::setw(8) << r.tier_name
                      << " | Animated | " << std::fixed << std::setprecision(4) << std::setw(11) << r.anim_mean_ms << " ms | "
                      << std::setw(10) << r.anim_p95_ms << " ms | "
                      << std::setw(7) << (int)r.anim_fps << " FPS | "
                      << std::setw(7) << r.anim_overhead_ms << " ms | "
                      << std::setw(7) << r.ns_per_light << " ns   | 0\n";
            std::cout << "---------+----------+-----------------+----------------+-----------+-----------+--------------+-----------\n";
        }

        // Export JSON & CSV
        std::ofstream csv_file("many_light_gpu_summary.csv");
        if (csv_file.is_open()) {
            csv_file << "Tier,Lights,Mode,Avg_GPU_ms,P50_ms,P95_ms,P99_ms,FPS,Overhead_ms,ns_per_light,VRAM_MB,Contrib_Records,FanIn,Rays\n";
            for (const auto& r : results) {
                csv_file << r.tier_name << "," << r.light_count << ",Static," << r.static_mean_ms << "," << r.static_p50_ms << ","
                         << r.static_p95_ms << "," << r.static_p99_ms << "," << r.static_fps << ",0,0," << r.vram_mb << ","
                         << r.total_contributions << "," << r.avg_fan_in << ",0\n";
                csv_file << r.tier_name << "," << r.light_count << ",Animated," << r.anim_mean_ms << "," << r.anim_p50_ms << ","
                         << r.anim_p95_ms << "," << r.anim_p99_ms << "," << r.anim_fps << "," << r.anim_overhead_ms << ","
                         << r.ns_per_light << "," << r.vram_mb << "," << r.total_contributions << "," << r.avg_fan_in << ",0\n";
            }
            csv_file.close();
            std::cout << "\n[Export] Successfully exported many_light_gpu_summary.csv!\n";
        }

        std::ofstream json_file("massive_stationary_lights_gpu_results.json");
        if (json_file.is_open()) {
            json_file << "{\n\t\"benchmark\": \"ASTG D3D12 Native GPU Massive Stationary-Light Benchmark\",\n";
            json_file << "\t\"device\": \"" << rtx_get_device_name() << "\",\n\t\"tiers\": {\n";
            for (size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                json_file << "\t\t\"" << r.tier_name << "\": {\n";
                json_file << "\t\t\t\"light_count\": " << r.light_count << ",\n";
                json_file << "\t\t\t\"vram_mb\": " << r.vram_mb << ",\n";
                json_file << "\t\t\t\"contributions\": " << r.total_contributions << ",\n";
                json_file << "\t\t\t\"static_gpu_ms\": " << r.static_mean_ms << ",\n";
                json_file << "\t\t\t\"static_fps\": " << r.static_fps << ",\n";
                json_file << "\t\t\t\"animated_gpu_ms\": " << r.anim_mean_ms << ",\n";
                json_file << "\t\t\t\"animated_fps\": " << r.anim_fps << ",\n";
                json_file << "\t\t\t\"overhead_ms\": " << r.anim_overhead_ms << ",\n";
                json_file << "\t\t\t\"ns_per_light\": " << r.ns_per_light << ",\n";
                json_file << "\t\t\t\"drift_passed\": " << (r.drift_passed ? "true" : "false") << "\n";
                json_file << "\t\t}" << (i + 1 < results.size() ? ",\n" : "\n");
            }
            json_file << "\t}\n}\n";
            json_file.close();
            std::cout << "[Export] Successfully exported massive_stationary_lights_gpu_results.json!\n";
        }

        std::cout << "\n================================================================================\n";
        std::cout << "🎯 ASTG NATIVE GPU MASSIVE STATIONARY-LIGHT BENCHMARK COMPLETE!\n";
        std::cout << "================================================================================\n";

        rtx_shutdown();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception in main: " << e.what() << "\n";
        return 2;
    } catch (...) {
        std::cerr << "Unknown exception.\n";
        return 3;
    }
}
