#include "astg_transport_diagnostics.h"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "================================================================================\n";
    std::cout << "🚀 ASTG TRANSPORT SCALING DIAGNOSTIC & SAFE OPTIMIZATION VERIFICATION SUITE\n";
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

    // 1. Run full 8-tier scaling ladder with Scene-Valid Lights & Distributed Bounce 1
    diag.run_full_tier_scaling_diagnostics();

    // 2. Run Phase 33: Probe Locality Test
    diag.test_probe_locality();

    // 3. Run Phase 34: Source Isolation Test
    diag.test_source_isolation();

    // 4. Run Phase 35: Provenance Test
    diag.test_provenance();

    // 5. Run Phases 36 & 37: Fan-In Sweep & 512-Light Spatial Variation Test
    diag.test_fan_in_variations();

    // 6. Run Part 8 & 9: Fresh-Rebuild Equivalence Test (Incremental vs Rebuild)
    diag.test_fresh_rebuild_equivalence();

    // 7. Run Part 10 & 29: Destruction Matrix & 1000x Mutation Stress Test
    diag.test_destruction_matrix_and_stability();

    // 8. Run Part 34: Repair Budget Invariance Test (64 vs 4096 rays/frame)
    diag.test_repair_budget_invariance();

    // 9. Run Part 17 & 20: Adaptive Energy Retention Quality Sweep (Energy95-99.5 vs K8-128)
    diag.run_adaptive_energy_retention_sweep();

    // 10. Run Part 21: Equal-Contribution Many-Light Torture Test
    diag.run_equal_contribution_torture_test();

    // 11. Run Part 4 & 11: Adaptive 2-Stage Discovery Optimization Test
    diag.run_adaptive_discovery_optimization();

    // 12. Export all clean telemetry deliverables
    diag.export_all_diagnostics_files();

    // 13. Output Definition of Done verification summary
    diag.print_final_diagnostic_summary();

    rtx_shutdown();
    return 0;
}
