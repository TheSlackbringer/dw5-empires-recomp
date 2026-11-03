#pragma once
// Setup Wizard UI and console driver.
// ImGui + SDL3 interface with single weighted progress bar, drag-and-drop ISO picker,
// and headless unattended console fallback for automated testing.

#include "setup_pipeline.h"
#include "installation.h"

#include <filesystem>
#include <string>

namespace dw5e::setup {

struct WizardConfig
{
    SetupOptions base;
    std::filesystem::path launcherDir;
    std::string destinationError;
    bool explicitDestination = false;
    std::string layoutError;
    std::filesystem::path prefilledIso;
    bool discInstalled = false;
    bool closeWhenFinished = true;
    bool noWindow = false;
    bool manageOnly = false;
    bool modeChosen = false;
    SaveMode desiredMode = SaveMode::Normal;
    SaveConflict conflict = SaveConflict::Ask;
    std::filesystem::path importSource;
    std::vector<std::filesystem::path> discovered;
};

bool prepareWizardInstallation(WizardConfig &config, const std::filesystem::path &destination,
                               std::string &error);
struct InstallFolderSelection;
bool prepareWizardSelection(WizardConfig &config, InstallFolderSelection &selection);

// Runs setup in interactive GUI window (ImGui + SDL3).
// Returns the final chosen destination in config.base, including after a GUI selection.
bool runWizardWindow(WizardConfig &config);

// Runs setup headless on console, printing progress steps to stdout.
int runWizardConsole(const SetupOptions &options);

} // namespace dw5e::setup
