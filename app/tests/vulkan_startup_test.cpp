#include <rex/logging.h>
#include <rex/ui/vulkan/provider.h>

#include <iostream>

int main()
{
    rex::LogConfig logging;
    logging.log_to_console = true;
    rex::InitLogging(logging);
    // Use the real game runtime's instance, device selection and GPU features.
    // No SDL window, surface, swapchain, guest code or game assets are created.
    auto provider = rex::ui::vulkan::VulkanProvider::Create(true, false);
    if (!provider)
    {
        std::cerr << "FAIL: actual runtime Vulkan instance/device initialization\n";
        rex::FlushLogging();
        return 1;
    }
    std::cout << "PASS: actual runtime Vulkan instance/device initialized: "
              << provider->vulkan_device()->properties().deviceName << '\n';
    provider.reset();
    rex::FlushLogging();
    return 0;
}
