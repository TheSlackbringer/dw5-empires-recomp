#pragma once
// Setup Wizard UI and console driver.
// ImGui + SDL3 interface with single weighted progress bar, drag-and-drop ISO picker,
// and headless unattended console fallback for automated testing.

#include "setup_pipeline.h"

#include <filesystem>
#include <string>

namespace dw5e::setup {

struct WizardConfig
{
    SetupOptions base;
    std::string layoutError;
    std::filesystem::path prefilledIso;
    bool discInstalled = false;
    bool closeWhenFinished = true;
    bool noWindow = false;
};

// Runs setup in interactive GUI window (ImGui + SDL3).
bool runWizardWindow(const WizardConfig &config);

// Runs setup headless on console, printing progress steps to stdout.
int runWizardConsole(const SetupOptions &options);

} // namespace dw5e::setup
