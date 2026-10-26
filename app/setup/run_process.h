#pragma once
// Process launcher for setup wizard and release builder.
// Executes subprocesses (CMake, Ninja, ReXGlue CLI, and the built game) without shell interference.
// Accurately handles arguments with spaces/unicode, output logging, and process cancellation.

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace dw5e::setup {

struct ProcessResult
{
    bool started = false;
    int exitCode = -1;
    bool cancelled = false;
    std::string error;
};

// Runs program with args synchronously, appending stdout/stderr to logFile.
// If cancel is set, terminates the child process and its tree immediately.
ProcessResult runProcess(const std::filesystem::path &program,
                         const std::vector<std::string> &args,
                         const std::filesystem::path &workingDirectory,
                         const std::filesystem::path &logFile,
                         const std::atomic<bool> *cancel = nullptr,
                         const std::vector<std::pair<std::string, std::string>> &extraEnv = {});

// Starts program and returns immediately (used to boot the game from the launcher).
bool launchDetached(const std::filesystem::path &program,
                    const std::vector<std::string> &args,
                    const std::filesystem::path &workingDirectory,
                    std::string &error);

} // namespace dw5e::setup
