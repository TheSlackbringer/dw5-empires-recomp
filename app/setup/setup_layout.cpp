#include "setup_layout.h"

#include <fstream>
#include <iterator>
#include <vector>

namespace dw5e::setup {

namespace fs = std::filesystem;

bool syncDirectory(const fs::path &source, const fs::path &target, std::string &error, bool keepSource)
{
    std::error_code ec;
    if (!fs::exists(source, ec))
        return true; // Nothing to sync

    if (!fs::exists(target, ec))
    {
        if (!keepSource)
        {
            fs::rename(source, target, ec);
            if (!ec)
                return true;
        }
        fs::create_directories(target, ec);
    }

    // Copy new or modified files
    for (const auto &entry : fs::recursive_directory_iterator(source, fs::directory_options::none, ec))
    {
        const fs::path rel = entry.path().lexically_relative(source);
        const fs::path dest = target / rel;

        if (entry.is_directory(ec))
        {
            fs::create_directories(dest, ec);
            if (!ec && keepSource)
                fs::permissions(dest, fs::perms::owner_all, fs::perm_options::add, ec);
            continue;
        }

        if (entry.is_symlink(ec))
        {
            fs::create_directories(dest.parent_path(), ec);
            fs::remove(dest, ec);
            fs::copy_symlink(entry.path(), dest, ec);
            continue;
        }

        bool needCopy = !fs::exists(dest, ec);
        if (!needCopy)
        {
            const auto srcTime = fs::last_write_time(entry.path(), ec);
            const auto destTime = fs::last_write_time(dest, ec);
            const auto srcSize = fs::file_size(entry.path(), ec);
            const auto destSize = fs::file_size(dest, ec);
            if (srcTime > destTime || srcSize != destSize)
                needCopy = true;
        }

        if (needCopy)
        {
            fs::create_directories(dest.parent_path(), ec);
            fs::remove(dest, ec);
            fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
            if (!ec && keepSource)
                fs::permissions(dest, fs::perms::owner_write, fs::perm_options::add, ec);
            if (ec)
            {
                error = "failed to copy " + entry.path().string() + " to " + dest.string() + ": " + ec.message();
                return false;
            }
        }
    }

    return true;
}

bool prepareLinuxBuildRoot(const fs::path &root, const fs::path &data, fs::path &buildRoot, std::string &error)
{
    buildRoot = data / "builder";
    std::error_code ec;
    fs::create_directories(buildRoot, ec);

    for (const char *item : {"app", "rexglue-sdk", "tools", "dw5e_manifest.toml"})
    {
        const fs::path src = root / item;
        const fs::path dst = buildRoot / item;
        if (fs::is_directory(src, ec))
        {
            if (!syncDirectory(src, dst, error, true))
                return false;
        }
        else if (fs::is_regular_file(src, ec))
        {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        }
    }

    const auto read = [](const fs::path &p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };

    const std::string version = read(root / "BUILD_ID");
    if (!version.empty() && version != read(buildRoot / "BUILD_ID"))
    {
        std::ofstream out(buildRoot / "BUILD_ID", std::ios::binary | std::ios::trunc);
        out << version;
        if (!out.flush())
        {
            error = "cannot write Linux builder version in " + buildRoot.string();
            return false;
        }
    }
    return true;
}

fs::path findRoot(const fs::path &start)
{
    std::error_code ec;
    fs::path dir = fs::absolute(start, ec).lexically_normal();
    while (!dir.empty())
    {
        if (fs::exists(dir / "app" / "CMakeLists.txt", ec) &&
            (fs::exists(dir / "app" / "dw5e_manifest.toml", ec) || fs::exists(dir / "dw5e_manifest.toml", ec)))
        {
            return dir;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir)
            break;
        dir = parent;
    }
    return {};
}

bool findLayout(const fs::path &root, bool windows, SetupLayout &layout, std::string &error)
{
    const std::string exe = windows ? ".exe" : "";
    const fs::path tools = root / "tools";
    std::error_code ec;
    layout = {};
    layout.root = root;

    // Discover recompiler CLI
    const std::vector<fs::path> recompCandidates = {
        tools / ("rexglue" + exe),
        tools / ("rexgluerd" + exe),
        root / "usr" / "bin" / ("rexglue" + exe),
        root / "usr" / "bin" / ("rexgluerd" + exe),
        root / "rexglue-sdk" / "out" / "linux-amd64" / "rexgluerd",
        root / "rexglue-sdk" / "out" / "linux-amd64" / "rexglue",
        root / "rexglue-sdk" / "out" / "linux-amd64" / "RelWithDebInfo" / "rexgluerd",
        root / "rexglue-sdk" / "out" / "bin" / ("rexglue" + exe),
    };
    for (const auto &cand : recompCandidates)
    {
        if (fs::exists(cand, ec))
        {
            layout.recompiler = cand;
            break;
        }
    }
    if (layout.recompiler.empty())
        layout.recompiler = "rexglue"; // Fallback to PATH

    // Discover CMake
    const fs::path bundledCmake = tools / "cmake" / "bin" / ("cmake" + exe);
    layout.tools.cmake = fs::exists(bundledCmake, ec) ? bundledCmake : fs::path("cmake");

    // Discover Ninja
    const fs::path bundledNinja = tools / ("ninja" + exe);
    if (fs::exists(bundledNinja, ec))
        layout.tools.ninja = bundledNinja;
    else if (fs::exists(tools / "ninja" / ("ninja" + exe), ec))
        layout.tools.ninja = tools / "ninja" / ("ninja" + exe);

    if (windows)
    {
        layout.tools.llvmMingwRoot = tools / "llvm-mingw";
        if (!fs::is_directory(layout.tools.llvmMingwRoot, ec) && fs::exists(root / "BUILD_ID", ec))
        {
            error = "bundled Windows compiler toolchain is missing (" + layout.tools.llvmMingwRoot.string() + ")";
            return false;
        }
    }
    else if (fs::exists(root / "BUILD_ID", ec))
    {
        // Linux release package
        if (fs::exists(tools / "clang", ec))
            layout.tools.linuxToolchainRoot = tools / "clang";
        else if (fs::exists(root / "usr" / "sdk", ec))
            layout.tools.linuxToolchainRoot = root / "usr" / "sdk";
    }

    return true;
}

} // namespace dw5e::setup
