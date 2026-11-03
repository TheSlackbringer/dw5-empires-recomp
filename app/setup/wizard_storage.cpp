#include "wizard_ui.h"
#include "install_state.h"
#include "install_location.h"
#include <fstream>

namespace dw5e::setup
{
bool wizardBuildIsCurrent(const SetupOptions &options, const std::filesystem::path &destination,
                          const std::string &release)
{
    if (release.empty() || options.rebuild || installState(destination) != InstallState::Installed)
        return false;
    SetupOptions selected = options;
    selected.dataDir = destination;
    const auto build = selected.effectiveBuildDir();
#ifdef _WIN32
    const auto executable = build / "dw5e.exe";
#else
    const auto executable = build / "dw5e";
#endif
    std::error_code ec;
    if (!std::filesystem::is_regular_file(executable, ec))
        return false;
    std::ifstream marker(build / "built-from.txt");
    std::string builtFrom;
    std::getline(marker, builtFrom);
    while (!builtFrom.empty() && (builtFrom.back() == '\r' || builtFrom.back() == ' '))
        builtFrom.pop_back();
    return builtFrom == release;
}

bool prepareWizardSelection(WizardConfig &config, InstallFolderSelection &selection)
{
    if (config.importSource.empty())
        return selection.prepare() &&
               prepareWizardInstallation(config, selection.destination, selection.error);
    // Keep the previous pointer until import/copy verification succeeds.
    if (!selection.error.empty() || !validateInstallDirectory(selection.destination, selection.error) ||
        !prepareWizardInstallation(config, selection.destination, selection.error))
        return false;
    if (!selection.explicitOverride &&
        !rememberInstallLocation(selection.launcher, selection.destination, selection.error))
        return false;
    selection.frozen = true;
    return true;
}
bool prepareWizardInstallation(WizardConfig &config, const std::filesystem::path &destination,
                               std::string &error)
{
    Installation installation;
    if (!loadInstallation(destination, installation, error))
        return false;
    if (installationGameRunning(destination, error))
        return false;
    if (config.modeChosen && config.desiredMode != installation.mode)
    {
        if (installation.recorded || installState(destination) == InstallState::Installed)
        {
            if (!switchSaveMode(installation, config.desiredMode, config.conflict, error))
                return false;
        }
        else
            configureSaveMode(installation, config.desiredMode);
    }
    if (!config.importSource.empty())
    {
        if (!importInstallation(config.importSource, installation, config.conflict, error))
            return false;
    }
    else if (!prepareInstallation(installation, error))
        return false;
    config.base.dataDir = destination;
    return true;
}
} // namespace dw5e::setup
