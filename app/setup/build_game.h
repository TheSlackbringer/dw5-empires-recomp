#pragma once
// CMake & Ninja build invoker for compiling recompiled game code on player's machine.
// Supports Windows llvm-mingw and Linux relocatable Clang toolchains.
// Implements resetBuildIfMoved (commit b350108) and parseNinjaProgress.

#include "setup_layout.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace dw5e::setup {

struct BuildProgress
{
    int done = 0;
    int total = 0;
    std::string phase; // "configure" or "build"
};

// Generates CMake arguments for configuring the game executable build.
std::vector<std::string> gameConfigureArgs(const std::filesystem::path &root,
                                           const std::filesystem::path &buildDir,
                                           const BuildTools &tools,
                                           const std::filesystem::path &generatedDir = {});

// Detects if the build directory was previously configured for a different source folder.
// If moved, clears CMakeCache.txt and CMakeFiles/ so configure starts fresh without breaking Ninja.
// (Commit b350108)
bool resetBuildIfMoved(const std::filesystem::path &buildDir, const std::filesystem::path &sourceDir);

// Parses "[done/total]" progress strings from Ninja build output logs.
BuildProgress parseNinjaProgress(const std::string &logText);

// Invokes CMake configure and build for target dw5e. Progress callback receives real-time progress.
bool buildGame(const std::filesystem::path &root,
               const std::filesystem::path &buildDir,
               const BuildTools &tools,
               const std::filesystem::path &logFile,
               const std::function<void(const BuildProgress &)> &progress,
               std::filesystem::path &executable,
               std::string &error,
               const std::atomic<bool> *cancel = nullptr,
               const std::filesystem::path &generatedDir = {});

} // namespace dw5e::setup
