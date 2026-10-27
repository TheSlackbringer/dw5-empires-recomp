#include <rex/system/gpu_plugin.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>

#ifdef _WIN32
// Match the game's Unicode Windows CRT startup, without a GUI subsystem.
int wmain()
#else
int main()
#endif
{
    rex::LogConfig logging;
    logging.log_to_console = true;
    rex::InitLogging(logging);
    const auto directory = rex::filesystem::GetExecutableFolder();
    if (directory.empty() || !directory.is_absolute()) {
        std::cerr << "FAIL: executable directory is unavailable or relative\n";
        return 1;
    }
    std::cout << "Plugin directory: " << directory << '\n';
    // Exercise runtime discovery, dependent libraries, exports, ABI and factory.
    // Presentation/device setup is deliberately outside this host-only test.
    auto graphics = rex::system::LoadGpuPlugin("xenos");
    if (!graphics) {
        std::cerr << "FAIL: real xenos plugin discovery/load/ABI/factory\n";
        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            const auto name = entry.path().filename().string();
            if (entry.path().extension() == ".dll") {
                std::cerr << "Staged DLL: " << name << '\n';
#ifdef _WIN32
                if (name.starts_with("rexgpu-") || name.starts_with("librexgpu-")) {
                    const auto module = LoadLibraryW(entry.path().c_str());
                    const auto error = module ? ERROR_SUCCESS : GetLastError();
                    std::cerr << "Direct plugin LoadLibrary WinError: " << error << '\n';
                    if (module)
                        FreeLibrary(module);
                }
#endif
            }
        }
        rex::FlushLogging();
        return 1;
    }
    graphics.reset();
    std::cout << "PASS: real xenos plugin loads without a game, window or GPU device\n";
    return 0;
}
