#include "setup_layout.h"

#include <fstream>
#include <array>
#include <algorithm>
#include <iterator>
#include <vector>

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

bool syncDirectory(const fs::path &source, const fs::path &target, std::string &error, bool keepSource)
{
    error.clear();
    std::error_code ec;
    const auto fail = [&](const std::string &operation, const fs::path &path) {
        error = operation + " " + path.string() + ": " + ec.message();
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
    while (it != end)
    {
        const auto entry = *it;
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
                error = "destination directory is a symlink: " + dest.string();
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

bool prepareLinuxBuildRoot(const fs::path &root, const fs::path &data, fs::path &buildRoot, std::string &error)
{
    buildRoot = data / "builder";
    std::error_code ec;
    fs::create_directories(buildRoot, ec);
    if (ec)
    {
        error = "cannot create builder directory " + buildRoot.string() + ": " + ec.message();
        return false;
    }
    for (const char *item : {"app", "rexglue-sdk", "tools", "dw5e_manifest.toml"})
    {
        const fs::path src = root / item, dst = buildRoot / item;
        if (fs::is_directory(src, ec))
        {
            if (!syncDirectory(src, dst, error, true))
                return false;
        }
        else if (fs::is_regular_file(src, ec))
        {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                error = "cannot copy builder input " + src.string() + ": " + ec.message();
                return false;
            }
        }
        else if (ec && ec != std::errc::no_such_file_or_directory)
        {
            error = "cannot inspect builder input " + src.string() + ": " + ec.message();
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
            error = "bundled Windows compiler toolchain is missing (" + layout.tools.llvmMingwRoot.string() + ")";
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
