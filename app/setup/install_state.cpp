#include "install_state.h"

#include <cstdlib>
#include <fstream>

namespace dw5e::setup {

namespace fs = std::filesystem;

fs::path playerDataDir()
{
    if (const char *env = std::getenv("DW5E_DATA_DIR"))
    {
        if (*env)
            return fs::path(env);
    }
#ifdef _WIN32
    if (const char *local = std::getenv("LOCALAPPDATA"))
        return fs::path(local) / "DW5E";
    if (const char *user = std::getenv("USERPROFILE"))
        return fs::path(user) / "AppData" / "Local" / "DW5E";
#else
    if (const char *xdg = std::getenv("XDG_DATA_HOME"))
    {
        if (*xdg)
            return fs::path(xdg) / "dw5e";
    }
    if (const char *home = std::getenv("HOME"))
        return fs::path(home) / ".local" / "share" / "dw5e";
#endif
    return {};
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
        error = "cannot create directory " + (dataDir / "game").string();
        return false;
    }

    const fs::path marker = dataDir / "game" / "installed.txt";
    std::ofstream out(marker, std::ios::trunc);
    if (!out)
    {
        error = "cannot write install marker to " + marker.string();
        return false;
    }

    out << "DW5E Disc Installed\n";
    out << "sha1: " << discSha1 << "\n";
    return true;
}

} // namespace dw5e::setup
