#include "setup_pipeline.h"
#include "codegen.h"
#include "install_state.h"
#include "xdvdfs.h"

#include <algorithm>
#include <fstream>
#include <numeric>

namespace dw5e::setup {

namespace fs = std::filesystem;

const char *stepName(Step step)
{
    switch (step)
    {
    case Step::Check:
        return "Validating disc";
    case Step::Extract:
        return "Extracting game files";
    case Step::Generate:
        return "Translating game code";
    case Step::Build:
        return "Compiling native executable";
    case Step::Done:
        return "Ready";
    }
    return "";
}

namespace {
double stepWeight(Step step)
{
    switch (step)
    {
    case Step::Check:
        return 0.05;
    case Step::Extract:
        return 0.15;
    case Step::Generate:
        return 0.15;
    case Step::Build:
        return 0.65;
    case Step::Done:
        return 0.0;
    }
    return 0.0;
}
} // namespace

double overallProgress(const std::vector<Step> &plan, Step current, double fraction)
{
    if (plan.empty() || current == Step::Done)
        return 1.0;

    double totalWeight = 0.0;
    for (Step s : plan)
        totalWeight += stepWeight(s);
    if (totalWeight <= 0.0)
        return fraction;

    double completedWeight = 0.0;
    for (Step s : plan)
    {
        if (s == current)
        {
            completedWeight += stepWeight(s) * std::clamp(fraction, 0.0, 1.0);
            break;
        }
        completedWeight += stepWeight(s);
    }

    return std::clamp(completedWeight / totalWeight, 0.0, 1.0);
}

SetupRun::SetupRun(SetupOptions options)
    : m_options(std::move(options))
{
}

SetupRun::~SetupRun()
{
    cancel();
    if (m_thread.joinable())
        m_thread.join();
}

void SetupRun::start()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.running = true;
    }
    m_thread = std::thread([this] {
        try { run(); }
        catch (const std::exception &e) { fail(std::string("Setup failed: ") + e.what()); }
    });
}

bool SetupRun::wait()
{
    if (m_thread.joinable())
        m_thread.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status.finished && !m_status.failed;
}

SetupStatus SetupRun::status() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void SetupRun::setStep(Step s, double fraction, const std::string &detail)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.step = s;
    m_status.fraction = fraction;
    m_status.overall = overallProgress(m_status.plan, s, fraction);
    m_status.detail = detail;
}

void SetupRun::appendLog(const std::string &line)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.log.push_back(line);
    if (m_status.log.size() > 50)
        m_status.log.erase(m_status.log.begin());
}

bool SetupRun::fail(const std::string &err)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.running = false;
    m_status.failed = true;
    m_status.error = err;
    return false;
}

