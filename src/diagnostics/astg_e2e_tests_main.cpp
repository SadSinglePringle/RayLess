#include "astg_e2e_tests.h"
#include <iostream>

int main(int argc, char** argv) {
    if (!rtx_init()) {
        std::cerr << "❌ Failed to initialize DXR 1.1 Hardware RT Core Ray Tracer.\n";
        return 1;
    }

    ASTGE2ETestSuite suite;
    suite.initialize("assets/bistro/bistro.gltf", "assets/bistro/bistro.bin");
    bool all_passed = suite.run_all_e2e_tests();

    rtx_shutdown();
    return all_passed ? 0 : 1;
}
