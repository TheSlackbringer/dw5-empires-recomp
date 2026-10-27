#include "run_process.h"

#include <chrono>
#include <fstream>
#include <map>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace dw5e::setup {

namespace {
void logError(const std::filesystem::path &log, const std::string &error)
{
    std::ofstream(log, std::ios::app) << error << '\n';
}
} // namespace

#ifdef _WIN32

namespace {
std::wstring widen(const std::string &utf8)
{
    if (utf8.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string windowsError(DWORD code)
{
    return "Windows error " + std::to_string(code) + ": " +
           std::system_category().message(static_cast<int>(code));
}

struct EnvironmentLess {
    bool operator()(const std::wstring &a, const std::wstring &b) const
    {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    }
};

std::vector<wchar_t> childEnvironment(
    const std::vector<std::pair<std::string, std::string>> &extra)
{
    std::map<std::wstring, std::wstring, EnvironmentLess> values;
    if (wchar_t *block = GetEnvironmentStringsW())
    {
        for (const wchar_t *entry = block; *entry; entry += wcslen(entry) + 1)
        {
            const std::wstring value(entry);
            const auto split = value.find(L'=', value[0] == L'=' ? 1 : 0);
            if (split != std::wstring::npos)
                values[value.substr(0, split)] = value.substr(split + 1);
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto &[key, value] : extra)
        values[widen(key)] = widen(value);
    std::vector<wchar_t> result;
    for (const auto &[key, value] : values)
    {
        const auto entry = key + L"=" + value;
        result.insert(result.end(), entry.begin(), entry.end());
        result.push_back(L'\0');
    }
    result.push_back(L'\0');
    return result;
}

void appendQuoted(std::wstring &cmd, const std::wstring &arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
    {
        cmd += arg;
        return;
    }
    cmd += L'"';
    for (auto it = arg.begin();; ++it)
    {
        size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\')
        {
            ++it;
            ++backslashes;
        }
        if (it == arg.end())
        {
            cmd.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"')
        {
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd += L'"';
        }
        else
        {
            cmd.append(backslashes, L'\\');
            cmd += *it;
        }
    }
    cmd += L'"';
}
} // namespace

ProcessResult runProcess(const std::filesystem::path &program,
                         const std::vector<std::string> &args,
                         const std::filesystem::path &workingDirectory,
                         const std::filesystem::path &logFile,
                         const std::atomic<bool> *cancel,
                         const std::vector<std::pair<std::string, std::string>> &extraEnv)
{
    ProcessResult result;
    if (cancel && cancel->load())
    {
        result.cancelled = true;
        return result;
    }
    std::wstring cmd;
    appendQuoted(cmd, program.wstring());
    for (const auto &arg : args)
    {
        cmd += L' ';
        appendQuoted(cmd, widen(arg));
    }

    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE log = CreateFileW(logFile.wstring().c_str(), FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE)
    {
        result.error = "cannot open log file " + logFile.string() + ": " + windowsError(GetLastError());
        return result;
    }
    // A GUI launcher may not have valid inherited standard input.
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = input;
    si.hStdOutput = log;
    si.hStdError = log;
    PROCESS_INFORMATION pi{};
    const std::wstring dir = workingDirectory.wstring();

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
    {
        result.error = "cannot create subprocess job: " + windowsError(GetLastError());
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        CloseHandle(log);
        if (job) CloseHandle(job);
        logError(logFile, result.error);
        return result;
    }
    auto environment = extraEnv.empty() ? std::vector<wchar_t>() : childEnvironment(extraEnv);
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                        environment.empty() ? nullptr : environment.data(),
                        dir.empty() ? nullptr : dir.c_str(), &si, &pi))
    {
        result.error = "failed to start " + program.string() + ": " + windowsError(GetLastError());
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        CloseHandle(log);
        CloseHandle(job);
        logError(logFile, result.error);
        return result;
    }

    const bool assigned = AssignProcessToJobObject(job, pi.hProcess);
    if (!assigned)
    {
        result.error = "cannot manage subprocess " + program.string() + ": " + windowsError(GetLastError());
        TerminateProcess(pi.hProcess, 1);
    }
    else if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1))
    {
        result.error = "cannot resume subprocess " + program.string() + ": " + windowsError(GetLastError());
        TerminateJobObject(job, 1);
    }
    else
        result.started = true;
    CloseHandle(pi.hThread);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    CloseHandle(log);

    while (true)
    {
        DWORD wait = WaitForSingleObject(pi.hProcess, 100);
        if (wait == WAIT_OBJECT_0)
        {
            DWORD code = 0;
            if (GetExitCodeProcess(pi.hProcess, &code))
                result.exitCode = static_cast<int>(code);
            else
                result.error = "cannot read subprocess result: " + windowsError(GetLastError());
            break;
        }
        if (wait == WAIT_FAILED)
        {
            result.error = "cannot wait for subprocess: " + windowsError(GetLastError());
            TerminateJobObject(job, 1);
            break;
        }
        if (cancel && cancel->load())
        {
            result.cancelled = true;
            TerminateJobObject(job, 1);
            WaitForSingleObject(pi.hProcess, INFINITE);
            break;
        }
    }
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    if (!result.error.empty())
        logError(logFile, result.error);
    return result;
}

