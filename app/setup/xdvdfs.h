#pragma once
// 100% native C++ XDVDFS / XGD2 filesystem reader and extractor for DW5 Empires (Xbox 360).
// Directly reads the game partition, verifies the European PAL retail SHA-1 checksum,
// parses the binary-tree directory tables, and extracts default.xex and game media.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace dw5e::setup {

constexpr const char *kExpectedDiscSha1 = "82fc63021a0919d8b97584682babf0654997c0a9";
constexpr uint32_t kXdvdfsSectorSize = 2048;
constexpr const char *kXdvdfsMagic = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kXdvdfsMagicLen = 20;

// Game partition base offsets: plain XISO (0), XGD3 (0x2080000), XGD2 (0xFD90000), XGD1 (0x18300000)
constexpr uint64_t kPartitionOffsets[] = {
    0xFD90000ULL, // Primary for retail DW5E (XGD2 PAL)
    0ULL,
    0x2080000ULL,
    0x18300000ULL
};

struct XdvdfsEntry
{
    std::string path;
    uint32_t startSector = 0;
    uint32_t size = 0;
    bool isDirectory = false;
};

// Computes the SHA-1 of the disc image, streaming chunks (e.g. 8 MB) with progress updates.
bool hashDiscSha1(const std::filesystem::path &isoPath,
                  const std::function<bool(double fraction)> &progress,
                  std::string &calculatedSha1,
                  std::string &error,
                  const std::atomic<bool> *cancel = nullptr);

// Quick check: finds the XDVDFS partition volume descriptor and verifies default.xex exists.
bool checkDiscImage(const std::filesystem::path &isoPath,
                    uint64_t &partitionBase,
                    std::string &error);

// Lists all files inside the XDVDFS filesystem.
bool listXdvdfsFiles(const std::filesystem::path &isoPath,
                     std::vector<XdvdfsEntry> &entries,
                     std::string &error);

// Extracts all files or a specific subset into outDir.
// If fileFilter is non-empty, only files whose paths match or are within fileFilter are extracted.
bool extractXdvdfs(const std::filesystem::path &isoPath,
                   const std::filesystem::path &outDir,
                   const std::function<bool(double fraction, const std::string &currentFile)> &progress,
                   std::string &error,
                   const std::atomic<bool> *cancel = nullptr,
                   const std::vector<std::string> &fileFilter = {});

} // namespace dw5e::setup
