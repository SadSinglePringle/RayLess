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

    // 6. Atomic CSV/JSON/Manifest Artifact Export & Cross-File Validation
    diag.export_all_diagnostics_files();

    // 7. PART 90: Final Evidence Integrity Report
    diag.print_final_diagnostic_summary();

    rtx_shutdown();
    return 0;
}
