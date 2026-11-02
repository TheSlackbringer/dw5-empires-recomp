// Test-only replacement for human GUI selection; production launcher and pipeline are linked unchanged.
#include "setup/install_location.h"
#include "setup/wizard_ui.h"
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
#include <cstdlib>
#endif
namespace dw5e::setup
{
bool runWizardWindow(WizardConfig &config)
{
    InstallFolderSelection selection{config.launcherDir, config.base.effectiveDataDir(),
                                     config.explicitDestination};
#ifdef _WIN32
    if (const wchar_t *folder = _wgetenv(L"DW5E_FIXTURE_GUI_FOLDER"))
        selection.choose(std::filesystem::path(folder));
#else
    if (const char *folder = std::getenv("DW5E_FIXTURE_GUI_FOLDER"))
        selection.choose(pathFromText(folder));
#endif
    if (!selection.prepare())
    {
        std::cerr << selection.error;
        return false;
    }
    config.base.dataDir = selection.destination;
    SetupRun run(config.base);
    run.start();
    return run.wait();
}
int runWizardConsole(const SetupOptions &)
{
    return 1;
}
} // namespace dw5e::setup
