#include "run_process.h"

#include <chrono>
#include <fstream>
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
        result.error = "cannot open log file " + logFile.string();
        return result;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log;
    si.hStdError = log;

    PROCESS_INFORMATION pi{};
    const std::wstring dir = workingDirectory.wstring();

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        dir.empty() ? nullptr : dir.c_str(), &si, &pi))
    {
        CloseHandle(log);
        if (job)
            CloseHandle(job);
        result.error = "failed to start " + program.string();
        return result;
    }

    if (job)
        AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(log);

    result.started = true;
    while (true)
    {
        DWORD wait = WaitForSingleObject(pi.hProcess, 100);
        if (wait == WAIT_OBJECT_0)
        {
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            result.exitCode = static_cast<int>(code);
            break;
        }
        if (cancel && cancel->load())
        {
            result.cancelled = true;
            if (job)
                TerminateJobObject(job, 1);
            else
                TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            break;
        }
    }

    CloseHandle(pi.hProcess);
    if (job)
        CloseHandle(job);
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
        error = "cannot launch " + program.string();
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
    int logFd = open(logFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logFd < 0)
    {
        result.error = "cannot open log file " + logFile.string() + ": " + std::strerror(errno);
        return result;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        close(logFd);
        result.error = "fork failed: " + std::string(std::strerror(errno));
        return result;
    }

    if (pid == 0)
    {
        // Child process
        setpgid(0, 0); // Own process group for clean subtree termination
        if (!workingDirectory.empty())
        {
            if (chdir(workingDirectory.c_str()) != 0)
                _exit(127);
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
        const char *err = std::strerror(errno);
        (void)!write(STDERR_FILENO, "execvp error: ", 14);
        (void)!write(STDERR_FILENO, err, std::strlen(err));
        (void)!write(STDERR_FILENO, "\n", 1);
        _exit(127);
    }

    close(logFd);
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
