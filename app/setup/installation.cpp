#include "installation.h"
#include "install_location.h"
#include "install_state.h"
#include "../../rexglue-sdk/thirdparty/inja/third_party/include/nlohmann/json.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#endif

namespace dw5e::setup
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace
{
fs::path environment(const char *key)
{
#ifdef _WIN32
    auto nativeKey = pathFromText(key).wstring();
    const wchar_t *value = _wgetenv(nativeKey.c_str());
    return value && *value ? fs::path(value) : fs::path{};
#else
    const char *value = std::getenv(key);
    return value && *value ? pathFromText(value) : fs::path{};
#endif
}
fs::path normalBase(bool config)
{
#ifdef _WIN32
    auto base = environment("LOCALAPPDATA");
    return base.empty() || !base.is_absolute() ? fs::path{} : base / "DW5E";
#else
    auto base = environment(config ? "XDG_CONFIG_HOME" : "XDG_DATA_HOME");
    if (base.empty() || !base.is_absolute())
    {
        auto home = environment("HOME");
        if (home.empty() || !home.is_absolute())
            return {};
        base = home / (config ? ".config" : ".local/share");
    }
    return base / "dw5e";
#endif
}
bool fail(const fs::path &path, const std::string &message, std::string &error)
{
    error = message + ": " + pathText(path);
    return false;
}
bool readJson(const fs::path &file, Json &json, std::string &error)
{
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || fs::file_size(file, ec) > 65536 || ec)
        return fail(file, "Cannot read installation metadata", error);
    try
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
            return fail(file, "Cannot open installation metadata", error);
        json = Json::parse(input);
        if (!json.is_object() || json.value("version", 0) != 1)
            return fail(file, "Invalid installation metadata version", error);
    }
    catch (...)
    {
        return fail(file, "Malformed installation metadata", error);
    }
    return true;
}
void setPaths(Installation &installation)
{
    if (installation.mode == SaveMode::Normal)
    {
        installation.userData = normalUserDataDirectory();
        installation.configDir = normalConfigDirectory();
    }
    else if (installation.mode == SaveMode::Portable)
    {
        installation.userData = installation.configDir = installation.root / "user-data";
    }
    else
    {
        installation.userData = installation.root;
        installation.configDir = installation.root / "build";
    }
}
fs::path resolvedPath(const fs::path &path)
{
    std::error_code ec;
    auto result = fs::weakly_canonical(path, ec);
    return (ec ? fs::absolute(path).lexically_normal() : result).make_preferred();
}
bool samePath(const fs::path &a, const fs::path &b)
{
    if (a.empty() || b.empty())
        return a.empty() && b.empty();
#ifdef _WIN32
    return _wcsicmp(resolvedPath(a).c_str(), resolvedPath(b).c_str()) == 0;
#else
    return resolvedPath(a) == resolvedPath(b);
#endif
}
bool sameBytes(const fs::path &a, const fs::path &b)
{
    std::error_code ec;
    if (!fs::is_regular_file(a, ec) || !fs::is_regular_file(b, ec) ||
        fs::file_size(a, ec) != fs::file_size(b, ec) || ec)
        return false;
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    char x[65536], y[65536];
    while (left && right)
    {
        left.read(x, sizeof(x));
        right.read(y, sizeof(y));
        if (left.gcount() != right.gcount() || !std::equal(x, x + left.gcount(), y))
            return false;
    }
    return left.eof() && right.eof();
}
bool hasRetailMarker(const fs::path &root)
{
    std::ifstream marker(root / "game/installed.txt");
    std::string line;
    for (int i = 0; i < 2 && std::getline(marker, line); ++i) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line == "sha1: 82fc63021a0919d8b97584682babf0654997c0a9")
            return true;
    }
    return false;
}
bool verified(const fs::path &root)
{
    return installState(root) == InstallState::Installed && hasRetailMarker(root);
}
struct Copy
{
    fs::path source, destination;
};
bool collect(const fs::path &source, const fs::path &destination, std::vector<Copy> &files,
             std::string &error)
{
    if (samePath(source, destination))
        return true;
    std::error_code ec;
    auto status = fs::symlink_status(source, ec);
    if (ec == std::errc::no_such_file_or_directory || !fs::exists(status))
        return true;
    if (ec)
        return fail(source, "Cannot inspect migration source", error);
    if (fs::is_symlink(status))
        return fail(source, "Migration refuses symbolic links", error);
    if (fs::is_regular_file(status))
    {
        files.push_back({source, destination});
        return true;
    }
    if (!fs::is_directory(status))
        return fail(source, "Unsupported migration source", error);
    const auto relative = resolvedPath(destination).lexically_relative(resolvedPath(source));
    if (!relative.empty() && *relative.begin() != "..")
        return fail(destination, "Migration destination is inside its source", error);
    for (fs::directory_iterator it(source, ec), end; it != end && !ec; it.increment(ec))
    {
        if (!collect(it->path(), destination / it->path().filename(), files, error))
            return false;
    }
    return !ec || fail(source, "Cannot enumerate migration source", error);
}
bool collectUserFiles(const Installation &source, const Installation &destination, std::vector<Copy> &files,
                      std::string &error)
{
    if (source.mode == SaveMode::Legacy)
    {
        std::error_code ec;
        for (fs::directory_iterator it(source.root, ec), end; it != end && !ec; it.increment(ec))
        {
            const auto name = pathText(it->path().filename());
            const bool profile =
                name.size() == 16 && name.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
            if (profile || name == "achievements" || name == "save" || name == "saves")
                if (!collect(it->path(), destination.userData / it->path().filename(), files, error))
                    return false;
        }
        if (ec)
            return fail(source.root, "Cannot enumerate legacy saves", error);
    }
    else if (!samePath(source.userData, destination.userData))
    {
        std::error_code ec;
        if (fs::exists(source.userData, ec))
        {
            for (fs::directory_iterator it(source.userData, ec), end; it != end && !ec; it.increment(ec))
            {
                if (it->path().filename() == "dw5e.toml")
                    continue;
                if (!collect(it->path(), destination.userData / it->path().filename(), files, error))
                    return false;
            }
            if (ec)
                return fail(source.userData, "Cannot enumerate user data", error);
        }
    }
    return collect(source.configDir / "dw5e.toml", destination.configDir / "dw5e.toml", files, error);
}
bool copyFiles(const std::vector<Copy> &files, SaveConflict policy, std::string &error)
{
    // Decide all conflicts before creating files. A source remains a recovery copy throughout.
    std::error_code ec;
    for (const auto &file : files)
    {
        if (fs::exists(file.destination, ec) && !sameBytes(file.source, file.destination))
        {
            if (!fs::is_regular_file(file.destination, ec))
                return fail(file.destination, "Migration destination is not a file", error);
            if (policy == SaveConflict::Ask)
                return fail(file.destination, "Save conflict: choose current saves or destination saves",
                            error);
        }
        if (ec)
            return fail(file.destination, "Cannot inspect migration destination", error);
    }
    static std::atomic<unsigned long> sequence{0};
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                        std::to_string(++sequence);
    for (const auto &file : files)
    {
        if (sameBytes(file.source, file.destination))
            continue;
        const bool exists = fs::exists(file.destination, ec);
        if (exists && policy == SaveConflict::KeepDestination)
            continue;
        fs::create_directories(file.destination.parent_path(), ec);
        if (ec)
            return fail(file.destination, "Cannot create migration destination", error);
        auto temporary = file.destination;
        temporary += ".dw5e-copy-" + suffix;
        if (fs::exists(temporary, ec))
            return fail(temporary, "Migration staging file already exists", error);
        fs::copy_file(file.source, temporary, fs::copy_options::none, ec);
        if (ec || !sameBytes(file.source, temporary))
        {
            fs::remove(temporary, ec);
            return fail(file.destination, "Save copy verification failed", error);
        }
        if (exists)
        {
            auto backup = file.destination;
            backup += ".dw5e-backup-" + suffix;
            if (fs::exists(backup, ec))
            {
                fs::remove(temporary, ec);
                return fail(backup, "Save backup already exists", error);
            }
            fs::copy_file(file.destination, backup, fs::copy_options::none, ec);
            if (ec || !sameBytes(file.destination, backup))
            {
                fs::remove(temporary, ec);
                return fail(backup, "Cannot preserve destination save", error);
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), file.destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return fail(file.destination, "Cannot commit copied save", error);
#else
        fs::rename(temporary, file.destination, ec);
        if (ec)
            return fail(file.destination, "Cannot commit copied save", error);
#endif
    }
    return true;
}
} // namespace
InstallationLock::~InstallationLock()
{
    if (handle_ == -1)
        return;
#ifdef _WIN32
    ReleaseMutex(reinterpret_cast<HANDLE>(handle_));
    CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
    flock(static_cast<int>(handle_), LOCK_UN);
    close(static_cast<int>(handle_));
#endif
}
bool InstallationLock::acquire(const fs::path &root, std::string &error)
{
#ifdef _WIN32
    auto name = resolvedPath(root).wstring();
    std::uint64_t hash = 14695981039346656037ULL;
    for (auto c : name)
    {
        hash ^= static_cast<std::uint32_t>(towlower(c));
        hash *= 1099511628211ULL;
    }
    const auto mutexName = L"Local\\DW5E-storage-" + std::to_wstring(hash);
    HANDLE handle = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!handle)
        return fail(root, "Cannot lock installation", error);
    const auto state = WaitForSingleObject(handle, 0);
    if (state != WAIT_OBJECT_0 && state != WAIT_ABANDONED)
    {
        CloseHandle(handle);
        return fail(root, "Another installation action is active; close the game or manager", error);
    }
    handle_ = reinterpret_cast<std::intptr_t>(handle);
