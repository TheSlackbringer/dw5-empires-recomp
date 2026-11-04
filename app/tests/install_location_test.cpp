#include "setup/install_location.h"
#include "setup/install_state.h"
#include "setup/setup_layout.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace dw5e::setup;
namespace fs = std::filesystem;
static int failures = 0;
static void check(bool value, const char *name)
{
    std::cout << (value ? "PASS: " : "FAIL: ") << name << '\n';
    failures += !value;
}
static std::string read(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
static void write(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}
static void env(const char *value)
{
#ifdef _WIN32
    _putenv_s("DW5E_DATA_DIR", value);
#else
    if (*value)
        setenv("DW5E_DATA_DIR", value, 1);
    else
        unsetenv("DW5E_DATA_DIR");
#endif
}
int main()
{
    const auto scratch =
        fs::temp_directory_path() /
        ("dw5e_install_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto launcher = scratch / "package with spaces";
    fs::create_directories(launcher);
    fs::path result;
    bool explicitOverride = false;
    std::string error;
    env("");
    check(resolveInstallLocation(launcher, {}, result, explicitOverride, error) && result == launcher / "dw5e-data" &&
              !explicitOverride,
          "missing configuration uses dw5e-data beneath launcher");
    InstallFolderSelection selection{launcher, launcher};
    check(selection.choose({}) && selection.destination == launcher,
          "cancelled folder picker preserves selection");
    check(selection.prepare(), "default selection validates and persists");
    check(read(launcher / "dw5e-install.json").find("\"data_dir\": \".\"") != std::string::npos,
          "launcher folder stored as dot");
    const auto child = launcher / pathFromText("custom \xc3\xb8 \xe6\xb8\xac\xe8\xa9\xa6");
    check(!selection.choose(child) && selection.destination == launcher,
          "active operation freezes destination");
    selection.frozen = false;
    check(selection.choose(child) && selection.prepare(), "custom Unicode child folder prepared");
    const auto saved = read(launcher / "dw5e-install.json");
    check(saved.find(pathText(launcher)) == std::string::npos, "child choice stored relative to launcher");
    check(resolveInstallLocation(launcher, {}, result, explicitOverride, error) && result == child,
          "remembered child resolves against launcher");
    const auto external = scratch / "external choice";
    fs::create_directories(external);
    env(pathText(external).c_str());
    check(resolveInstallLocation(launcher, {}, result, explicitOverride, error) && result == external &&
              explicitOverride,
          "environment overrides remembered choice");
    check(resolveInstallLocation(launcher, scratch / "command", result, explicitOverride, error) &&
              result == scratch / "command",
          "command line overrides environment");
    check(read(launcher / "dw5e-install.json") == saved,
          "invocation overrides leave remembered GUI choice unchanged");
    env("");
    write(launcher / "dw5e-install.json.tmp-abandoned", "{partial");
    check(resolveInstallLocation(launcher, {}, result, explicitOverride, error) && result == child,
          "interrupted temporary write leaves valid choice usable");
    for (const auto &invalid :
         {"{broken", "{\"version\":2,\"data_dir\":\".\"}", "{\"version\":1,\"data_dir\":17}"})
    {
        write(launcher / "dw5e-install.json", invalid);
        check(!resolveInstallLocation(launcher, {}, result, explicitOverride, error) && !error.empty(),
              "invalid configuration reports error without default fallback");
    }
    write(launcher / "dw5e-install.json", "{\"version\":1,\"data_dir\":\"missing-drive-or-folder\"}");
    check(!resolveInstallLocation(launcher, {}, result, explicitOverride, error) &&
              !fs::exists(launcher / "missing-drive-or-folder"),
          "unavailable remembered destination is not initialized elsewhere");
    write(scratch / "file", "not a directory");
    check(!validateInstallDirectory(scratch / "file", error) && error.find("file") != std::string::npos,
          "file destination rejected with selected path");
    check(checkInstallDirectory(scratch / "unmade" / "deep", error) && !fs::exists(scratch / "unmade"),
          "checking a missing destination does not create it");
    check(checkInstallDirectory(external, error) && fs::is_directory(external),
          "existing destination passes the check");
    check(!checkInstallDirectory(scratch / "file", error) && error.find("file") != std::string::npos,
          "checking a file destination rejects it");
    check(!checkInstallDirectory(scratch / "file" / "child", error) && error.find("child") != std::string::npos,
          "checking a destination below a file rejects it");
    size_t leftovers = 0;
    for (const auto &entry : fs::directory_iterator(scratch))
        leftovers += entry.path().filename().string().rfind(".dw5e-write-check-", 0) == 0;
    check(leftovers == 0, "checking a destination leaves no access-check file");
    check(rememberInstallLocation(launcher, external, error), "external folder choice saved");
    const auto moved = scratch / "moved package";
    fs::rename(launcher, moved);
    check(resolveInstallLocation(moved, {}, result, explicitOverride, error) && result == external,
          "external choice survives launcher-only move");
    check(rememberInstallLocation(moved, moved / child.filename(), error),
          "relative choice saved after move");
    check(resolveInstallLocation(moved, {}, result, explicitOverride, error) &&
              result == moved / child.filename(),
          "relative child survives complete package move");
    check(!fs::exists(launcher), "original package location unavailable");
    const auto package = scratch / "overlapping-package";
    write(package / "app/source.cpp", "authored source");
    const auto data = package / "app/custom-data";
    write(data / "save", "preserve");
    fs::path builder;
    check(prepareLinuxBuildRoot(package, data, builder, error) &&
              prepareLinuxBuildRoot(package, data, builder, error),
          "nested custom destination can prepare its Linux builder twice");
    check(read(builder / "app/source.cpp") == "authored source" && !fs::exists(builder / "app/custom-data") &&
              read(data / "save") == "preserve",
          "builder excludes the installation subtree without copying itself or player files");
    std::cout << (failures ? "FAIL" : "PASS") << ": install-location fixtures\n";
    return failures ? 1 : 0;
}
