#include "codegen.h"
#include "install_state.h"
#include "install_location.h"
#include "run_process.h"
#include "setup_layout.h"
#include "wizard_ui.h"
#include "installation.h"
#include "../../rexglue-sdk/thirdparty/inja/third_party/include/nlohmann/json.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

using namespace dw5e::setup;
namespace fs = std::filesystem;

namespace {
#ifdef _WIN32
constexpr bool kWindows = true;
#else
constexpr bool kWindows = false;
#endif

struct Args
{
    fs::path iso;
    fs::path root;
    fs::path dataDir;
    std::string error;
    bool help = false;
    bool audioMute = false;
    bool noWindow = false;
    bool installOnly = false;
    bool rebuild = false;
    bool manage = false;
    bool status = false;
    bool modeChosen = false;
    SaveMode mode = SaveMode::Normal;
    SaveConflict conflict = SaveConflict::Ask;
    fs::path existing, importSource;
};

std::string firstLine(const fs::path &path)
{
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    return line;
}

Args parseArgs(const std::vector<std::string> &argv)
{
    Args a;
    for (size_t i=1;i<argv.size();++i) {
        const auto &arg=argv[i];
        if(arg=="--iso" || arg=="--disc" || arg=="--root" || arg=="--data-dir" || arg=="--use-installation" || arg=="--import-installation") {
            if(i+1==argv.size() || argv[i+1].empty() || argv[i+1].starts_with("--")) {
                a.error="Missing path after "+arg; break;
            }
            const auto path=pathFromText(argv[++i]);
            if(arg=="--root") a.root=path;
            else if(arg=="--data-dir") a.dataDir=path;
            else if(arg=="--use-installation") a.existing=path;
            else if(arg=="--import-installation") a.importSource=path;
            else a.iso=path;
        } else if(arg=="--save-mode" || arg=="--save-conflict") {
            if(i+1==argv.size()) { a.error="Missing value after "+arg; break; }
            const auto value=argv[++i];
            if(arg=="--save-mode") {
                a.modeChosen=true;
                if(value=="normal") a.mode=SaveMode::Normal;
                else if(value=="portable") a.mode=SaveMode::Portable;
                else { a.error="Save mode must be normal or portable"; break; }
            } else {
                if(value=="source") a.conflict=SaveConflict::UseSource;
                else if(value=="destination") a.conflict=SaveConflict::KeepDestination;
                else if(value!="ask") { a.error="Save conflict choice must be ask, source or destination"; break; }
            }
        } else if(arg=="--manage") a.manage=true;
        else if(arg=="--status") a.status=true;
        else if(arg=="--no-window") a.noWindow=true;
        else if(arg=="--install") { a.installOnly=true; a.noWindow=true; }
        else if(arg=="--rebuild") a.rebuild=true;
        else if(arg=="--audio_mute=true") a.audioMute=true;
        else if(arg=="--help" || arg=="-h") a.help=true;
        else { a.error="Unknown setup argument: "+arg; break; }
    }
    return a;
}
} // namespace