#else
    const int handle = open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (handle < 0)
        return fail(root, "Cannot lock installation directory", error);
    if (flock(handle, LOCK_EX | LOCK_NB) != 0)
    {
        close(handle);
        return fail(root, "Another installation action is active; close the game or manager", error);
    }
    handle_ = handle;
#endif
    return true;
}
const char *saveModeName(SaveMode mode)
{
    return mode == SaveMode::Normal ? "normal" : mode == SaveMode::Portable ? "portable" : "legacy";
}
void configureSaveMode(Installation &installation, SaveMode mode)
{
    installation.mode = mode;
    setPaths(installation);
}
fs::path existingInstallationRoot(const fs::path &selected)
{
    if (verified(selected))
        return resolvedPath(selected);
    if (verified(selected / "DW5E-Recomp/game-files"))
        return resolvedPath(selected / "DW5E-Recomp/game-files");
    if (verified(selected / "game-files"))
        return resolvedPath(selected / "game-files");
    if (verified(selected / "DW5E"))
        return resolvedPath(selected / "DW5E");
    return {};
}
fs::path newInstallationRoot(const fs::path &selected)
{
    const auto root = resolvedPath(selected);
    const auto name = root.filename();
    std::error_code ec;
#ifdef _WIN32
    const bool dedicated = _wcsicmp(name.c_str(), L"DW5E") == 0 ||
                           _wcsicmp(name.c_str(), L"game-files") == 0;
    const bool branded = _wcsicmp(name.c_str(), L"DW5E-Recomp") == 0;
#else
    const bool dedicated = name == "DW5E" || name == "game-files";
    const bool branded = name == "DW5E-Recomp";
#endif
    if (dedicated || fs::is_regular_file(root / "install.json", ec))
        return root;
    return (branded ? root : root / "DW5E-Recomp") / "game-files";
}
fs::path normalUserDataDirectory()
{
    const auto root = normalBase(false);
    return root.empty() ? root : root / "user-data";
}
fs::path normalConfigDirectory()
{
    const auto root = normalBase(true);
    return root.empty() ? root : root / "config";
}
bool loadInstallation(const fs::path &root, Installation &installation, std::string &error)
{
    error.clear();
    installation = {};
    installation.root = root;
    std::error_code ec;
    const auto metadata = root / "install.json";
    if (fs::exists(metadata, ec))
    {
        Json json;
        if (!readJson(metadata, json, error))
            return false;
        const auto mode = json.find("save_mode");
        if (mode == json.end() || !mode->is_string())
            return fail(metadata, "Missing save mode", error);
        if (*mode == "normal")
            installation.mode = SaveMode::Normal;
        else if (*mode == "portable")
            installation.mode = SaveMode::Portable;
        else if (*mode == "legacy")
            installation.mode = SaveMode::Legacy;
        else
            return fail(metadata, "Unsupported save mode", error);
        installation.recorded = true;
    }
    else if (ec)
        return fail(metadata, "Cannot inspect installation metadata", error);
    else if (installState(root) == InstallState::Installed)
        installation.mode = SaveMode::Legacy;
    setPaths(installation);
    if (installation.recorded && (installation.userData.empty() || installation.configDir.empty()))
        return fail(root, "Cannot determine standard user directories", error);
    return true;
}
bool saveInstallation(const Installation &installation, std::string &error)
{
    return writeAtomicText(
        installation.root / "install.json",
        Json{{"version", 1}, {"save_mode", saveModeName(installation.mode)}}.dump(2) + "\n", error);
}
bool rememberNormalInstallation(const Installation &installation, std::string &error, bool makePreferred)
{
    if (installation.mode != SaveMode::Normal || installState(installation.root) != InstallState::Installed)
        return true;
    const auto configDirectory = normalConfigDirectory();
    if (configDirectory.empty())
        return fail(installation.root, "Cannot determine standard configuration directory", error);
    const auto file = configDirectory / "installations.json";
    std::error_code directoryError;
    fs::create_directories(configDirectory, directoryError);
    if (directoryError)
        return fail(configDirectory, "Cannot create installation index directory", error);
    InstallationLock indexLock;
    if (!indexLock.acquire(configDirectory, error))
        return false;
    Json json{{"version", 1}, {"installations", Json::array()}};
    std::error_code ec;
    if (fs::exists(file, ec) && !readJson(file, json, error))
        return false;
    auto &paths = json["installations"];
    if (!paths.is_array())
        return fail(file, "Invalid installation location list", error);
    std::string stored;
    for (const auto &path : paths) {
        if (path.is_string() && samePath(pathFromText(path.get<std::string>()), installation.root)) {
            stored = path.get<std::string>();
            break;
        }
    }
    if (!stored.empty()) {
        if (!makePreferred || (json.contains("preferred") && json["preferred"] == stored))
            return true;
    } else {
        if (paths.size() >= 128)
            return fail(file, "Installation location list is full", error);
        stored = pathText(resolvedPath(installation.root));
        paths.push_back(stored);
    }
    if (makePreferred)
        json["preferred"] = stored;
    return writeAtomicText(file, json.dump(2) + "\n", error);
}
bool prepareInstallation(Installation &installation, std::string &error)
{
    if (installation.userData.empty() || installation.configDir.empty())
        return fail(installation.root,
                    "Cannot determine standard user directories; select portable storage or restore the user "
                    "environment",
                    error);
    if (!validateInstallDirectory(installation.root, error) ||
        !validateInstallDirectory(installation.userData, error) ||
        !validateInstallDirectory(installation.configDir, error))
        return false;
    if (!rememberNormalInstallation(installation, error) || !saveInstallation(installation, error))
        return false;
    installation.recorded = true;
    return true;
}
bool discoverInstallations(const fs::path &launcher, std::vector<fs::path> &found, std::string &error,
                           fs::path *preferred)
{
    found.clear();
    error.clear();
    if (preferred)
        preferred->clear();
    auto add = [&](const fs::path &root) {
        if (!verified(root))
            return;
        for (const auto &existing : found)
            if (samePath(existing, root))
                return;
        found.push_back(resolvedPath(root));
    };
    add(launcher / "game-files");
    if (!found.empty())
        return true;
    add(launcher / "DW5E-Recomp/game-files");
    if (!found.empty())
        return true;
    add(launcher);
    if (!found.empty())
        return true;
    add(launcher / "DW5E");
    if (!found.empty())
        return true;
    const auto configDirectory = normalConfigDirectory();
    const auto file = configDirectory.empty() ? fs::path{} : configDirectory / "installations.json";
    std::error_code ec;
    if (!file.empty() && fs::exists(file, ec))
    {
        Json json;
        if (!readJson(file, json, error))
            return false;
        const auto paths = json.find("installations");
        if (paths == json.end() || !paths->is_array() || paths->size() > 128)
            return fail(file, "Invalid installation location list", error);
        const auto savedPreference = json.find("preferred");
        for (const auto &path : *paths)
        {
            if (!path.is_string())
                return fail(file, "Invalid installation location", error);
            const auto text = path.get<std::string>();
            const auto root = pathFromText(text);
            if (text.empty() || text.find('\0') != std::string::npos || !root.is_absolute())
                return fail(file, "Installation locations must be absolute paths", error);
            add(root);
            if (preferred && savedPreference != json.end() && *savedPreference == path) {
                Installation retained;
                std::string metadataError;
                const bool readyToReinstall = fs::is_directory(root, ec) &&
                    installState(root) == InstallState::Missing &&
                    loadInstallation(root, retained, metadataError) &&
                    retained.recorded && retained.mode == SaveMode::Normal;
                if (verified(root) || readyToReinstall)
                    *preferred = resolvedPath(root);
                ec.clear();
            }
        }
    }
    add(normalBase(false)); // Read-only recognition of pre-010 installations.
    // Bounded neighbourhood only: no whole-drive scan and no dependence on cwd.
    size_t inspected = 0;
    for (const auto &parent : {launcher.parent_path(), launcher.parent_path().parent_path()})
    {
        if (parent.empty() || parent == parent.root_path())
            continue;
        for (fs::directory_iterator it(parent, fs::directory_options::skip_permission_denied, ec), end;
             it != end && !ec && inspected < 256; it.increment(ec))
        {
            if (!it->is_directory(ec) || it->is_symlink(ec))
                continue;
            ++inspected;
            add(it->path() / "DW5E-Recomp/game-files");
            add(it->path() / "game-files");
            add(it->path() / "DW5E");
            add(it->path());
            const auto name = pathText(it->path().filename());
            if (name.starts_with("DW5E") || name.starts_with("dw5e"))
            {
                std::error_code innerError;
                for (fs::directory_iterator
                         child(it->path(), fs::directory_options::skip_permission_denied, innerError),
                     finish;
                     child != finish && !innerError && inspected < 256; child.increment(innerError))
                {
                    if (!child->is_directory(innerError) || child->is_symlink(innerError))
                        continue;
                    ++inspected;
                    add(child->path() / "DW5E-Recomp/game-files");
                    add(child->path() / "game-files");
                    add(child->path());
                }
            }
        }
        ec.clear();
    }
    return true;
}
bool installationGameRunning(const fs::path &root, std::string &error)
{
    error.clear();
#ifdef _WIN32
    const auto expected = resolvedPath(root / "build/dw5e.exe");
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        error = "Cannot check whether the game is running";
        return true;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
    {
        if (_wcsicmp(entry.szExeFile, L"dw5e.exe") != 0 || entry.th32ProcessID == GetCurrentProcessId())
            continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process)
        {
            running = true;
            break;
        }
        wchar_t path[32768];
        DWORD length = 32768;
        if (QueryFullProcessImageNameW(process, 0, path, &length) && samePath(fs::path(path), expected))
            running = true;
        CloseHandle(process);
        if (running)
            break;
    }
    CloseHandle(snapshot);