bool launchDetached(const std::filesystem::path &program,
                    const std::vector<std::string> &args,
                    const std::filesystem::path &workingDirectory,
                    std::string &error)
{
    std::wstring cmd;
    appendQuoted(cmd, program.wstring());
    for (const auto &arg : args)
    {
        cmd += L' ';
        appendQuoted(cmd, widen(arg));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring dir = workingDirectory.wstring();

    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_DEFAULT_ERROR_MODE, nullptr,
                        dir.empty() ? nullptr : dir.c_str(), &si, &pi))
    {
        error = "cannot launch " + program.string() + ": " + windowsError(GetLastError());
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

#else

ProcessResult runProcess(const std::filesystem::path &program,
                         const std::vector<std::string> &args,
                         const std::filesystem::path &workingDirectory,
                         const std::filesystem::path &logFile,
                         const std::atomic<bool> *cancel,
                         const std::vector<std::pair<std::string, std::string>> &extraEnv)
{
    ProcessResult result;
    if (cancel && cancel->load())
    {
        result.cancelled = true;
        return result;
    }
    int logFd = open(logFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logFd < 0)
    {
        result.error = "cannot open log file " + logFile.string() + ": " + std::strerror(errno);
        return result;
    }

    int launchPipe[2];
    if (pipe(launchPipe) != 0)
    {
        result.error = "cannot create subprocess error pipe: " + std::string(std::strerror(errno));
        close(logFd);
        logError(logFile, result.error);
        return result;
    }
    if (fcntl(launchPipe[1], F_SETFD, FD_CLOEXEC) < 0)
    {
        result.error = "cannot prepare subprocess error pipe: " + std::string(std::strerror(errno));
        close(launchPipe[0]);
        close(launchPipe[1]);
        close(logFd);
        logError(logFile, result.error);
        return result;
    }
    pid_t pid = fork();
    if (pid < 0)
    {
        const int code = errno;
        close(logFd);
        close(launchPipe[0]);
        close(launchPipe[1]);
        result.error = "fork failed: " + std::string(std::strerror(code));
        logError(logFile, result.error);
        return result;
    }

    if (pid == 0)
    {
        // Successful exec closes the pipe; failed chdir/exec reports errno.
        close(launchPipe[0]);
        const auto launchFailed = [&](int code) {
            (void)!write(launchPipe[1], &code, sizeof(code));
            _exit(127);
        };
        setpgid(0, 0); // Own process group for clean subtree termination
        if (!workingDirectory.empty())
        {
            if (chdir(workingDirectory.c_str()) != 0)
                launchFailed(errno);
        }

        for (const auto &[k, v] : extraEnv)
            setenv(k.c_str(), v.c_str(), 1);

        dup2(logFd, STDOUT_FILENO);
        dup2(logFd, STDERR_FILENO);
        close(logFd);

        std::vector<const char *> argv;
        std::string progStr = program.string();
        argv.push_back(progStr.c_str());
        for (const auto &arg : args)
            argv.push_back(arg.c_str());
        argv.push_back(nullptr);

        execvp(progStr.c_str(), const_cast<char *const *>(argv.data()));
        launchFailed(errno);
    }

    close(logFd);
    close(launchPipe[1]);
    int launchCode = 0;
    ssize_t received;
    do {
        received = read(launchPipe[0], &launchCode, sizeof(launchCode));
    } while (received < 0 && errno == EINTR);
    close(launchPipe[0]);
    if (received != 0)
    {
        result.error = "failed to start " + program.string() + ": " +
                       std::string(std::strerror(received > 0 ? launchCode : errno));
        if (received < 0)
            kill(pid, SIGKILL);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        logError(logFile, result.error);
        return result;
    }
    result.started = true;

    while (true)
    {
        int status = 0;
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid)
        {
            if (WIFEXITED(status))
                result.exitCode = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                result.exitCode = 128 + WTERMSIG(status);
            break;
        }
        else if (waited < 0)
        {
            if (errno == EINTR)
                continue;
            result.error = "waitpid error: " + std::string(std::strerror(errno));
            break;
        }

        if (cancel && cancel->load())
        {
            result.cancelled = true;
            kill(-pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            kill(-pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return result;
}

bool launchDetached(const std::filesystem::path &program,
                    const std::vector<std::string> &args,
                    const std::filesystem::path &workingDirectory,
                    std::string &error)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        error = "fork failed: " + std::string(std::strerror(errno));
        return false;
    }

    if (pid == 0)
    {
        setsid();
        if (!workingDirectory.empty())
        {
            if (chdir(workingDirectory.c_str()) != 0)
                _exit(127);
        }

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0)
        {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        std::vector<const char *> argv;
        std::string progStr = program.string();
        argv.push_back(progStr.c_str());
        for (const auto &arg : args)
            argv.push_back(arg.c_str());
        argv.push_back(nullptr);

        execvp(progStr.c_str(), const_cast<char *const *>(argv.data()));
        _exit(127);
    }

    return true;
}

#endif

} // namespace dw5e::setup
