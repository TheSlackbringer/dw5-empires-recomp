#include "install_state.h"
#include "install_location.h"

#include <cstdlib>
#include <fstream>

namespace dw5e::setup {

namespace fs = std::filesystem;

fs::path playerDataDir()
{
    std::string error;
    const auto launcher = launcherDirectory(error);
    fs::path destination;
    bool explicitOverride = false;
    return resolveInstallLocation(launcher, {}, destination, explicitOverride, error) ? destination : fs::path{};
}

InstallState installState(const fs::path &dataDir, std::string *problem)
{
    if (dataDir.empty())
    {
        if (problem)
            *problem = "no data directory configured";
        return InstallState::Missing;
    }

    std::error_code ec;
    const fs::path marker = dataDir / "game" / "installed.txt";
    if (!fs::exists(marker, ec))
    {
        if (problem)
            *problem = "game files have not been extracted yet";
        return InstallState::Missing;
    }

    const fs::path xex = dataDir / "game" / "files" / "default.xex";
    if (!fs::exists(xex, ec) || fs::file_size(xex, ec) == 0)
    {
        if (problem)
            *problem = "default.xex is missing or corrupted; repair required";
        return InstallState::Incomplete;
    }

    return InstallState::Installed;
}

bool writeInstallMarker(const fs::path &dataDir, const std::string &discSha1, std::string &error)
{
    std::error_code ec;
    fs::create_directories(dataDir / "game", ec);
    if (ec)
    {
        error = "cannot create directory " + pathText(dataDir / "game");
        return false;
    }

    const fs::path marker = dataDir / "game" / "installed.txt";
    std::ofstream out(marker, std::ios::trunc);
    if (!out)
    {
        error = "cannot write install marker to " + pathText(marker);
        return false;
    }

    out << "DW5E Disc Installed\n";
    out << "sha1: " << discSha1 << "\n";
    if (!out.flush())
    {
        error = "cannot finish writing install marker " + pathText(marker);
        return false;
    }
    return true;
}

} // namespace dw5e::setup
