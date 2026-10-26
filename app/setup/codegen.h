#pragma once
// ReXGlue codegen driver for setup wizard and builder pipeline.
// Translates PowerPC Xenon binary (default.xex) from player's extracted disc into native C++
// using ReXGlue CLI, writing sources to <dataDir>/generated/default.

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

namespace dw5e::setup {

// Prepares a manifest configured for dataDir and invokes ReXGlue codegen.
// Progress callback reports current stage/file.
bool generateCode(const std::filesystem::path &root,
                  const std::filesystem::path &recompiler,
                  const std::filesystem::path &logDir,
                  const std::function<bool(double fraction, const std::string &detail)> &progress,
                  std::string &error,
                  const std::filesystem::path &dataDir = {},
                  const std::atomic<bool> *cancel = nullptr);

} // namespace dw5e::setup
