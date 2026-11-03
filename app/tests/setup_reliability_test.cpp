#include "setup/build_game.h"
#include "setup/codegen.h"
#include "setup/install_state.h"
#include "setup/run_process.h"
#include "setup/setup_layout.h"
#include "setup/setup_pipeline.h"
#include "setup/xdvdfs.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace fs = std::filesystem;
using namespace dw5e::setup;
static int failures = 0;

static std::string read(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
static void write(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
static void check(bool ok, const char *name)
{
    std::cout << (ok ? "PASS: " : "FAIL: ") << name << '\n';
    failures += !ok;
}

static void testSetupProgress()
{
    const auto near = [](double value, double expected) { return std::abs(value - expected) < 0.0005; };

    // Compiler output longer than the parsed tail must not lose Ninja's counter.
    const BuildProgress seen{809, 932, "build"};
    const std::string warnings(5000, 'w');
    const BuildProgress kept = latestNinjaProgress(seen, "[809/932] Building\n" + warnings);
    check(kept.done == 809 && kept.total == 932, "compile counter survives long compiler output");
    const BuildProgress next = latestNinjaProgress(seen, warnings + "\n[810/932] Building\n");
    check(next.done == 810 && next.total == 932, "newer compile counter replaces the previous one");

    // Step shares measured by a timed clean setup (issue 010).
    const std::vector<Step> full = {Step::Check, Step::Extract, Step::Generate, Step::Build};
    check(near(overallProgress(full, Step::Extract, 0.0), 0.20), "disc check takes 20% of a full setup");
    check(near(overallProgress(full, Step::Generate, 0.0), 0.25), "extraction takes 5% of a full setup");
    check(near(overallProgress(full, Step::Build, 0.0), 0.30), "translation takes 5% of a full setup");
    check(near(overallProgress(full, Step::Build, 1.0), 1.0), "the build takes the remaining 70%");

    const BuildProgress compile{269, 932, "build"};
    check(near(buildStepFraction({0, 0, "configure"}), 0.05), "configuring reaches 5% of the build step");
    check(near(buildStepFraction({932, 932, "build"}), 1.0), "the last compile task finishes the build step");
    check(near(overallProgress(full, Step::Build, buildStepFraction(compile)), 0.5521),
          "269 of 932 compile tasks is 55% of a full setup");

    check(buildStepDetail(compile) == "Compiling 269 of 932 (29%)", "compile detail labels counter and its percentage");
    check(buildStepDetail({0, 0, "configure"}) == "Configuring CMake...", "configure detail is unchanged");
}

int main(int argc, char **argv)
{
    if (argc > 1)
    {
        // Deterministic fixture output on both CRTs; assertions compare the same bytes.
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        const std::string mode = argv[1];
        if (mode == "--child-args")
        {
            for (int i = 2; i < argc; ++i)
                std::cout << '[' << argv[i] << "]\n";
            return 7;
        }
        if (mode == "--child-env")
        {
            const char *value = std::getenv("DW5E_TEST_CHILD");
            std::cout << (value ? value : "missing") << '\n';
            return 0;
        }
        if (mode == "--child-wait")
        {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            return 0;
        }
        if (mode == "codegen")
        {
            const std::string manifest = read(argv[2]);
            const std::string key = "out_directory_path = \"";
            const auto begin = manifest.find(key) + key.size();
            const auto end = manifest.find('"', begin);
            const fs::path output = manifest.substr(begin, end - begin);
            write(output / "sources.cmake", "# synthetic codegen fixture\n");
            write(output / "dw5e_pch.h", "// synthetic codegen fixture\n");
            return 0;
        }
        if (mode == "-S")
            return 0;
        if (mode == "--build")
        {
#ifdef _WIN32
            write(fs::path(argv[2]) / "dw5e.exe", "synthetic unexecutable game fixture");
#else
            write(fs::path(argv[2]) / "dw5e", "synthetic unexecutable game fixture");
#endif
            return 0;
        }
    }

    testSetupProgress();

    const fs::path self = fs::absolute(argv[0]);
    const fs::path scratch = fs::temp_directory_path() /
        ("dw5e_reliability_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(scratch);

    auto result = runProcess(scratch / "missing-tool.exe", {}, scratch, scratch / "missing.log");
    check(!result.started && !result.error.empty() &&
          read(scratch / "missing.log").find(result.error) != std::string::npos,
          "missing process produces a useful log");

    result = runProcess(self, {"--child-args", "space inside", "quote\"inside", "C:\\space path\\", ""},
                        scratch, scratch / "args.log");
    check(result.started && result.exitCode == 7 &&
          read(scratch / "args.log") == "[space inside]\n[quote\"inside]\n[C:\\space path\\]\n[]\n",
          "arguments and real child exit code survive process launch");

    result = runProcess(self, {"--child-env"}, scratch, scratch / "env.log", nullptr,
                        {{"DW5E_TEST_CHILD", "child value"}});
    check(result.started && result.exitCode == 0 &&
          read(scratch / "env.log") == "child value\n",
          "per-child environment overrides reach the subprocess");

    std::atomic<bool> cancel{true};
    result = runProcess(self, {"--child-wait"}, scratch, scratch / "cancel.log", &cancel);
    check(result.cancelled, "cancelled process reports cancellation");

    const fs::path source = scratch / "sync-source", target = scratch / "sync-target";
    write(source / "same.cpp", "new");
    write(target / "same.cpp", "old");
    fs::last_write_time(source / "same.cpp", fs::last_write_time(target / "same.cpp"));
    std::string error;
    check(syncDirectory(source, target, error, true) && read(target / "same.cpp") == "new",
          "builder update copies equal-size equal-timestamp changed content");
    write(source / "same.cpp", "NEW");
    fs::last_write_time(source / "same.cpp",
                       fs::last_write_time(target / "same.cpp") - std::chrono::hours(1));
    check(syncDirectory(source, target, error, true) && read(target / "same.cpp") == "NEW",
          "builder update copies older-timestamp changed content");

    fs::create_directories(scratch / "bad-source/empty-dir");
    write(scratch / "not-a-directory", "fixture");
    error.clear();
    check(!syncDirectory(scratch / "bad-source", scratch / "not-a-directory", error, true) &&
          !error.empty(), "directory synchronization reports directory creation failures");

    const fs::path package = scratch / "release";
    write(package / "BUILD_ID", "new-version\n");
    fs::create_directories(package / "app/empty-dir");
    const fs::path data = scratch / "failed-update";
    write(data / "builder/BUILD_ID", "old-version\n");
    write(data / "builder/app", "blocks source directory");
    fs::path cached;
    error.clear();
    check(!prepareLinuxBuildRoot(package, data, cached, error) &&
          read(data / "builder/BUILD_ID") == "old-version\n",
          "failed builder synchronization does not stamp a new version");

    const fs::path incomplete = scratch / "incomplete-release";
    write(incomplete / "BUILD_ID", "fixture\n");
    fs::create_directories(incomplete / "tools/llvm-mingw");
    SetupLayout layout;
    error.clear();
    check(!findLayout(incomplete, true, layout, error) && !error.empty(),
          "incomplete release rejects host-tool fallback before installation");

    const fs::path generatedData = scratch / "codegen-error";
    write(generatedData / "game/files/default.xex", "synthetic input; never parsed");
    error.clear();
    bool generated = generateCode(scratch, scratch / "missing-translator.exe",
        generatedData / "logs", {}, error, generatedData);
    check(!generated && error.find("exit code -1") == std::string::npos &&
          error.find("start") != std::string::npos,
          "codegen reports a launch error instead of a fictitious child exit");

    const fs::path rebuildRoot = scratch / "rebuild";
    write(rebuildRoot / "game/files/default.xex", "synthetic fixture");
    writeInstallMarker(rebuildRoot, kExpectedDiscSha1, error);
    SetupOptions options;
    options.root = rebuildRoot;
    options.dataDir = rebuildRoot;
    options.recompiler = self;
    options.tools.cmake = self;
#ifdef _WIN32
    options.tools.llvmMingwRoot = scratch / "fake-compiler";
    write(options.tools.llvmMingwRoot / "bin/libc++.dll", "synthetic C++ runtime");
    write(options.tools.llvmMingwRoot / "bin/libunwind.dll", "synthetic unwind runtime");
#endif
    options.rebuild = true;
    SetupRun run(options);
    run.start();
    bool rebuilt = run.wait();
    const auto status = run.status();
    check(rebuilt && status.finished && !status.failed && fs::exists(status.executable),
          "installed-file rebuild completes without a disc image");
#ifdef _WIN32
    check(read(options.effectiveBuildDir() / "libc++.dll") == "synthetic C++ runtime" &&
          read(options.effectiveBuildDir() / "libunwind.dll") == "synthetic unwind runtime",
          "Windows player build stages compiler runtime DLLs beside the game");
#endif

#ifndef _WIN32
    // The UI keeps the package as its source while the player can reset data.
    const fs::path retryPackage = scratch / "retry-package";
    write(retryPackage / "BUILD_ID", "retry-release\n");
    write(retryPackage / "app/CMakeLists.txt", "# synthetic package\n");
    for (const char *tool : {"tools/rexglue", "tools/cmake/bin/cmake"})
    {
        const fs::path dest = retryPackage / tool;
        fs::create_directories(dest.parent_path());
        fs::copy_file(self, dest);
    }
    write(retryPackage / "tools/ninja", "synthetic tool discovery fixture");
    write(retryPackage / "tools/clang/bin/clang++", "synthetic tool discovery fixture");
    const fs::path retryData = scratch / "retry-data";
    fs::path idleCached;
    check(prepareLinuxBuildRoot(retryPackage, retryData, idleCached, error),
          "release builder is staged before the idle-window reset fixture");
    fs::remove_all(retryData); // Only this test's scratch, without a build/_deps tree.
    write(retryData / "game/files/default.xex", "synthetic installed input; never parsed");
    writeInstallMarker(retryData, kExpectedDiscSha1, error);
    SetupOptions retryOptions;
    retryOptions.root = retryPackage;
    retryOptions.dataDir = retryData;
    retryOptions.recompiler = idleCached / "tools/rexglue";
    retryOptions.tools.cmake = idleCached / "tools/cmake/bin/cmake";
    SetupRun retry(retryOptions);
    retry.start();
    const bool recovered = retry.wait();
    check(recovered && retry.status().finished &&
          fs::is_regular_file(idleCached / "tools/rexglue") &&
          fs::exists(retry.status().executable),
          "same-window setup restores a deleted data/builder directory before codegen");
#endif

    std::error_code cleanupError;
    fs::remove_all(scratch, cleanupError);
    std::cout << (failures ? "FAIL" : "PASS") << ": " << failures << " failed checks\n";
    return failures ? 1 : 0;
}
