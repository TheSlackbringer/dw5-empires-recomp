#include "setup/installation.h"
#include "setup/install_location.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace dw5e::setup;
namespace fs = std::filesystem;
int failures = 0;
void check(bool ok, const char *name)
{
    std::cout << (ok ? "PASS: " : "FAIL: ") << name << '\n';
    failures += !ok;
}
void put(const fs::path &p, const std::string &text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
std::string read(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void env(const char *name, const fs::path &value)
{
#ifdef _WIN32
    _wputenv_s(pathFromText(name).c_str(), value.c_str());
#else
    setenv(name, pathText(value).c_str(), 1);
#endif
}
void installed(const fs::path &root)
{
    put(root / "game/installed.txt", "DW5E Disc Installed\nsha1: 82fc63021a0919d8b97584682babf0654997c0a9\n");
    put(root / "game/files/default.xex", "authored non-retail fixture");
}
int main()
{
    auto scratch =
        fs::temp_directory_path() /
        ("dw5e_modes_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    // Windows temp paths can use short names (RUNNER~1), but installation roots are resolved.
    fs::create_directories(scratch);
    scratch = fs::canonical(scratch);
    env("LOCALAPPDATA", scratch / "home");
    env("HOME", scratch / "home");
    env("XDG_CONFIG_HOME", scratch / "config-home");
    env("XDG_DATA_HOME", scratch / "data-home");
    std::string error;
    Installation install;
    const auto root = scratch / pathFromText("release ø 測試") / "game-files";
    const auto games = scratch / "Games";
    fs::create_directories(games);
    check(newInstallationRoot(games) == games / "DW5E-Recomp/game-files",
          "custom parent gets a self-contained DW5E-Recomp installation");
    check(newInstallationRoot(games / "DW5E-Recomp") == games / "DW5E-Recomp/game-files",
          "selecting the branded folder does not create a second branded folder");
    check(newInstallationRoot(games / "DW5E") == games / "DW5E" && newInstallationRoot(root) == root,
          "dedicated folders do not get nested");
    installed(games / "DW5E");
    check(existingInstallationRoot(games) == games / "DW5E", "find an existing DW5E child from its parent");
    fs::remove(games / "DW5E/game/installed.txt");
    check(loadInstallation(root, install, error) && install.mode == SaveMode::Normal && !install.recorded,
          "new installation defaults to normal saves");
    check(install.userData == normalUserDataDirectory() && install.configDir == normalConfigDirectory(),
          "normal saves and config use platform user directories");
    check(prepareInstallation(install, error), "prepare normal installation atomically");
    installed(root);
    put(install.userData / "0000000000000001/4B4F07D3/save", "current progress");
    put(install.userData / "achievements/4B4F07D3.toml", "unlocked");
    put(install.configDir / "dw5e.toml", "settings");
    auto oldUser = install.userData, oldConfig = install.configDir;
    check(rememberNormalInstallation(install, error),
          "normal install location recorded outside downloaded release");
    {
        InstallationLock held;
        check(held.acquire(root, error), "lock existing installation");
        bool blocked = false;
        std::thread other([&] {
            auto copy = install;
            std::string lockError;
            blocked = !switchSaveMode(copy, SaveMode::Portable, SaveConflict::Ask, lockError);
        });
        other.join();
        check(blocked, "another management transaction cannot race the active installation lock");
    }
    check(switchSaveMode(install, SaveMode::Portable, SaveConflict::Ask, error),
          "normal to portable copies saves and settings");
    check(install.userData == root / "user-data" && install.configDir == install.userData,
          "portable paths stay with installation");
    check(read(install.userData / "0000000000000001/4B4F07D3/save") == "current progress" &&
              read(install.configDir / "dw5e.toml") == "settings",
          "portable progress/settings copied exactly");
    check(read(oldUser / "0000000000000001/4B4F07D3/save") == "current progress" &&
              read(oldConfig / "dw5e.toml") == "settings",
          "mode switch retains original files");
    Installation restarted;
    check(loadInstallation(root, restarted, error) && restarted.recorded &&
              restarted.mode == SaveMode::Portable,
          "update reads persisted portable mode without asking again");
    put(install.userData / "0000000000000001/4B4F07D3/save", "new portable progress");
    const auto before = read(root / "install.json");
    check(!switchSaveMode(install, SaveMode::Normal, SaveConflict::Ask, error) &&
              error.find("conflict") != std::string::npos,
          "conflicting destination saves require an explicit choice");
    check(read(root / "install.json") == before && install.mode == SaveMode::Portable,
          "conflict leaves old mode and source progress active");
    check(switchSaveMode(install, SaveMode::Normal, SaveConflict::UseSource, error) &&
              read(oldUser / "0000000000000001/4B4F07D3/save") == "new portable progress",
          "explicit current-save choice switches mode");
    bool backup = false;
    for (const auto &entry : fs::directory_iterator(oldUser / "0000000000000001/4B4F07D3"))
        if (entry.path().filename() != "save" && read(entry.path()) == "current progress")
            backup = true;
    check(backup, "overwritten destination progress has a verified backup");
    check(switchSaveMode(install, SaveMode::Portable, SaveConflict::Ask, error),
          "switch again after keeping verified source copies");
    put(install.userData / "0000000000000001/4B4F07D3/save", "different portable branch");
    check(switchSaveMode(install, SaveMode::Normal, SaveConflict::KeepDestination, error) &&
              read(oldUser / "0000000000000001/4B4F07D3/save") == "new portable progress",
          "explicit destination-save choice retains destination progress");
    const auto newZip = scratch / "downloads/new-version";
    fs::create_directories(newZip);
    std::vector<fs::path> candidates;
    check(discoverInstallations(newZip, candidates, error) && candidates.size() == 1 &&
              fs::equivalent(candidates[0], root),
          "fresh ZIP finds recorded installation");
    check(loadInstallation(candidates[0], restarted, error) && restarted.mode == SaveMode::Normal,
          "discovery retains installation mode");
    const auto local = newZip / "game-files";
    installed(local);
    check(discoverInstallations(newZip, candidates, error) && candidates.size() == 1 &&
              fs::equivalent(candidates[0], local),
          "valid local installation takes priority over machine record");
    const auto portableOnly = scratch / "portable-only/game-files";
    Installation standalone;
    check(loadInstallation(portableOnly, standalone, error), "load independent portable installation");
    standalone.mode = SaveMode::Portable;
    standalone.userData = standalone.configDir = portableOnly / "user-data";
    const auto indexBefore = read(normalConfigDirectory() / "installations.json");
    check(prepareInstallation(standalone, error) &&
              read(normalConfigDirectory() / "installations.json") == indexBefore,
          "portable install makes no machine index write");
    const auto legacy = scratch / "legacy";
    installed(legacy);
    put(legacy / "0000000000000001/4B4F07D3/save", "legacy progress");
    put(legacy / "build/dw5e.toml", "legacy settings");
    put(legacy / "build/_deps/keep", "dependency sentinel");
    Installation imported;
    loadInstallation(scratch / "import/game-files", imported, error);
    imported.mode = SaveMode::Portable;
    imported.userData = imported.configDir = imported.root / "user-data";
    check(importInstallation(legacy, imported, SaveConflict::Ask, error),
          "copy-first legacy import reuses installed game files");
    check(read(imported.userData / "0000000000000001/4B4F07D3/save") == "legacy progress" &&
              read(imported.configDir / "dw5e.toml") == "legacy settings" &&
              read(imported.root / "build/_deps/keep") == "dependency sentinel",
          "import retains saves/settings and build dependencies");
    check(read(legacy / "0000000000000001/4B4F07D3/save") == "legacy progress" &&
              read(legacy / "build/dw5e.toml") == "legacy settings",
          "legacy originals remain unchanged");
    put(imported.root / "launcher/DW5E.exe", "packaged launcher");
    put(imported.root / "tools/keep", "packaged tools");
    put(imported.root / "builder/app/keep", "generated builder");
    put(imported.root / "generated/keep", "generated code");
    put(imported.root / "cache/snapshots/keep", "player snapshot");
    const auto importMetadata = read(imported.root / "install.json");
    const auto machineIndex = read(normalConfigDirectory() / "installations.json");
    {
        InstallationLock held;
        check(held.acquire(imported.root, error), "lock installation for simulated setup");
        bool blocked = false;
        std::thread attempt([&] {
            std::string lockError;
            blocked = !uninstallGameFiles(imported.root, imported.root.parent_path(), true, lockError);
        });
        attempt.join();
        check(blocked && fs::exists(imported.root / "game/files/default.xex"),
              "uninstall cannot remove files while another operation holds the lock");
    }
    check(uninstallGameFiles(imported.root, imported.root.parent_path(), true, error),
          "uninstall verified portable game files");
    check(!fs::exists(imported.root / "game/files") &&
              !fs::exists(imported.root / "game/installed.txt") &&
              !fs::exists(imported.root / "build/_deps/keep") &&
              !fs::exists(imported.root / "builder") && !fs::exists(imported.root / "generated"),
          "uninstall removes extracted and built files");
    check(read(imported.root / "install.json") == importMetadata &&
              read(imported.userData / "0000000000000001/4B4F07D3/save") == "legacy progress" &&
              read(imported.configDir / "dw5e.toml") == "legacy settings" &&
              read(imported.root / "launcher/DW5E.exe") == "packaged launcher" &&
              read(imported.root / "tools/keep") == "packaged tools" &&
              read(imported.root / "cache/snapshots/keep") == "player snapshot" &&
              read(normalConfigDirectory() / "installations.json") == machineIndex,
          "uninstall preserves portable progress, mode, launcher, tools and snapshots");
    check(uninstallGameFiles(legacy, newZip, true, error) &&
              loadInstallation(legacy, restarted, error) && restarted.mode == SaveMode::Legacy &&
              read(legacy / "build/dw5e.toml") == "legacy settings" &&
              read(legacy / "0000000000000001/4B4F07D3/save") == "legacy progress" &&
              !fs::exists(legacy / "build/_deps/keep"),
          "legacy uninstall keeps settings in build and saves in place");
    put(scratch / "unverified/build/keep", "unrelated");
    check(!uninstallGameFiles(scratch / "unverified", newZip, true, error) &&
              read(scratch / "unverified/build/keep") == "unrelated",
          "uninstall refuses an unverified folder");
    env("LOCALAPPDATA", {});
    env("HOME", {});
    env("XDG_CONFIG_HOME", {});
    env("XDG_DATA_HOME", {});
    Installation noHome;
    check(loadInstallation(scratch / "without-home/game-files", noHome, error),
          "first setup can offer portable mode without a user home");
    configureSaveMode(noHome, SaveMode::Portable);
    check(prepareInstallation(noHome, error) && noHome.userData == noHome.root / "user-data",
          "portable setup does not depend on machine-local user folders");
    env("LOCALAPPDATA", scratch / "home");
    env("HOME", scratch / "home");
    env("XDG_CONFIG_HOME", scratch / "config-home");
    env("XDG_DATA_HOME", scratch / "data-home");
    put(imported.root / "install.json", "{broken");
    check(!loadInstallation(imported.root, restarted, error),
          "malformed mode metadata cannot silently switch saves");
    std::cout << (failures ? "FAIL" : "PASS") << ": storage modes\n";
    return failures ? 1 : 0;
}