int main(int argc, char **argv)
{
    const Args args = parseArgs(nativeArguments(argc, argv));
    if (args.help) {
        std::cout << "DW5E setup: [--iso <disc>] [--data-dir <folder>] [--install | --no-window] [--rebuild]\n"
                     "Installation defaults to game-files beside DW5E.exe / the original AppImage. Saves: this PC or portable.\n"
                     "Overrides: --data-dir, DW5E_DATA_DIR, remembered choice, launcher folder. Invocation overrides are not saved.\n"
                     "Manage: --manage [--save-mode normal|portable] [--save-conflict source|destination]\n"
                     "Existing files: --use-installation <folder> or --import-installation <folder>. Inspect with --status.\n";
        return 0;
    }
    if (!args.error.empty()) { std::cerr << args.error << std::endl; return 1; }

    std::error_code ec;
    fs::path root = args.root.empty() ? fs::path{} : fs::absolute(args.root,ec).lexically_normal();
    if (root.empty())
    {
        std::string discoveryError;
        fs::path exeDir = executableDirectory(discoveryError);
        root = findRoot(exeDir);
    }
    if (root.empty())
        root = findRoot(fs::current_path(ec));

    SetupOptions options;
    std::string layoutError;
    SetupLayout layout;

    if (root.empty())
    {
        layoutError = "cannot find project root (dw5e_manifest.toml or app/CMakeLists.txt missing)";
    }
    options.root = root;

    // Release package check
    const std::string release = root.empty() ? std::string() : firstLine(root / "BUILD_ID");
    fs::path game, data, launcher;
    bool explicitDestination = false;
    std::string destinationError;
    Installation storage;
    std::vector<fs::path> discovered;
    fs::path importSource;

    if (!release.empty())
    {
        launcher = launcherDirectory(destinationError);
        std::string resolveError;
        if (!resolveInstallLocation(launcher, args.dataDir, data, explicitDestination, resolveError))
            destinationError = resolveError;
        else if (!args.existing.empty()) {
            data = existingInstallationRoot(args.existing);
            if(data.empty()) destinationError = "Selected folder is not a verified DW5E installation: " + pathText(args.existing);
            else destinationError.clear();
        }
        else if (args.importSource.empty() && !explicitDestination && !fs::exists(launcher / "dw5e-install.json") &&
                 installState(data) != InstallState::Installed) {
            if(!discoverInstallations(launcher, discovered, resolveError)) destinationError=resolveError;
            else if(discovered.size()==1) { data=discovered.front(); destinationError.clear(); }
            else if(discovered.size()>1) destinationError="Several existing DW5E installations were found. Choose one below.";
        }
        if (destinationError.empty() && !args.importSource.empty()) {
            importSource=existingInstallationRoot(args.importSource);
            if(importSource.empty()) destinationError="Cannot import an unverified DW5E installation: "+pathText(args.importSource);
            else if(!explicitDestination) data=launcher/"game-files";
        }
        if (destinationError.empty() && !loadInstallation(data, storage, resolveError)) destinationError=resolveError;
        if (destinationError.empty() && !args.status && !validateInstallDirectory(data, resolveError))
            destinationError = resolveError;
        options.dataDir = data;
        game = options.effectiveBuildDir() / (kWindows ? "dw5e.exe" : "dw5e");
    }

    const auto installed = [&] {
        return installState(data) == InstallState::Installed;
    };

    if(args.status) {
        if(!destinationError.empty()) { std::cerr<<destinationError<<std::endl; return 1; }
        std::cout<<nlohmann::json{{"installation",pathText(data)},{"save_mode",saveModeName(storage.mode)},
            {"user_data",pathText(storage.userData)},{"config",pathText(storage.configDir)},
            {"recorded",storage.recorded}}.dump(2)<<std::endl;
        return 0;
    }
    const bool managing = args.manage || (args.modeChosen && installed());
    const auto startGame = [&] {
        InstallationLock lock;
        if(!lock.acquire(data,destinationError)) { std::cerr<<destinationError<<std::endl;return 1; }
        if (!validateInstallDirectory(data, destinationError)) {
            std::cerr << "DW5E launcher error: " << destinationError << std::endl; return 1;
        }
        game = options.effectiveBuildDir() / (kWindows ? "dw5e.exe" : "dw5e");
        if(!loadInstallation(data, storage, destinationError) ||
           !validateInstallDirectory(storage.userData,destinationError) ||
           !validateInstallDirectory(storage.configDir,destinationError)) {
            std::cerr<<"DW5E save-location error: "<<destinationError<<std::endl; return 1;
        }
        fs::create_directories(data / "logs", ec);
        if(ec) { std::cerr<<"Cannot create game logs at "<<pathText(data/"logs")<<std::endl; return 1; }
        if(!rememberNormalInstallation(storage,destinationError)) { std::cerr<<destinationError<<std::endl;return 1; }
        const fs::path gameData = data / "game" / "files";
        std::vector<std::string> gameArgs = {
            "--game_data_root=" + pathText(gameData),
            "--user_data_root=" + pathText(storage.userData),
            "--config_root=" + pathText(storage.configDir),
            "--log_file=" + pathText(data / "logs" / "runtime.log"),
            "--cache_root=" + pathText(data / "cache"),
            "--gpu_plugin=xenos"
        };
        if (args.audioMute) gameArgs.push_back("--audio_mute=true");
        std::vector<std::pair<std::string, std::string>> env;
#ifndef _WIN32
        // Ensure the build directory and local runtime libraries take priority in LD_LIBRARY_PATH
        std::string ldPath = pathText(options.effectiveBuildDir()) + ":" +
                             pathText(options.effectiveBuildDir() / "rexglue-sdk" / "src" / "system") + ":" +
                             pathText(options.effectiveBuildDir() / "rexglue-sdk" / "thirdparty");
        if (fs::exists(options.root / "tools" / "clang" / "lib"))
            ldPath += ":" + pathText(options.root / "tools" / "clang" / "lib");
        if (const char *orig = std::getenv("LD_LIBRARY_PATH"))
        {
            if (*orig)
                ldPath += std::string(":") + orig;
        }
        env.emplace_back("LD_LIBRARY_PATH", ldPath);

        // Linux: keep launcher process alive while child game runs so AppImage FUSE mount stays mounted
        const ProcessResult res = runProcess(game, gameArgs, data, data / "logs" / "game.log", nullptr, env);
        if (res.started)
            return res.exitCode;
        std::cerr << "DW5E launcher error: " << res.error << std::endl;
        return 1;
#else
        std::string err;
        if (!launchDetached(game, gameArgs, data, err))
        {
            std::cerr << "DW5E launcher error: " << err << std::endl;
            return 1;
        }
        return 0;
#endif
    };

    const auto markBuilt = [&] {
        std::ofstream out(options.effectiveBuildDir() / "built-from.txt", std::ios::trunc);
        out << release << "\n";
        return bool(out.flush());
    };

    options.rebuild = args.rebuild;
    // Fast-path: if game is already built for this release and disc is installed, boot immediately!
    if (layoutError.empty() && destinationError.empty() && !args.installOnly && !managing &&
        !args.modeChosen && importSource.empty() && wizardBuildIsCurrent(options, data, release))
    {
        if (args.noWindow)
        {
            std::cout << "SUCCESS: Game already built and ready at " << pathText(game) << std::endl;
            return 0;
        }
        return startGame();
    }

    // Build-only prerequisites must not block an already-built game's fast path.
    if (!managing && layoutError.empty() && findLayout(root, kWindows, layout, layoutError))
    {
        options.recompiler = layout.recompiler;
        options.tools = layout.tools;
    }

    // Keep the package root in the idle/retry UI. Each SetupRun restores its
    // stable Linux builder before using it, even if player data was deleted.
    if (!args.iso.empty())
        options.discIso = args.iso;

    WizardConfig config;
    config.base = options;
    config.launcherDir = launcher;
    config.destinationError = destinationError;
    config.explicitDestination = explicitDestination;
    config.layoutError = layoutError;
    config.releaseId = release;
    config.prefilledIso = args.iso;
    config.discInstalled = installed();
    config.noWindow = args.noWindow;
    config.manageOnly = managing;
    config.modeChosen = args.modeChosen;
    config.desiredMode = args.mode;
    config.conflict = args.conflict;
    config.importSource = importSource;
    config.discovered = discovered;
    config.closeWhenFinished = true;

    bool ok = runWizardWindow(config);
    if (ok)
    {
        if(config.manageOnly) return 0;
        options = config.base;
        data = options.effectiveDataDir();
        if(!loadInstallation(data,storage,destinationError) || !rememberNormalInstallation(storage,destinationError)) {
            std::cerr<<destinationError<<std::endl;return 1;
        }
        if (!release.empty() && !config.launchExistingBuild && !markBuilt()) {
            std::cerr << "Cannot finish build marker at " << pathText(options.effectiveBuildDir()) << std::endl;
            return 1;
        }

        if (!args.noWindow)
        {
            game = options.effectiveBuildDir() / (kWindows ? "dw5e.exe" : "dw5e");
            if (fs::exists(game))
                return startGame();
        }
        return 0;
    }

    return 1;
}
