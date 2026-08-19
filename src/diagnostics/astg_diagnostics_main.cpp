#include "astg_transport_diagnostics.h"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "================================================================================\n";
    std::cout << "🚀 ASTG MEASUREMENT CLEANUP, LATE-BOUND STRESS & LARGE-SCALE REGENERATION\n";
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

    // 1. Full 8-Tier Scaling Ladder with Distributed Bounce 1
    diag.run_full_tier_scaling_diagnostics();

    // 2. PART A: Measurement Consistency, Microsecond Timings & Accounting Closure Checks
    diag.test_measurement_integrity_and_accounting();

    // 3. PART B: Late-Bound Pruned-Source Adversarial Validation & Residual-Tail Test
    diag.test_late_bound_pruned_source_stress();

    // 4. PART C & D: Large-Scale Safe Regeneration & 8x Discovery Optimization (128k Lights)
    diag.test_large_scale_regeneration_and_discovery();

    // 5. Export Clean CSV/JSON Telemetry Deliverables
    diag.export_all_diagnostics_files();

    // 6. PART H: Final Report & Definition of Done
    diag.print_final_diagnostic_summary();

    rtx_shutdown();
    return 0;
}
