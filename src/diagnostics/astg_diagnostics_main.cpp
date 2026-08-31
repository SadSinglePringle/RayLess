#include "astg_transport_diagnostics.h"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "================================================================================\n";
    std::cout << "🚀 ASTG EVIDENCE INTEGRITY & ANTI-OVERSTATEMENT BENCHMARK SUITE\n";
    std::cout << "================================================================================\n\n";

    if (!rtx_init()) {
        std::cerr << "❌ Failed to initialize DXR 1.1 Hardware RT Core Ray Tracer.\n";
        return 1;
    }

    std::cout << "Active GPU: " << rtx_get_device_name() << "\n";
    std::cout << "Hardware RT Cores Active: " << (rtx_is_hardware_active() ? "YES" : "NO") << "\n\n";

    ASTGTransportDiagnostics diag;
    if (!diag.initialize_scene("assets/bistro/bistro.gltf", "assets/bistro/bistro.bin")) {
        std::cerr << "❌ Failed to load Bistro scene!\n";
        rtx_shutdown();
        return 1;
    }

    // 1. Full 8-Tier Scaling Ladder with Immutable Result Sealing
    diag.run_full_tier_scaling_diagnostics();

    // 2. PART A: Measurement Integrity, Workload Separation & Memory Accounting
    diag.test_measurement_integrity_and_accounting();

    // 3. PART B: Late-Bound Pruned-Source Adversarial Stress & Guard-Band Progression
    diag.test_late_bound_pruned_source_stress();

    // 4. PART C & D: 128k Large-Scale Safe Regeneration & Independent Rebuild Equivalence
    diag.test_large_scale_regeneration_and_discovery();

    // 5. PART E: Path Provenance Preservation, Surgical Invalidation & Accounting Closure
    diag.test_path_provenance_preservation();

    // 6. PART F: ASTG Regeneration Path-Stitching Validation
    diag.test_path_stitching_regeneration();

    // 7. PART G: Partial Transport Segment Reuse & Stitched Frontier Continuation
    diag.test_partial_transport_segment_reuse();

    // 8. PART H: Moving Lights via Dynamic Ingress & Persistent Transport Reuse
    diag.test_dynamic_light_transport_and_reuse();

    // 9. PART I: ASTG Dynamic Object Occlusion for Bounding-Box Groups
    diag.test_dynamic_object_occlusion();

    // 10. PART J: ASTG Dynamic Occlusion Modes & Angular B0 Occlusion
    diag.test_dynamic_occlusion_modes_and_angular_b0();
    diag.print_dynamic_occlusion_modes_report();

    // 11. PART K: ASTG Dynamic Surface Receiver Probes for Moving Objects (Phase 6)
    diag.test_dynamic_surface_receivers();
    diag.print_dynamic_surface_receivers_report();

    // 12. PART J & K: GPU Runtime Correctness, Persistence & Timestamps
    diag.test_parts_jk_gpu_runtime_correctness_and_persistence();

    // 13. PART L: ASTG GPU-First Transport Execution & Split Accounting
    diag.test_gpu_first_transport_benchmarks();

    // 13. Atomic CSV/JSON/Manifest Artifact Export & Cross-File Validation
    diag.export_all_diagnostics_files();

    // 12. PART 90: Final Evidence Integrity Report
    diag.print_final_diagnostic_summary();

    rtx_shutdown();
    return 0;
}
