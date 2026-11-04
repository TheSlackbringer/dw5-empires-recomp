#pragma once
#include <filesystem>
#include <string>

namespace dw5e::setup {
bool scheduleUninstall(const std::filesystem::path &package, const std::filesystem::path &root,
                       bool keepSaves, std::filesystem::path &failureLog, std::string &error);
}
