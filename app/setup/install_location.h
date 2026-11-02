#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace dw5e::setup
{
std::string pathText(const std::filesystem::path &path);
std::filesystem::path pathFromText(const std::string &text);
// Actual executable location; AppImages use their original file, never the mounted payload.
std::filesystem::path executableDirectory(std::string &error);
std::filesystem::path launcherDirectory(std::string &error);
std::vector<std::string> nativeArguments(int argc, char **argv);
bool resolveInstallLocation(const std::filesystem::path &launcher, const std::filesystem::path &commandLine,
                            std::filesystem::path &destination, bool &explicitOverride, std::string &error);
bool validateInstallDirectory(const std::filesystem::path &directory, std::string &error);
bool rememberInstallLocation(const std::filesystem::path &launcher, const std::filesystem::path &destination,
                             std::string &error);
// Shared by the wizard and its disc-free regression tests. Empty picker results are cancellation.
struct InstallFolderSelection
{
    std::filesystem::path launcher, destination;
    bool explicitOverride = false;
    bool frozen = false;
    std::string error;
    bool choose(const std::filesystem::path &path);
    bool prepare();
};
} // namespace dw5e::setup
