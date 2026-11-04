#include "installation.h"
#include "install_location.h"

#include <charconv>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace dw5e::setup;

namespace {
bool waitForProcess(unsigned long pid, const fs::path &expected = {})
{
    if (!pid)
        return true;
#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return GetLastError() == ERROR_INVALID_PARAMETER;
    if (!expected.empty()) {
        std::wstring image(32768, L'\0');
        DWORD length = static_cast<DWORD>(image.size());
        const bool queried = QueryFullProcessImageNameW(process, 0, image.data(), &length);
        image.resize(length);
        std::error_code ec;
        if (!queried || !fs::equivalent(expected, fs::path(image), ec)) {
            CloseHandle(process);
            return queried || ec == std::errc::no_such_file_or_directory;
        }
    }
    const bool exited = WaitForSingleObject(process, INFINITE) == WAIT_OBJECT_0;
    CloseHandle(process);
    return exited;
#else
    if (!expected.empty()) {
        std::error_code ec;
        if (!fs::equivalent(expected, fs::path("/proc") / std::to_string(pid) / "exe", ec))
            return !ec || ec == std::errc::no_such_file_or_directory ||
                   ec == std::errc::permission_denied;
    }
    const int handle = static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
    if (handle < 0)
        return errno == ESRCH;
    pollfd descriptor{handle, POLLIN, 0};
    int ready;
    do {
        ready = poll(&descriptor, 1, -1);
    } while (ready < 0 && errno == EINTR);
    close(handle);
    return ready == 1 && (descriptor.revents & (POLLIN | POLLHUP));
#endif
}
}

static int uninstallMain(int argc, char **argv)
{
    const auto args = nativeArguments(argc, argv);
    if (args.size() != 7 || (args[2] != "keep" && args[2] != "delete"))
        return 2;
    unsigned long wizard = 0, parent = 0;
    const auto parsePid = [](const std::string &text, unsigned long &pid) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), pid);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    };
    if (!parsePid(args[3], wizard) || !parsePid(args[4], parent))
        return 2;
    const auto failureLog = pathFromText(args[6]);
    std::string error;
    if (!waitForProcess(wizard) || (!args[5].empty() && !waitForProcess(parent, pathFromText(args[5]))))
        error = "Could not wait for the launcher to close. DW5E was not removed.";
    else
        uninstallInstallation(pathFromText(args[1]), args[2] == "keep", error);
    if (!error.empty()) {
        std::ofstream log(failureLog, std::ios::binary);
        log << error << '\n';
#ifdef _WIN32
        const auto message = pathFromText(error + "\n\nDetails: " + pathText(failureLog)).wstring();
        MessageBoxW(nullptr, message.c_str(), L"DW5E uninstall could not finish", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
#ifndef _WIN32
    // Linux can unlink the helper while it is executing.
    std::error_code ec;
    fs::remove(pathFromText(args[0]), ec);
    fs::remove(failureLog.parent_path(), ec);
#endif
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return uninstallMain(0, nullptr);
}
#else
int main(int argc, char **argv)
{
    return uninstallMain(argc, argv);
}
#endif
