#include "install_location.h"
#include "setup_layout.h"

#include <fstream>
#include <array>
#include <algorithm>
#include <iterator>
#include <vector>
#include <chrono>
#include <cstdlib>

namespace dw5e::setup {

namespace fs = std::filesystem;

namespace {
bool sameContents(const fs::path &a, const fs::path &b, std::error_code &ec)
{
    const auto aSize = fs::file_size(a, ec);
    if (ec) return false;
    const auto bSize = fs::file_size(b, ec);
    if (ec || aSize != bSize) return false;
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    if (!left || !right)
    {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }
    std::array<char, 65536> x{}, y{};
    while (left && right)
    {
        left.read(x.data(), x.size());
        right.read(y.data(), y.size());
        if (left.gcount() != right.gcount() ||
            !std::equal(x.begin(), x.begin() + left.gcount(), y.begin()))
            return false;
    }
    if (left.bad() || right.bad())
        ec = std::make_error_code(std::errc::io_error);
    return !ec;
}
} // namespace

bool syncDirectory(const fs::path &source, const fs::path &target, std::string &error, bool keepSource, const fs::path &excluded)
{
    error.clear();
    std::error_code ec;
    const auto fail = [&](const std::string &operation, const fs::path &path) {
        error = operation + " " + pathText(path) + ": " + ec.message();
        return false;
    };
    const bool sourceExists = fs::exists(source, ec);
    if (ec) return fail("cannot inspect source", source);
    if (!sourceExists) return true;
    const bool targetExists = fs::exists(target, ec);
    if (ec) return fail("cannot inspect destination", target);
    if (!targetExists && !keepSource)
    {
        fs::rename(source, target, ec);
        if (!ec) return true;
        ec.clear(); // A cross-filesystem move may still be copied.
    }
    fs::create_directories(target, ec);
    if (ec) return fail("cannot create directory", target);

    fs::recursive_directory_iterator it(source, fs::directory_options::none, ec), end;
    if (ec) return fail("cannot read directory", source);
    const auto targetAbsolute = fs::weakly_canonical(target, ec);
    if (ec) return fail("cannot resolve destination", target);
    const auto excludedAbsolute = excluded.empty() ? fs::path{} : fs::weakly_canonical(excluded, ec);
    if (ec) return fail("cannot resolve excluded installation directory", excluded);
    while (it != end)
    {
        const auto entry = *it;
        // A custom install can be inside the package tree. Never iterate the output we are creating.
        const auto sourceAbsolute = fs::weakly_canonical(entry.path(), ec);
        if (ec) return fail("cannot resolve source", entry.path());
        if (sourceAbsolute == targetAbsolute || (!excludedAbsolute.empty() && sourceAbsolute == excludedAbsolute)) {
            it.disable_recursion_pending();
            it.increment(ec);
            if (ec) return fail("cannot read directory", source);
            continue;
        }
        const fs::path dest = target / entry.path().lexically_relative(source);
        const auto sourceStatus = entry.symlink_status(ec);
        if (ec) return fail("cannot inspect source", entry.path());
        if (fs::is_symlink(sourceStatus))
        {
            const auto link = fs::read_symlink(entry.path(), ec);
            if (ec) return fail("cannot read symlink", entry.path());
            const auto destStatus = fs::symlink_status(dest, ec);
            if (ec == std::errc::no_such_file_or_directory) ec.clear();
            if (ec) return fail("cannot inspect destination", dest);
            bool equal = false;
            if (fs::is_symlink(destStatus))
            {
                equal = fs::read_symlink(dest, ec) == link;
                if (ec) return fail("cannot read symlink", dest);
            }
            if (!equal)
            {
                fs::remove(dest, ec);
                if (ec) return fail("cannot replace destination symlink", dest);
                fs::copy_symlink(entry.path(), dest, ec);
                if (ec) return fail("cannot copy symlink", dest);
            }
        }
        else if (fs::is_directory(sourceStatus))
        {
            if (fs::is_symlink(fs::symlink_status(dest, ec)))
            {
                error = "destination directory is a symlink: " + pathText(dest);
                return false;
            }
            if (ec == std::errc::no_such_file_or_directory) ec.clear();
            if (ec) return fail("cannot inspect destination", dest);
            fs::create_directories(dest, ec);
            if (ec) return fail("cannot create directory", dest);
            if (keepSource)
            {
                fs::permissions(dest, fs::perms::owner_all, fs::perm_options::add, ec);
                if (ec) return fail("cannot make builder directory writable", dest);
            }
        }
        else if (fs::is_regular_file(sourceStatus))
        {
            const bool exists = fs::exists(dest, ec);
            if (ec) return fail("cannot inspect destination", dest);
            bool equal = false;
            if (exists)
            {
                equal = sameContents(entry.path(), dest, ec);
                if (ec) return fail("cannot compare builder files", dest);
            }
            if (!equal)
            {
                fs::remove(dest, ec);
                if (ec) return fail("cannot replace builder file", dest);
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
                if (ec) return fail("cannot copy builder file", dest);
            }
            if (keepSource)
            {
                fs::permissions(dest, sourceStatus.permissions() | fs::perms::owner_write,
                                fs::perm_options::replace, ec);
                if (ec) return fail("cannot set builder file permissions", dest);
            }
        }
        it.increment(ec);
        if (ec) return fail("cannot read directory", source);
    }
    return true;
}

bool installReleaseLauncher(fs::path &root, const fs::path &data, std::string &error)
{
    const auto folder = data.parent_path();
#ifdef _WIN32
    if (_wcsicmp(data.filename().c_str(), L"game-files") != 0 ||
        _wcsicmp(folder.filename().c_str(), L"DW5E-Recomp") != 0)
#else
    if (data.filename() != "game-files" || folder.filename() != "DW5E-Recomp")
#endif
        return true;

    std::error_code ec;
    const auto fail = [&](const fs::path &path) {
        error = "Cannot install launcher file " + pathText(path) + ": " + ec.message();
        return false;
    };
    const auto copy = [&](const fs::path &source, const fs::path &target) {
        if (fs::is_regular_file(target, ec) && sameContents(source, target, ec))
            return true;
        const auto temporary = folder / (".dw5e-launcher-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!fs::create_directory(temporary, ec)) {
            if (!ec) ec = std::make_error_code(std::errc::file_exists);
            return fail(temporary);
        }
        const auto staged = temporary / target.filename();
        fs::copy_file(source, staged, fs::copy_options::none, ec);
        if (!ec)
            fs::rename(staged, target, ec);
        const bool copied = !ec || fail(target);
        std::error_code cleanup;
        fs::remove(staged, cleanup);
        fs::remove(temporary, cleanup);
        return copied;
    };
#ifdef _WIN32
    if (root.filename() != "game-files" || fs::equivalent(root, data, ec))
        return true;
    for (const auto *item : {"launcher", "app", "patches", "tools", "rexglue-sdk", "LICENSES"})
        if (!syncDirectory(root / item, data / item, error, true, folder))
            return false;
    for (const auto *item : {"dw5e_manifest.toml", "LICENSE", "README.md", "BUILD_ID"})
        if (!copy(root / item, data / item))
            return false;
    if (!copy(root.parent_path() / "README.txt", folder / "README.txt") ||
        !copy(root.parent_path() / "DW5E.exe", folder / "DW5E.exe"))
        return false;
    root = data;
#else
    const char *image = std::getenv("APPIMAGE");
    if (!image || !*image)
        return true;
    const auto source = pathFromText(image);
    const auto launcher = folder / "DW5E.AppImage";
    if (fs::equivalent(source, launcher, ec))
        return true;
    if (!copy(root / "README.txt", folder / "README.txt") || !copy(source, launcher))
        return false;
    fs::permissions(launcher, fs::perms::owner_exec, fs::perm_options::add, ec);
    if (ec)
        return fail(launcher);
#endif
    return true;
}

bool prepareLinuxBuildRoot(const fs::path &root, const fs::path &data, fs::path &buildRoot, std::string &error)
{
    buildRoot = data / "builder";
    std::error_code ec;
    fs::create_directories(buildRoot, ec);
    if (ec)
    {
        error = "cannot create builder directory " + pathText(buildRoot) + ": " + ec.message();
        return false;
    }
    for (const char *item : {"app", "rexglue-sdk", "tools", "dw5e_manifest.toml"})
    {
        const fs::path src = root / item, dst = buildRoot / item;
        if (fs::is_directory(src, ec))
        {
            if (!syncDirectory(src, dst, error, true, src.lexically_normal() == data.lexically_normal() ? buildRoot : data))
                return false;
        }
        else if (fs::is_regular_file(src, ec))
        {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                error = "cannot copy builder input " + pathText(src) + ": " + ec.message();
                return false;
            }
        }
        else if (ec && ec != std::errc::no_such_file_or_directory)
        {
            error = "cannot inspect builder input " + pathText(src) + ": " + ec.message();
            return false;
        }
        ec.clear();
    }
    const auto read = [](const fs::path &path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const std::string version = read(root / "BUILD_ID");
    if (!version.empty() && version != read(buildRoot / "BUILD_ID"))
    {
        std::ofstream out(buildRoot / "BUILD_ID", std::ios::binary | std::ios::trunc);
        out << version;
        if (!out.flush())
        {
            error = "cannot write Linux builder version in " + pathText(buildRoot);
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
    error.clear();
    const bool release = fs::is_regular_file(root / "BUILD_ID", ec);
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
        if (fs::is_regular_file(cand, ec))
        {
            layout.recompiler = cand;
            break;
        }
    }
    if (layout.recompiler.empty())
    {
        if (release)
        {
            error = "bundled ReXGlue recompiler is missing; extract a complete release package";
            return false;
        }
        layout.recompiler = "rexglue"; // Developer checkout: deliberate host-tool discovery.
    }

    // Discover CMake
    const fs::path bundledCmake = tools / "cmake" / "bin" / ("cmake" + exe);
    if (release && !fs::is_regular_file(bundledCmake, ec))
    {
        error = "bundled CMake is missing; extract a complete release package";
        return false;
    }
    layout.tools.cmake = fs::is_regular_file(bundledCmake, ec) ? bundledCmake : fs::path("cmake");

    // Discover Ninja
    const fs::path bundledNinja = tools / ("ninja" + exe);
    if (fs::exists(bundledNinja, ec))
        layout.tools.ninja = bundledNinja;
    else if (fs::exists(tools / "ninja" / ("ninja" + exe), ec))
        layout.tools.ninja = tools / "ninja" / ("ninja" + exe);

    if (release && layout.tools.ninja.empty())
    {
        error = "bundled Ninja is missing; extract a complete release package";
        return false;
    }
    if (windows)
    {
        layout.tools.llvmMingwRoot = tools / "llvm-mingw";
        if (release && !fs::is_regular_file(layout.tools.llvmMingwRoot /
            "bin" / "x86_64-w64-mingw32-clang++.exe", ec))
        {
            error = "bundled Windows compiler toolchain is missing (" + pathText(layout.tools.llvmMingwRoot) + ")";
            return false;
        }
    }
    else if (release)
    {
        // Linux release package
        if (fs::exists(tools / "clang", ec))
            layout.tools.linuxToolchainRoot = tools / "clang";
        else if (fs::exists(root / "usr" / "sdk", ec))
            layout.tools.linuxToolchainRoot = root / "usr" / "sdk";
        if (layout.tools.linuxToolchainRoot.empty() ||
            !fs::is_regular_file(layout.tools.linuxToolchainRoot / "bin" / "clang++", ec))
        {
            error = "bundled Linux compiler is missing; extract a complete release package";
            return false;
        }
    }

    return true;
}

} // namespace dw5e::setup