void SetupRun::run()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.running = true;
        m_status.failed = false;
        m_status.finished = false;
        m_status.cancelled = false;
    }

    const fs::path dataDir = m_options.effectiveDataDir();
    const fs::path buildDir = m_options.effectiveBuildDir();
    const fs::path logDir = m_options.effectiveLogDir();

    std::error_code ec;
    fs::create_directories(dataDir, ec);
    fs::create_directories(logDir, ec);

    // Determine plan
    std::string problem;
    const bool isInstalled = (installState(dataDir, &problem) == InstallState::Installed);
    std::vector<Step> plan;

    if (isInstalled && m_options.discIso.empty())
    {
        plan = {Step::Generate, Step::Build};
    }
    else
    {
        plan = {Step::Check, Step::Extract, Step::Generate, Step::Build};
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.plan = plan;
    }

    // Step: Check
    if (std::find(plan.begin(), plan.end(), Step::Check) != plan.end())
    {
        setStep(Step::Check, 0.05, "Inspecting disc structure...");
        uint64_t partitionBase = 0;
        std::string err;
        if (!checkDiscImage(m_options.discIso, partitionBase, err))
        {
            fail("Disc check failed: " + err);
            return;
        }

        setStep(Step::Check, 0.20, "Verifying retail European PAL checksum...");
        std::string discSha1;
        bool hashOk = hashDiscSha1(m_options.discIso,
                                   [this](double frac) {
                                       setStep(Step::Check, 0.20 + frac * 0.75, "Hashing disc image...");
                                       return !m_cancel.load();
                                   },
                                   discSha1, err, &m_cancel);

        if (!hashOk || m_cancel.load())
        {
            if (m_cancel.load())
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_status.cancelled = true;
                m_status.running = false;
            }
            else
            {
                fail("Disc hash failed: " + err);
            }
            return;
        }

        if (discSha1 != kExpectedDiscSha1)
        {
            fail("Disc verification failed: SHA-1 " + discSha1 + " does not match expected European PAL retail disc (" + kExpectedDiscSha1 + ")");
            return;
        }
        setStep(Step::Check, 1.0, "Disc verification passed.");
    }

    // Step: Extract
    if (std::find(plan.begin(), plan.end(), Step::Extract) != plan.end())
    {
        setStep(Step::Extract, 0.0, "Extracting files from XDVDFS...");
        fs::path gameFilesDir = dataDir / "game" / "files";
        std::string err;
        bool extractOk = extractXdvdfs(m_options.discIso, gameFilesDir,
                                       [this](double frac, const std::string &cur) {
                                           setStep(Step::Extract, frac, cur);
                                           return !m_cancel.load();
                                       },
                                       err, &m_cancel);

        if (!extractOk || m_cancel.load())
        {
            if (m_cancel.load())
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_status.cancelled = true;
                m_status.running = false;
            }
            else
            {
                fail("Extraction failed: " + err);
            }
            return;
        }

        std::string markerErr;
        if (!writeInstallMarker(dataDir, kExpectedDiscSha1, markerErr))
        {
            fail("Cannot finish disc installation: " + markerErr);
            return;
        }
        setStep(Step::Extract, 1.0, "Extraction complete.");
    }

    // Step: Generate
    if (std::find(plan.begin(), plan.end(), Step::Generate) != plan.end())
    {
        setStep(Step::Generate, 0.05, "Preparing ReXGlue codegen...");
        std::string err;
        bool genOk = generateCode(m_options.root, m_options.recompiler, logDir,
                                  [this](double frac, const std::string &detail) {
                                      setStep(Step::Generate, frac, detail);
                                      return !m_cancel.load();
                                  },
                                  err, dataDir, &m_cancel);

        if (!genOk || m_cancel.load())
        {
            if (m_cancel.load())
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_status.cancelled = true;
                m_status.running = false;
            }
            else
            {
                fail("Recompiler codegen failed: " + err);
            }
            return;
        }
        setStep(Step::Generate, 1.0, "Translation complete.");
    }

    // Step: Build
    if (std::find(plan.begin(), plan.end(), Step::Build) != plan.end())
    {
        setStep(Step::Build, 0.05, "Configuring native CMake build...");
        fs::path executable;
        std::string err;
        fs::path buildLog = logDir / "build.log";
        fs::path generatedDir = dataDir / "generated";

        bool buildOk = buildGame(m_options.root, buildDir, m_options.tools, buildLog,
                                 [this](const BuildProgress &p) {
                                     double f = 0.05;
                                     std::string msg = p.phase == "configure" ? "Configuring CMake..." : "Compiling C++...";
                                     if (p.phase == "build" && p.total > 0)
                                     {
                                         f = 0.10 + 0.88 * (static_cast<double>(p.done) / static_cast<double>(p.total));
                                         msg = "[" + std::to_string(p.done) + "/" + std::to_string(p.total) + "] Compiling...";
                                     }
                                     setStep(Step::Build, f, msg);
                                 },
                                 executable, err, &m_cancel, generatedDir);

        if (!buildOk || m_cancel.load())
        {
            if (m_cancel.load())
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_status.cancelled = true;
                m_status.running = false;
            }
            else
            {
                fail("Compilation failed: " + err);
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status.executable = executable;
        }
        setStep(Step::Build, 1.0, "Build finished successfully.");
    }

    // Done
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.step = Step::Done;
        m_status.fraction = 1.0;
        m_status.overall = 1.0;
        m_status.detail = "Game is ready to play!";
        m_status.running = false;
        m_status.finished = true;
    }
}

} // namespace dw5e::setup
