#include "astg_transport_diagnostics.h"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "================================================================================\n";
    std::cout << "🚀 ASTG ADVERSARIAL REPAIR VALIDATION & SAFE OPTIMIZATION SUITE\n";
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

    // 2. Priority 1: Regeneration-Anchor Semantics Audit
    diag.test_anchor_semantics_audit();

    // 3. Priority 2: Exact Repair-Memory Accounting
    diag.test_exact_repair_memory_accounting();

    // 4. Priority 3, 4, 12: Baseline vs 8x Discovery & Lifetime Ray Cost
    diag.test_baseline_vs_8x_discovery_comparison();

    // 5. Priority 5, 10, 11, 12, 13: Multi-Chunk Adversarial Destruction Matrix
    diag.test_multi_chunk_adversarial_matrix();

    // 6. Priority 6 & 7: Merged-Node Partial Invalidation & Source Attribution
    diag.test_merged_node_partial_invalidation();

    // 7. Priority 8 & 9: Stale Generation Attack Test & AS Generation Safety
    diag.test_stale_generation_attack();

    // 8. Priority 18 & 19: Frontier Completeness & Recall Test
    diag.test_frontier_completeness_and_recall();

    // 9. Priority 24, 25, 32: Adaptive Energy Retention Quality & Runtime Benchmark
    diag.run_adaptive_energy_retention_sweep();

    // 10. Priority 26 & 28: Pruned-Source Activation Test
    diag.test_pruned_source_activation();

    // 11. Export Clean CSV/JSON Telemetry Deliverables
    diag.export_all_diagnostics_files();

    // 12. Final Report & Definition of Done
    diag.print_final_diagnostic_summary();

    rtx_shutdown();
    return 0;
}
