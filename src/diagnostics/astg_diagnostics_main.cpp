#include "astg_transport_diagnostics.h"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "================================================================================\n";
    std::cout << "🚀 ASTG TRANSPORT SCALING DIAGNOSTIC SUITE (TEST GROUPS A–T)\n";
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

    // 1. Run full 8-tier scaling diagnostics (Test Groups A, B, C, D1, D2, E, F, G, H, O, P, Q)
    diag.run_full_tier_scaling_diagnostics();

    // 2. Run Test Group D3: Fan-In Sweep
    diag.run_fan_in_sweep();

    // 3. Run Test Group I: Light Range Sweep
    diag.run_range_sweep();

    // 4. Run Test Group J: Discovery Ray Count Sweep
    diag.run_ray_count_sweep();

    // 5. Run Test Group T: Probe Pool Count Sweep
    diag.run_probe_count_sweep();

    // 6. Run Test Groups L & M: Provenance & Real Coefficient Validation
    diag.verify_provenance_and_coefficients();

    // 7. Run Test Group N: Late-Bound Source Isolation
    diag.verify_late_bound_source_isolation();

    // 8. Export all required CSV & JSON deliverable files
    diag.export_all_diagnostics_files();

    // 9. Output final formatted diagnostic summary
    diag.print_final_diagnostic_summary();

    rtx_shutdown();
    return 0;
}
