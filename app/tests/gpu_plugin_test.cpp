#include <rex/system/gpu_plugin.h>

#include <iostream>

int main()
{
    // Exercise runtime discovery, dependent libraries, exports, ABI and factory.
    // Presentation/device setup is deliberately outside this host-only test.
    auto graphics = rex::system::LoadGpuPlugin("xenos");
    if (!graphics) {
        std::cerr << "FAIL: real xenos plugin discovery/load/ABI/factory\n";
        return 1;
    }
    graphics.reset();
    std::cout << "PASS: real xenos plugin loads without a game, window or GPU device\n";
    return 0;
}