#else
    const auto expected = resolvedPath(root / "build/dw5e");
    bool running = false;
    std::error_code ec;
    for (fs::directory_iterator it("/proc", fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec))
    {
        const auto name = pathText(it->path().filename());
        if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos)
            continue;
        std::error_code linkError;
        auto image = fs::read_symlink(it->path() / "exe", linkError);
        if (!linkError && samePath(image, expected))
        {
            running = true;
            break;
        }
    }
    if (ec)
    {
        error = "Cannot check whether the game is running";
        return true;
    }
#endif
    if (running)
        error = "Close the game before changing saves or importing this installation";
    return running;
}
bool switchSaveMode(Installation &installation, SaveMode mode, SaveConflict conflict, std::string &error)
{
    if (mode == SaveMode::Legacy)
        return fail(installation.root, "Legacy mode cannot be selected", error);
    InstallationLock lock;
    if (!lock.acquire(installation.root, error) || installationGameRunning(installation.root, error))
        return false;
    Installation destination = installation;
    destination.mode = mode;
    setPaths(destination);
    if (destination.userData.empty() || destination.configDir.empty())
        return fail(destination.root, "Standard user directories unavailable", error);
    if (mode == installation.mode)
        return true;
    std::vector<Copy> files;
    if (!collectUserFiles(installation, destination, files, error) || !copyFiles(files, conflict, error))
        return false;
    if (!prepareInstallation(destination, error))
        return false;
    installation = destination;
    return true;
}
bool importInstallation(const fs::path &sourceRoot, Installation &destination, SaveConflict conflict,
                        std::string &error)
{
    if (samePath(sourceRoot, destination.root))
        return true;
    if (!verified(sourceRoot))
        return fail(sourceRoot, "Not a verified DW5E installation", error);
    if (!validateInstallDirectory(destination.root, error))
        return false;
    InstallationLock sourceLock, destinationLock;
    if (!sourceLock.acquire(sourceRoot, error) || !destinationLock.acquire(destination.root, error) ||
        installationGameRunning(sourceRoot, error) || installationGameRunning(destination.root, error))
        return false;
    Installation source;
    if (!loadInstallation(sourceRoot, source, error))
        return false;
    std::vector<Copy> files;
    for (const char *name : {"game", "generated", "build", "cache", "logs"})
        if (!collect(sourceRoot / name, destination.root / name, files, error))
            return false;
    if (!collectUserFiles(source, destination, files, error) || !copyFiles(files, conflict, error))
        return false;
    return prepareInstallation(destination, error);
}

