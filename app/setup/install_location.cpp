#include "install_location.h"
#include "../../rexglue-sdk/thirdparty/inja/third_party/include/nlohmann/json.hpp"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace dw5e::setup
{
namespace fs = std::filesystem;
std::string pathText(const fs::path &p)
{
    const auto s = p.generic_u8string();
    return {s.begin(), s.end()};
}
fs::path pathFromText(const std::string &s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}
namespace
{
std::atomic<unsigned long> sequence{0};
fs::path uniqueFile(const fs::path &directory, const std::string &prefix)
{
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    return directory / (prefix + std::to_string(pid) + "-" + std::to_string(++sequence));
}
bool failure(const fs::path &path, const std::string &detail, std::string &error)
{
    error = "Cannot use '" + pathText(path) + "': " + detail +
            ". Choose another folder or move the release to a writable folder.";
    return false;
}
bool writeExclusive(const fs::path &path, const std::string &text, std::string &error)
{
#ifdef _WIN32
    HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return failure(path, "cannot create file (Windows error " + std::to_string(GetLastError()) + ")",
                       error);
    DWORD written = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                    written == text.size() && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != 0;
#else
    const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0)
        return failure(path, std::error_code(errno, std::generic_category()).message(), error);
    size_t offset = 0;
    while (offset < text.size())
    {
        const auto n = write(file, text.data() + offset, text.size() - offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        offset += size_t(n);
    }
    const bool ok = offset == text.size() && fsync(file) == 0;
    const bool closed = close(file) == 0;
#endif
    if (!ok || !closed)
        return failure(path, "cannot finish writing file", error);
    return true;
}
fs::path normalizedDirectory(const fs::path &path)
{
    auto result = path.lexically_normal();
    if (result.has_relative_path() && result.filename().empty())
        result = result.parent_path();
    return result;
}
fs::path relativeChoice(const fs::path &path, const fs::path &base)
{
    auto child = path.begin();
    for (auto parent = base.begin(); parent != base.end(); ++parent, ++child)
    {
        if (child == path.end())
            return {};
#ifdef _WIN32
        if (_wcsicmp(child->c_str(), parent->c_str()) != 0)
            return {};
#else
        if (*child != *parent)
            return {};
#endif
    }
    fs::path relative;
    for (; child != path.end(); ++child)
        relative /= *child;
    return relative.empty() ? fs::path(".") : relative;
}
} // namespace
std::vector<std::string> nativeArguments(int argc, char **argv)
{
    std::vector<std::string> args;
#ifdef _WIN32
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide)
        return {};
    for (int i = 0; i < count; ++i)
    {
        const auto text = fs::path(wide[i]).u8string();
        args.emplace_back(text.begin(), text.end());
    }
    LocalFree(wide);
#else
    for (int i = 0; i < argc; ++i)
        args.emplace_back(argv[i]);
#endif
    return args;
}
fs::path executableDirectory(std::string &error)
{
    error.clear();
#ifndef _WIN32
    std::vector<char> buf(1024);
    while (true)
    {
        const auto n = readlink("/proc/self/exe", buf.data(), buf.size());
        if (n < 0)
        {
            error = "Cannot discover the actual launcher location; no installation directory was selected.";
            return {};
        }
        if (size_t(n) < buf.size())
        {
            buf.resize(size_t(n));
            break;
        }
        buf.resize(buf.size() * 2);
    }
    const auto executable = pathFromText(std::string(buf.begin(), buf.end()));
#else
    std::vector<wchar_t> buf(1024);
    while (true)
    {
        const auto n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
        {
            error = "Cannot discover the actual Windows launcher location.";
            return {};
        }
        if (n < buf.size())
        {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    const fs::path executable(std::wstring(buf.begin(), buf.end()));
#endif
    return executable.parent_path();
}
fs::path launcherDirectory(std::string &error)
{
    error.clear();
#ifndef _WIN32
    if (const char *image = std::getenv("APPIMAGE"); image && *image)
    {
        std::error_code ec;
        const auto path = fs::absolute(pathFromText(image), ec).lexically_normal();
        if (ec || !fs::is_regular_file(path, ec))
        {
            failure(path, "original AppImage is unavailable", error);
            return {};
        }
        return path.parent_path();
    }
#endif
    const auto directory = executableDirectory(error);
#ifndef _WIN32
    for (const auto &part : directory)
        if (pathText(part).starts_with(".mount_"))
        {
            error = "Cannot discover the original AppImage location (APPIMAGE is missing); run the original "
                    "AppImage. No mount/home fallback was selected.";
            return {};
        }
#endif
    // The Windows package keeps its real wizard and DLLs in the payload.
#ifdef _WIN32
    const bool packaged = _wcsicmp(directory.filename().c_str(),L"launcher")==0 &&
        _wcsicmp(directory.parent_path().filename().c_str(),L"game-files")==0;
#else
    const bool packaged = directory.filename()=="launcher" && directory.parent_path().filename()=="game-files";
#endif
    if (packaged && fs::is_regular_file(directory.parent_path() / "BUILD_ID"))
        return directory.parent_path().parent_path();
    return directory;
}
bool resolveInstallLocation(const fs::path &launcher, const fs::path &commandLine, fs::path &destination,
                            bool &explicitOverride, std::string &error)
{
    destination.clear();
    error.clear();
    explicitOverride = false;
    const auto explicitPath = [&](const fs::path &path) {
        std::error_code ec;
        destination = normalizedDirectory(fs::absolute(path, ec));
        explicitOverride = true;
        return !ec || failure(path, ec.message(), error);
    };
    if (!commandLine.empty())
        return explicitPath(commandLine);
#ifdef _WIN32
    if (const wchar_t *value = _wgetenv(L"DW5E_DATA_DIR"); value && *value)
        return explicitPath(fs::path(value));
#else
    if (const char *value = std::getenv("DW5E_DATA_DIR"); value && *value)
        return explicitPath(pathFromText(value));
#endif
    if (launcher.empty())
    {
        error = "Cannot discover the launcher directory; no installation location was selected.";
        return false;
    }
    const auto config = launcher / "dw5e-install.json";
    std::error_code ec;
    const bool exists = fs::exists(config, ec);
    if (ec)
        return failure(config, ec.message(), error);
    if (!exists)
    {
        const auto child = launcher / "game-files";
        destination = fs::is_regular_file(child / "game/installed.txt") ? child :
            fs::is_regular_file(launcher / "game/installed.txt") ? launcher : child;
        return true;
    }
    if (!fs::is_regular_file(config, ec) || fs::file_size(config, ec) > 65536 || ec)
        return failure(config, "installation choice is not a readable small configuration file", error);
    try
    {
        std::ifstream in(config, std::ios::binary);
        if (!in)
            return failure(config, "cannot read remembered installation choice", error);
        const auto json = nlohmann::json::parse(in);
        if (!json.is_object() || !json.contains("version") || !json["version"].is_number_integer() ||
            json["version"] != 1 || !json.contains("data_dir") || !json["data_dir"].is_string())
            return failure(config, "invalid or unsupported installation choice", error);
        const auto text = json["data_dir"].get<std::string>();
        if (text.empty() || text.find('\0') != std::string::npos)
            return failure(config, "empty or invalid destination", error);
        const auto chosen = pathFromText(text);
        destination = normalizedDirectory(chosen.is_absolute() ? chosen : launcher / chosen);
        if (!fs::is_directory(destination, ec) || ec)
            return failure(destination,
                           "remembered installation folder is unavailable; select it explicitly to change it",
                           error);
    }
    catch (const std::exception &)
    {
        return failure(config, "malformed installation choice; select a folder explicitly to repair it",
                       error);
    }
    return true;
}
bool validateInstallDirectory(const fs::path &directory, std::string &error)
{
    error.clear();
    if (directory.empty())
        return failure(directory, "no installation folder selected", error);
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec || !fs::is_directory(directory, ec))
        return failure(directory, ec ? ec.message() : "not a directory", error);
    const auto probe = uniqueFile(directory, ".dw5e-write-check-");
    const bool written = writeExclusive(probe, "DW5E destination access check\n", error);
    if (!written)
        return false;
    fs::remove(probe, ec);
    if (ec)
        return failure(probe, "cannot remove the access-check file: " + ec.message(), error);
    return true;
}
bool rememberInstallLocation(const fs::path &launcher, const fs::path &destination, std::string &error)
{
    error.clear();
    const auto base = normalizedDirectory(launcher), chosen = normalizedDirectory(destination);
    const auto relative = relativeChoice(chosen, base);
    const auto stored = relative.empty() ? chosen : relative;
    const auto text = nlohmann::json{{"version", 1}, {"data_dir", pathText(stored)}}.dump(2) + "\n";
    const auto config = base / "dw5e-install.json";
    // The fixed default needs no loose sidecar in a newly extracted release.
    if (chosen == base / "game-files" && !fs::exists(config)) return true;
    return writeAtomicText(config, text, error);
}
bool writeAtomicText(const fs::path &config, const std::string &text, std::string &error)
{
    std::error_code directoryError;
    fs::create_directories(config.parent_path(), directoryError);
    if (directoryError) return failure(config, directoryError.message(), error);
    const auto temp = uniqueFile(config.parent_path(), pathText(config.filename()) + ".tmp-");
    if (!writeExclusive(temp, text, error))
        return false;
#ifdef _WIN32
    const bool moved =
        MoveFileExW(temp.c_str(), config.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!moved)
        error = "Cannot atomically save '" + pathText(config) + "' (Windows error " +
                std::to_string(GetLastError()) + "). Check access to this folder.";
#else
    const bool moved = rename(temp.c_str(), config.c_str()) == 0;
    if (!moved)
        failure(config, "cannot atomically save the installation choice", error);
#endif
    if (!moved)
    {
        std::error_code ec;
        fs::remove(temp, ec);
    }
    return moved;
}
bool InstallFolderSelection::choose(const fs::path &path)
{
    if (frozen || explicitOverride)
        return false;
    if (path.empty())
        return true; // A cancelled picker keeps the prior choice and error.
    destination = normalizedDirectory(path.is_absolute() ? path : launcher / path);
    error.clear();
    return true;
}
bool InstallFolderSelection::prepare()
{
    if (!error.empty())
        return false;
    if (!validateInstallDirectory(destination, error))
        return false;
    if (!explicitOverride && !launcher.empty() && !rememberInstallLocation(launcher, destination, error))
        return false;
    frozen = true;
    return true;
}
} // namespace dw5e::setup
