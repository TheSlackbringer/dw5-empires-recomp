#include <rex/filesystem.h>
#include <rex/logging.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
int main()
{
    const auto base =
        fs::temp_directory_path() /
        ("dw5e_host_paths_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = base / rex::to_path("folder ø 測試");
    fs::create_directories(root);
    const auto config = root / "config.toml";
    std::ofstream(config) << "[log.levels]\ncore = \"debug\"\n";
    int failures = 0;
    try
    {
        const auto levels = rex::ParseCategoryLevelsFromConfig(config);
        bool ok = levels.contains("core") && levels.at("core") == "debug";
        std::cout << (ok ? "PASS" : "FAIL") << ": SDK reads host Unicode configuration\n";
        failures += !ok;
    }
    catch (const std::exception &e)
    {
        std::cout << "FAIL: SDK Unicode configuration: " << e.what() << '\n';
        ++failures;
    }
    try
    {
        rex::LogConfig options;
        options.app_name = "host_unicode";
        options.log_dir = rex::path_to_utf8(root);
        options.log_to_console = false;
        rex::InitLogging(options);
        REXLOG_INFO("issue010-unicode-log-marker");
        rex::ShutdownLogging();
        bool found = false;
        for (const auto &entry : fs::directory_iterator(root))
            if (entry.path().extension() == ".log")
            {
                std::ifstream in(entry.path());
                std::string content(std::istreambuf_iterator<char>(in), {});
                found |= content.find("issue010-unicode-log-marker") != std::string::npos;
            }
        std::cout << (found ? "PASS" : "FAIL") << ": SDK writes log under exact Unicode destination\n";
        failures += !found;
    }
    catch (const std::exception &e)
    {
        std::cout << "FAIL: SDK Unicode log: " << e.what() << '\n';
        ++failures;
    }
    return failures ? 1 : 0;
}
