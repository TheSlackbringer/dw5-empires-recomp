#include "build_game.h"
#include "run_process.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <regex>
#include <thread>

namespace dw5e::setup {

namespace fs = std::filesystem;

namespace {
std::string utf8(const fs::path &path)
{
    return path.generic_string();
}

std::string readText(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

ProcessResult runWithProgress(const fs::path &program,
                              const std::vector<std::string> &args,
                              const fs::path &workingDirectory,
                              const fs::path &logFile,
                              const std::string &phase,
                              const std::function<void(const BuildProgress &)> &progress,
                              const std::atomic<bool> *cancel)
{
    ProcessResult result;
    std::atomic<bool> finished{false};
    std::thread worker([&] {
        result = runProcess(program, args, workingDirectory, logFile, cancel);
        finished = true;
    });

    while (!finished)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (progress)
        {
            BuildProgress p = parseNinjaProgress(readText(logFile));
            p.phase = phase;
            progress(p);
        }
    }
    worker.join();
    return result;
}
} // namespace

std::vector<std::string> gameConfigureArgs(const fs::path &root,
                                           const fs::path &buildDir,
                                           const BuildTools &tools,
                                           const fs::path &generatedDir)
{
    std::vector<std::string> args = {
        "-S", utf8(root / "app"),
        "-B", utf8(buildDir),
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        "-DDW5E_BUILD_GAME=ON"
    };

    // If rexglue-sdk is present in root, point to it
    if (fs::exists(root / "rexglue-sdk"))
        args.push_back("-DREXSDK_DIR=" + utf8(root / "rexglue-sdk"));

    if (!generatedDir.empty())
        args.push_back("-DDW5E_GENERATED_DIR=" + utf8(generatedDir));

    if (!tools.ninja.empty())
        args.push_back("-DCMAKE_MAKE_PROGRAM=" + utf8(tools.ninja));

    if (!tools.llvmMingwRoot.empty())
    {
        fs::path toolchain = root / "tools" / "windows" / "llvm-mingw.cmake";
        if (fs::exists(toolchain))
            args.push_back("-DCMAKE_TOOLCHAIN_FILE=" + utf8(toolchain));
        args.push_back("-DLLVM_MINGW_ROOT=" + utf8(tools.llvmMingwRoot));
    }
    else if (!tools.linuxToolchainRoot.empty())
    {
        fs::path toolchain = root / "tools" / "linux" / "clang.cmake";
        if (fs::exists(toolchain))
            args.push_back("-DCMAKE_TOOLCHAIN_FILE=" + utf8(toolchain));
        args.push_back("-DDW5E_LINUX_TOOLCHAIN_ROOT=" + utf8(tools.linuxToolchainRoot));
    }

    return args;
}

bool resetBuildIfMoved(const fs::path &buildDir, const fs::path &sourceDir)
{
    std::ifstream cache(buildDir / "CMakeCache.txt");
    if (!cache)
        return false;

    const std::string key = "CMAKE_HOME_DIRECTORY:INTERNAL=";
    std::string line, configured;
    while (std::getline(cache, line))
    {
        if (line.rfind(key, 0) == 0)
            configured = line.substr(key.size());
    }
    cache.close();

    while (!configured.empty() && (configured.back() == '\r' || configured.back() == ' '))
        configured.pop_back();

    auto normal = [](const fs::path &p) {
        std::string text = utf8(p.lexically_normal());
        while (text.size() > 1 && text.back() == '/')
            text.pop_back();
#ifdef _WIN32
        for (char &c : text)
            c = char(std::tolower(static_cast<unsigned char>(c)));
#endif
        return text;
    };

    if (configured.empty() || normal(fs::path(configured)) == normal(sourceDir))
        return false;

    std::error_code ec;
    fs::remove(buildDir / "CMakeCache.txt", ec);
    fs::remove_all(buildDir / "CMakeFiles", ec);
    return true;
}

BuildProgress parseNinjaProgress(const std::string &logText)
{
    BuildProgress p;
    static const std::regex re(R"(\[(\d+)/(\d+)\])");
    const size_t from = logText.size() > 4096 ? logText.size() - 4096 : 0;
    const std::string tail = logText.substr(from);
    for (std::sregex_iterator it(tail.begin(), tail.end(), re), end; it != end; ++it)
    {
        p.done = std::stoi((*it)[1].str());
        p.total = std::stoi((*it)[2].str());
    }
    return p;
}

bool buildGame(const fs::path &root,
               const fs::path &buildDir,
               const BuildTools &tools,
               const fs::path &logFile,
               const std::function<void(const BuildProgress &)> &progress,
               fs::path &executable,
               std::string &error,
               const std::atomic<bool> *cancel,
               const fs::path &generatedDir)
{
    std::error_code ec;
    fs::create_directories(buildDir, ec);
    fs::create_directories(logFile.parent_path(), ec);
    fs::remove(logFile, ec);

    if (resetBuildIfMoved(buildDir, root / "app"))
    {
        std::ofstream(logFile, std::ios::app) << "-- release moved to " << utf8(root)
                                              << ": build configuration was reset\n";
    }

    const ProcessResult configure =
        runWithProgress(tools.cmake, gameConfigureArgs(root, buildDir, tools, generatedDir),
                        root, logFile, "configure", progress, cancel);

    if (configure.cancelled)
    {
        error = "cancelled by user";
        return false;
    }
    if (!configure.started)
    {
        error = configure.error;
        return false;
    }
    if (configure.exitCode != 0)
    {
        error = "configuring the build failed; check log at " + logFile.string();
        return false;
    }

    const ProcessResult build =
        runWithProgress(tools.cmake, {"--build", utf8(buildDir), "--target", "dw5e"},
                        root, logFile, "build", progress, cancel);

    if (build.cancelled)
    {
        error = "cancelled by user";
        return false;
    }
    if (!build.started)
    {
        error = build.error;
        return false;
    }
    if (build.exitCode != 0)
    {
        error = "compiling the game failed; check log at " + logFile.string();
        return false;
    }

#ifdef _WIN32
    executable = buildDir / "dw5e.exe";
#else
    executable = buildDir / "dw5e";
#endif

    if (!fs::exists(executable, ec))
    {
        // Try searching in bin/
        if (fs::exists(buildDir / "bin" / executable.filename(), ec))
            executable = buildDir / "bin" / executable.filename();
        else
        {
            error = "build succeeded but executable missing at " + executable.string();
            return false;
        }
    }

#ifndef _WIN32
    // Colocate runtime shared libraries next to dw5e so $ORIGIN resolves them directly
    for (const auto &entry : fs::recursive_directory_iterator(buildDir, ec))
    {
        if (entry.is_regular_file(ec))
        {
            const std::string name = entry.path().filename().string();
            if (name.find(".so") != std::string::npos)
            {
                const fs::path target = buildDir / entry.path().filename();
                if (entry.path() != target)
                    fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
            }
        }
    }
    const fs::path clangLib = root / "tools" / "clang" / "lib";
    if (fs::exists(clangLib, ec))
    {
        for (const auto &entry : fs::directory_iterator(clangLib, ec))
        {
            if (entry.is_regular_file(ec))
            {
                const std::string name = entry.path().filename().string();
                if (name.rfind("libc++", 0) == 0 && name.find(".so") != std::string::npos)
                {
                    fs::copy_file(entry.path(), buildDir / entry.path().filename(),
                                  fs::copy_options::overwrite_existing, ec);
                }
            }
        }
    }
#endif

    return true;
}

} // namespace dw5e::setup
