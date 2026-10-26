#pragma once
// Release package layout and directory discovery.
// Discovers project roots, toolchains (bundled or host), and handles staging to <player-data>/builder
// for AppImage FUSE stability (commit 553f229).

#include <filesystem>
#include <string>

namespace dw5e::setup {

struct BuildTools
{
    std::filesystem::path cmake = "cmake";
    std::filesystem::path ninja;
    std::filesystem::path llvmMingwRoot;      // Windows: bundled llvm-mingw
    std::filesystem::path linuxToolchainRoot; // Linux: bundled offline Clang SDK
};

struct SetupLayout
{
    std::filesystem::path root;
    std::filesystem::path recompiler;
    BuildTools tools;
};

// Traverses parent directories to locate the package or repository root (contains dw5e_manifest.toml or app/CMakeLists.txt).
std::filesystem::path findRoot(const std::filesystem::path &start);

// Discovers the recompiler CLI and build tools within the package or host environment.
bool findLayout(const std::filesystem::path &root, bool windows, SetupLayout &layout, std::string &error);

// Synchronizes source tree into target directory. Incremental: only copies new/modified files.
bool syncDirectory(const std::filesystem::path &source, const std::filesystem::path &target, std::string &error, bool keepSource = false);

// Stages package sources into stable <dataDir>/builder path on Linux to avoid CMakeCache.txt
// invalidation caused by random AppImage FUSE mount points (/tmp/.mount_xxxxxx) (commit 553f229).
bool prepareLinuxBuildRoot(const std::filesystem::path &root, const std::filesystem::path &data, std::filesystem::path &buildRoot, std::string &error);

} // namespace dw5e::setup
