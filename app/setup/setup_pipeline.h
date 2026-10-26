#pragma once
// Setup wizard pipeline orchestrator.
// Coordinates: Disc Check -> XDVDFS Extraction -> ReXGlue Codegen -> CMake/Ninja Compilation -> Game Launch.
// Provides a unified weighted progress bar (commit 52e0735) and background execution.

#include "build_game.h"
#include "setup_layout.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dw5e::setup {

enum class Step
{
    Check,    // Validate retail XGD2 disc and calculate SHA-1
    Extract,  // Extract default.xex and game files via XDVDFS
    Generate, // Run ReXGlue codegen to produce C++
    Build,    // Compile native game binary via CMake & Ninja
    Done
};

const char *stepName(Step step);

// Calculates overall weighted progress (0.0 to 1.0) based on active steps in plan.
double overallProgress(const std::vector<Step> &plan, Step current, double fraction);

struct SetupOptions
{
    std::filesystem::path root;
    std::filesystem::path discIso;
    std::filesystem::path recompiler;
    BuildTools tools;
    std::filesystem::path buildDir;
    std::filesystem::path logDir;
    std::filesystem::path dataDir;
    bool rebuild = false;

    std::filesystem::path effectiveDataDir() const { return dataDir.empty() ? root : dataDir; }
    std::filesystem::path effectiveBuildDir() const
    {
        return !buildDir.empty() ? buildDir : (dataDir.empty() ? root / "app" / "build" : dataDir / "build");
    }
    std::filesystem::path effectiveLogDir() const
    {
        return !logDir.empty() ? logDir : (dataDir.empty() ? root / "logs" : dataDir / "logs");
    }
};

struct SetupStatus
{
    Step step = Step::Check;
    double fraction = 0.0;
    double overall = 0.0;
    std::vector<Step> plan;
    std::string detail;
    bool running = false;
    bool finished = false;
    bool failed = false;
    bool cancelled = false;
    std::string error;
    std::filesystem::path executable;
    std::vector<std::string> log;
};

class SetupRun
{
public:
    explicit SetupRun(SetupOptions options);
    ~SetupRun();

    SetupRun(const SetupRun &) = delete;
    SetupRun &operator=(const SetupRun &) = delete;

    void start();
    void cancel() { m_cancel = true; }
    bool wait();
    SetupStatus status() const;

private:
    void run();
    void setStep(Step s, double fraction, const std::string &detail);
    void appendLog(const std::string &line);
    bool fail(const std::string &err);

    SetupOptions m_options;
    std::atomic<bool> m_cancel{false};
    mutable std::mutex m_mutex;
    SetupStatus m_status;
    std::thread m_thread;
};

} // namespace dw5e::setup
