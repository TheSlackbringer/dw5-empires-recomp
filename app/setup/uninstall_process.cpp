#include "uninstall_process.h"
#include "installation.h"
#include "install_location.h"
#include "run_process.h"

#include <chrono>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

namespace dw5e::setup {
namespace fs = std::filesystem;

bool scheduleUninstall(const fs::path &package, const fs::path &root, bool keepSaves,
                       fs::path &failureLog, std::string &error)
{
    if (!canUninstallGameFiles(root) || installationGameRunning(root, error)) {
        if (error.empty())
            error = "The selected folder is not a verified DW5E installation.";
        return false;
    }
    Installation installation;
    bool shared = false;
    if (!loadInstallation(root, installation, error) || !uninstallSavesShared(installation, shared, error))
        return false;
    if (!keepSaves && shared) {
        error = "Another DW5E installation shares these saves.";
        return false;
    }

    std::error_code ec;
#ifdef _WIN32
    const auto executable = package / "launcher/dw5e-uninstall.exe";
    const auto wizardPid = GetCurrentProcessId();
    DWORD parentPid = 0;
    HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE) {
        error = "Cannot identify the Windows launcher process.";
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(processes, &entry); ok; ok = Process32NextW(processes, &entry))
        if (entry.th32ProcessID == wizardPid) {
            parentPid = entry.th32ParentProcessID;
            break;
        }
    CloseHandle(processes);
    const auto outerLauncher = package.parent_path() / "DW5E.exe";
#else
    const auto executable = package / "usr/bin/dw5e-uninstall";
    const auto wizardPid = getpid();
    const auto parentPid = getppid();
    const char *appImage = std::getenv("APPIMAGE");
    const auto outerLauncher = appImage ? pathFromText(appImage) : fs::path{};
#endif
    if (!fs::is_regular_file(executable, ec)) {
        error = "The uninstall helper is missing. Open a current DW5E release to uninstall this copy.";
        return false;
    }
    const auto temp = fs::temp_directory_path(ec);
    if (ec) {
        error = "Cannot locate a temporary folder for uninstall: " + ec.message();
        return false;
    }
    const auto work = temp / ("dw5e-uninstall-" + std::to_string(wizardPid) + "-" +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto relative = fs::relative(work, uninstallFolder(root), ec);
    if (ec || (!relative.empty() && *relative.begin() != "..")) {
        error = "The uninstall working folder must be outside the installation.";
        return false;
    }
    if (!fs::create_directory(work, ec)) {
        error = "Cannot create the uninstall working folder: " + pathText(work);
        return false;
    }
#ifndef _WIN32
    fs::permissions(work, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) {
        error = "Cannot protect the uninstall working folder: " + ec.message();
        fs::remove(work, ec);
        return false;
    }
#endif
    const auto staged = work / executable.filename();
    failureLog = work / "uninstall.log";
    fs::copy_file(executable, staged, fs::copy_options::none, ec);
    if (!ec && launchDetached(staged, {pathText(root), keepSaves ? "keep" : "delete",
                                     std::to_string(wizardPid), std::to_string(parentPid),
                                     pathText(outerLauncher), pathText(failureLog)}, work, error))
        return true;
    if (ec)
        error = "Cannot stage the uninstall helper: " + ec.message();
    fs::remove(staged, ec);
    fs::remove(work, ec);
    return false;
}
} // namespace dw5e::setup
