#include "codegen.h"
#include "install_state.h"
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

Args parseArgs(int argc, char **argv)
{
    Args a;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = (i + 1 < argc);
        if ((arg == "--iso" || arg == "--disc") && hasValue)
            a.iso = argv[++i];
        else if (arg == "--root" && hasValue)
            a.root = argv[++i];
        else if (arg == "--no-window")
            a.noWindow = true;
        else if (arg == "--install")
        {
            a.installOnly = true;
            a.noWindow = true;
        }
        else if (arg == "--rebuild")
            a.rebuild = true;
    }
    return a;
}
} // namespace

int main(int argc, char **argv)
{
    const Args args = parseArgs(argc, argv);

    std::error_code ec;
    fs::path root = args.root;
    if (root.empty())
    {
        fs::path exeDir = (argc > 0) ? fs::absolute(argv[0], ec).parent_path() : fs::current_path(ec);
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
    fs::path game, stamp, data;

    if (!release.empty())
    {
        data = playerDataDir();
        if (data.empty())
            layoutError = "no player data directory found (LOCALAPPDATA or HOME not set)";
        options.dataDir = data;
        game = options.effectiveBuildDir() / (kWindows ? "dw5e.exe" : "dw5e");
        stamp = options.effectiveBuildDir() / "built-from.txt";
    }

    const auto installed = [&] {
        return installState(data) == InstallState::Installed;
    };

    const auto startGame = [&] {
        const fs::path gameData = data / "game" / "files";
        std::vector<std::string> gameArgs = {
            "--game_data_root=" + gameData.string(),
            "--user_data_root=" + data.string(),
            "--cache_root=" + (data / "cache").string(),
            "--gpu_plugin=xenos"
        };
        std::vector<std::pair<std::string, std::string>> env;
#ifndef _WIN32
        // Ensure the build directory and local runtime libraries take priority in LD_LIBRARY_PATH
        std::string ldPath = options.effectiveBuildDir().string() + ":" +
                             (options.effectiveBuildDir() / "rexglue-sdk" / "src" / "system").string() + ":" +
                             (options.effectiveBuildDir() / "rexglue-sdk" / "thirdparty").string();
        if (fs::exists(options.root / "tools" / "clang" / "lib"))
            ldPath += ":" + (options.root / "tools" / "clang" / "lib").string();
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
        std::ofstream out(stamp, std::ios::trunc);
        out << release << "\n";
    };

    // Fast-path: if game is already built for this release and disc is installed, boot immediately!
    if (!release.empty() && layoutError.empty() && !args.rebuild && !args.installOnly &&
        fs::exists(game) && firstLine(stamp) == release && installed())
    {
        if (args.noWindow)
        {
            std::cout << "SUCCESS: Game already built and ready at " << game << std::endl;
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
    config.layoutError = layoutError;
    config.prefilledIso = args.iso;
    config.discInstalled = installed();
    config.noWindow = args.noWindow;
    config.closeWhenFinished = true;

    bool ok = runWizardWindow(config);
    if (ok)
    {
        if (!release.empty())
            markBuilt();

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
