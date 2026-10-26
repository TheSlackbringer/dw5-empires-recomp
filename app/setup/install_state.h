#pragma once
// Tracks player installation state in %LOCALAPPDATA%\DW5E (Windows) or ~/.local/share/dw5e (Linux).
// Fast checks verify that extracted game files exist before codegen/compilation or launching.

#include <filesystem>
#include <string>

namespace dw5e::setup {

// Windows: %LOCALAPPDATA%\DW5E. Linux: $XDG_DATA_HOME/dw5e, else ~/.local/share/dw5e.
// DW5E_DATA_DIR overrides it for automated tests or custom portable installs.
std::filesystem::path playerDataDir();

enum class InstallState
{
    Installed,  // marker present and required files exist with valid sizes
    Missing,    // no marker or game files directory missing
    Incomplete  // marker present, but default.xex is missing or 0 bytes
};

// Fast check to see if retail game assets have been extracted
InstallState installState(const std::filesystem::path &dataDir, std::string *problem = nullptr);

// Writes install marker to <dataDir>/game/installed.txt after successful extraction
bool writeInstallMarker(const std::filesystem::path &dataDir, const std::string &discSha1, std::string &error);

} // namespace dw5e::setup
