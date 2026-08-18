#include "astg_transport_diagnostics.h"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "================================================================================\n";
    std::cout << "🚀 ASTG TRANSPORT SCALING DIAGNOSTIC & VERIFICATION SUITE\n";
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

    // 1. Run full 8-tier scaling ladder with Scene-Valid Lights & Top-32 Ranking
    diag.run_full_tier_scaling_diagnostics();

    // 2. Run Phase 33: Probe Locality Test
    diag.test_probe_locality();

    // 3. Run Phase 34: Source Isolation Test
    diag.test_source_isolation();

    // 4. Run Phase 35: Provenance Test
    diag.test_provenance();

    // 5. Run Phases 36 & 37: Fan-In Sweep & 512-Light Spatial Variation Test
    diag.test_fan_in_variations();

    // 6. Export all clean telemetry deliverables
    diag.export_all_diagnostics_files();

    // 7. Output Definition of Done verification summary
    diag.print_final_diagnostic_summary();

    rtx_shutdown();
    return 0;
}