bool canUninstallGameFiles(const fs::path &root)
{
    return root.is_absolute() && root != root.root_path() && hasRetailMarker(root);
}

bool uninstallGameFiles(const fs::path &selected, const fs::path &launcher, bool rememberForReinstall,
                        std::string &error)
{
    if (!canUninstallGameFiles(selected))
        return fail(selected, "Cannot identify this folder as a DW5E installation", error);
    const auto root = resolvedPath(selected);
    if (root == root.root_path())
        return fail(root, "Cannot uninstall from the filesystem root", error);
    InstallationLock lock;
    if (!lock.acquire(root, error) || installationGameRunning(root, error))
        return false;
    if (!hasRetailMarker(root))
        return fail(root, "The installation changed before removal", error);

    const auto directoryIsSafe = [&](const fs::path &directory) {
        std::error_code ec;
        const auto status = fs::symlink_status(directory, ec);
        if (ec == std::errc::no_such_file_or_directory)
            return true;
        if (ec || fs::is_symlink(status) || !fs::is_directory(status) ||
            !samePath(directory.parent_path(), resolvedPath(directory).parent_path()))
            return fail(directory, "Cannot safely remove this directory", error);
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(directory.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return fail(directory, "Cannot remove a redirected directory", error);
#endif
        return true;
    };
    const auto executableDir = executableDirectory(error);
    if (executableDir.empty())
        return false;
    const auto appDirectory = resolvedPath(executableDir);
    const std::vector<fs::path> targets{root / "build", root / "generated", root / "builder",
                                        root / "game/files"};
    if (!directoryIsSafe(root / "game"))
        return false;
    for (const auto &target : targets) {
        if (!directoryIsSafe(target))
            return false;
        const auto relative = appDirectory.lexically_relative(resolvedPath(target));
        if (!relative.empty() && *relative.begin() != "..")
            return fail(target, "Cannot remove files containing the running launcher", error);
    }

    Installation installation;
    if (!loadInstallation(root, installation, error))
        return false;
    if (rememberForReinstall) {
        if (installation.mode == SaveMode::Normal && !rememberNormalInstallation(installation, error, true))
            return false;
        if (installation.mode == SaveMode::Legacy && !rememberInstallLocation(launcher, root, error))
            return false;
    }
    // Persist legacy mode before removing the marker that originally implied it.
    if (!installation.recorded && !saveInstallation(installation, error))
        return false;

    const auto removeTree = [&](const fs::path &path) {
        std::error_code ec;
#ifdef _WIN32
        const auto isReparsePoint = [](const fs::path &entry) {
            const DWORD attributes = GetFileAttributesW(entry.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
        };
        if (isReparsePoint(path))
            return fail(path, "Cannot remove a redirected path", error);
        if (fs::is_directory(path, ec)) {
            for (fs::recursive_directory_iterator it(path, ec), end; it != end && !ec; it.increment(ec))
                if (isReparsePoint(it->path()))
                    return fail(it->path(), "Cannot remove a redirected path", error);
            if (ec)
                return fail(path, "Cannot inspect files for removal: " + ec.message(), error);
        }
        ec.clear();
#endif
        fs::remove_all(path, ec); // Does not follow symbolic links.
        return !ec || fail(path, "Cannot remove game files: " + ec.message(), error);
    };
    for (const auto &target : targets) {
        if (target == root / "build") {
            std::error_code ec;
            std::vector<fs::path> entries;
            for (fs::directory_iterator it(target, ec), end; it != end && !ec; it.increment(ec))
                if (it->path().filename() != "dw5e.toml")
                    entries.push_back(it->path());
            if (ec && ec != std::errc::no_such_file_or_directory)
                return fail(target, "Cannot inspect the old build", error);
            for (const auto &entry : entries)
                if (!removeTree(entry))
                    return false;
        } else if (!removeTree(target)) {
            return false;
        }
    }
    std::error_code ec;
    const auto marker = root / "game/installed.txt";
    fs::remove(marker, ec);
    return !ec || fail(marker, "Cannot remove the installation marker", error);
}
} // namespace dw5e::setup
