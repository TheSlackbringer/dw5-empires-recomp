#pragma once
#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace dw5e::setup
{
class InstallationLock
{
    std::intptr_t handle_ = -1;

  public:
    InstallationLock() = default;
    InstallationLock(const InstallationLock &) = delete;
    InstallationLock &operator=(const InstallationLock &) = delete;
    ~InstallationLock();
    bool acquire(const std::filesystem::path &root, std::string &error);
};
enum class SaveMode
{
    Normal,
    Portable,
    Legacy
};
enum class SaveConflict
{
    Ask,
    UseSource,
    KeepDestination
};
struct Installation
{
    std::filesystem::path root, userData, configDir;
    SaveMode mode = SaveMode::Normal;
    bool recorded = false;
};
const char *saveModeName(SaveMode mode);
std::filesystem::path normalUserDataDirectory();
std::filesystem::path normalConfigDirectory();
bool loadInstallation(const std::filesystem::path &root, Installation &installation, std::string &error);
void configureSaveMode(Installation &installation, SaveMode mode);
std::filesystem::path existingInstallationRoot(const std::filesystem::path &selected);
std::filesystem::path newInstallationRoot(const std::filesystem::path &selected);
bool saveInstallation(const Installation &installation, std::string &error);
bool prepareInstallation(Installation &installation, std::string &error);
bool rememberNormalInstallation(const Installation &installation, std::string &error,
                                bool makePreferred = false);
bool discoverInstallations(const std::filesystem::path &launcher, std::vector<std::filesystem::path> &found,
                           std::string &error, std::filesystem::path *preferred = nullptr);
bool switchSaveMode(Installation &installation, SaveMode mode, SaveConflict conflict, std::string &error);
bool importInstallation(const std::filesystem::path &source, Installation &destination, SaveConflict conflict,
                        std::string &error);
bool installationGameRunning(const std::filesystem::path &root, std::string &error);
bool canUninstallGameFiles(const std::filesystem::path &root);
std::filesystem::path uninstallFolder(const std::filesystem::path &root);
bool uninstallSavesShared(const Installation &installation, bool &shared, std::string &error);
bool uninstallInstallation(const std::filesystem::path &root, bool keepSaves, std::string &error);
} // namespace dw5e::setup
