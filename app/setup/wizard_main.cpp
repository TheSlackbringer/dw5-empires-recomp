#include "codegen.h"
#include "install_state.h"
#include "install_location.h"
#include "run_process.h"
#include "setup_layout.h"
#include "wizard_ui.h"

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
        if(arg=="--iso" || arg=="--disc" || arg=="--root" || arg=="--data-dir") {
            if(i+1==argv.size() || argv[i+1].empty() || argv[i+1].starts_with("--")) {
                a.error="Missing path after "+arg; break;
            }
            const auto path=pathFromText(argv[++i]);
            if(arg=="--root") a.root=path;
            else if(arg=="--data-dir") a.dataDir=path;
            else a.iso=path;
        } else if(arg=="--no-window") a.noWindow=true;
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
                     "Installation defaults beside DW5E.exe / the original AppImage. The wizard can remember another folder.\n"
                     "Overrides: --data-dir, DW5E_DATA_DIR, remembered choice, launcher folder. Invocation overrides are not saved.\n";
        return 0;
    }
    if (!args.error.empty()) { std::cerr << args.error << std::endl; return 1; }

    std::error_code ec;
    fs::path root = args.root;
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

    if (!release.empty())
    {
        launcher = launcherDirectory(destinationError);
        std::string resolveError;
        if (!resolveInstallLocation(launcher, args.dataDir, data, explicitDestination, resolveError))
            destinationError = resolveError;
        else if (!validateInstallDirectory(data, resolveError))
            destinationError = resolveError;
        else
            destinationError.clear();
        options.dataDir = data;
        game = options.effectiveBuildDir() / (kWindows ? "dw5e.exe" : "dw5e");
    }

    const auto installed = [&] {
        return installState(data) == InstallState::Installed;
    };

    const auto startGame = [&] {
        if (!validateInstallDirectory(data, destinationError)) {
            std::cerr << "DW5E launcher error: " << destinationError << std::endl; return 1;
        }
        game = options.effectiveBuildDir() / (kWindows ? "dw5e.exe" : "dw5e");
        const fs::path gameData = data / "game" / "files";
        std::vector<std::string> gameArgs = {
            "--game_data_root=" + pathText(gameData),
            "--user_data_root=" + pathText(data),
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

    // Fast-path: if game is already built for this release and disc is installed, boot immediately!
    if (!release.empty() && layoutError.empty() && destinationError.empty() && !args.rebuild && !args.installOnly &&
        fs::exists(game) && firstLine(options.effectiveBuildDir() / "built-from.txt") == release && installed())
    {
        if (args.noWindow)
        {
            std::cout << "SUCCESS: Game already built and ready at " << pathText(game) << std::endl;
            return 0;
        }
        return startGame();
    }

    // Build-only prerequisites must not block an already-built game's fast path.
    if (layoutError.empty() && findLayout(root, kWindows, layout, layoutError))
    {
        options.recompiler = layout.recompiler;
        options.tools = layout.tools;
    }

    // Keep the package root in the idle/retry UI. Each SetupRun restores its
    // stable Linux builder before using it, even if player data was deleted.
    options.rebuild = args.rebuild;
    if (!args.iso.empty())
        options.discIso = args.iso;

    WizardConfig config;
    config.base = options;
    config.launcherDir = launcher;
    config.destinationError = destinationError;
    config.explicitDestination = explicitDestination;
    config.layoutError = layoutError;
    config.prefilledIso = args.iso;
    config.discInstalled = installed();
    config.noWindow = args.noWindow;
    config.closeWhenFinished = true;

    bool ok = runWizardWindow(config);
    if (ok)
    {
        options = config.base;
        data = options.effectiveDataDir();
        if (!release.empty() && !markBuilt()) {
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
