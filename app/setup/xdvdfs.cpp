#include "xdvdfs.h"
#include "sha1.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <stack>

namespace dw5e::setup {

namespace {

uint16_t readU16LE(const uint8_t *ptr)
{
    return static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
}

uint32_t readU32LE(const uint8_t *ptr)
{
    return static_cast<uint32_t>(ptr[0]) |
           (static_cast<uint32_t>(ptr[1]) << 8) |
           (static_cast<uint32_t>(ptr[2]) << 16) |
           (static_cast<uint32_t>(ptr[3]) << 24);
}

int64_t detectPartitionBase(std::ifstream &stream)
{
    char magicBuf[kXdvdfsMagicLen];
    for (uint64_t base : kPartitionOffsets)
    {
        uint64_t magicOffset = base + 32ULL * kXdvdfsSectorSize;
        stream.seekg(static_cast<std::streamoff>(magicOffset), std::ios::beg);
        if (!stream)
        {
            stream.clear();
            continue;
        }
        if (stream.read(magicBuf, kXdvdfsMagicLen) &&
            std::memcmp(magicBuf, kXdvdfsMagic, kXdvdfsMagicLen) == 0)
        {
            return static_cast<int64_t>(base);
        }
        stream.clear();
    }
    return -1;
}

bool readDirEntries(std::ifstream &stream, uint64_t base, uint32_t sector, uint32_t size,
                    const std::string &prefix, std::vector<XdvdfsEntry> &out, std::string &error)
{
    if (size == 0)
        return true;

    uint64_t tableOffset = base + static_cast<uint64_t>(sector) * kXdvdfsSectorSize;
    stream.seekg(static_cast<std::streamoff>(tableOffset), std::ios::beg);
    if (!stream)
    {
        error = "failed to seek to directory table at sector " + std::to_string(sector);
        return false;
    }

    std::vector<uint8_t> table(size);
    if (!stream.read(reinterpret_cast<char *>(table.data()), size))
    {
        error = "failed to read directory table of size " + std::to_string(size);
        return false;
    }

    std::set<size_t> seen;
    std::stack<size_t> stack;
    stack.push(0);

    struct DirNode
    {
        std::string name;
        uint32_t startSector;
        uint32_t length;
        bool isDir;
    };
    std::vector<DirNode> nodes;

    while (!stack.empty())
    {
        size_t off = stack.top();
        stack.pop();

        if (seen.find(off) != seen.end() || off + 14 > table.size())
            continue;
        seen.insert(off);

        const uint8_t *p = table.data() + off;
        uint16_t left = readU16LE(p);
        uint16_t right = readU16LE(p + 2);
        uint32_t start = readU32LE(p + 4);
        uint32_t length = readU32LE(p + 8);
        uint8_t attr = p[12];
        uint8_t nlen = p[13];

        if (left == 0xFFFF && right == 0xFFFF && start == 0xFFFFFFFF)
            continue; // Padding entry

        if (off + 14 + nlen <= table.size())
        {
            std::string name(reinterpret_cast<const char *>(p + 14), nlen);
            bool isDir = (attr & 0x10) != 0;
            nodes.push_back({name, start, length, isDir});
        }

        if (left != 0)
            stack.push(static_cast<size_t>(left) * 4);
        if (right != 0)
            stack.push(static_cast<size_t>(right) * 4);
    }

    // Sort nodes by name for deterministic order
    std::sort(nodes.begin(), nodes.end(), [](const DirNode &a, const DirNode &b) {
        return a.name < b.name;
    });

    for (const auto &node : nodes)
    {
        std::string fullPath = prefix.empty() ? node.name : (prefix + "/" + node.name);
        out.push_back({fullPath, node.startSector, node.length, node.isDir});

        if (node.isDir && node.length > 0)
        {
            if (!readDirEntries(stream, base, node.startSector, node.length, fullPath, out, error))
                return false;
        }
    }
    return true;
}

} // namespace

bool hashDiscSha1(const std::filesystem::path &isoPath,
                  const std::function<bool(double fraction)> &progress,
                  std::string &calculatedSha1,
                  std::string &error,
                  const std::atomic<bool> *cancel)
{
    std::ifstream stream(isoPath, std::ios::binary);
    if (!stream)
    {
        error = "cannot open disc image: " + isoPath.string();
        return false;
    }

    stream.seekg(0, std::ios::end);
    uint64_t totalBytes = static_cast<uint64_t>(stream.tellg());
    stream.seekg(0, std::ios::beg);

    if (totalBytes == 0)
    {
        error = "disc image is empty: " + isoPath.string();
        return false;
    }

    constexpr size_t kChunkSize = 8 * 1024 * 1024; // 8 MB chunks
    std::vector<char> buffer(kChunkSize);
    Sha1 sha;

    uint64_t processed = 0;
    while (processed < totalBytes)
    {
        if (cancel && cancel->load())
        {
            error = "cancelled by user";
            return false;
        }

        size_t toRead = static_cast<size_t>(std::min<uint64_t>(kChunkSize, totalBytes - processed));
        if (!stream.read(buffer.data(), toRead))
        {
            error = "error reading disc image at byte " + std::to_string(processed);
            return false;
        }

        sha.update(buffer.data(), toRead);
        processed += toRead;

        if (progress)
        {
            double frac = static_cast<double>(processed) / static_cast<double>(totalBytes);
            if (!progress(frac))
            {
                error = "cancelled";
                return false;
            }
        }
    }

    calculatedSha1 = sha.hex();
    return true;
}

bool checkDiscImage(const std::filesystem::path &isoPath,
                    uint64_t &partitionBase,
                    std::string &error)
{
    std::ifstream stream(isoPath, std::ios::binary);
    if (!stream)
    {
        error = "cannot open disc image: " + isoPath.string();
        return false;
    }

    int64_t base = detectPartitionBase(stream);
    if (base < 0)
    {
        error = "no valid XDVDFS volume descriptor found (is this an Xbox 360 game ISO?)";
        return false;
    }

    partitionBase = static_cast<uint64_t>(base);

    // Read root directory sector and size
    uint64_t descOffset = partitionBase + 32ULL * kXdvdfsSectorSize + kXdvdfsMagicLen;
    stream.seekg(static_cast<std::streamoff>(descOffset), std::ios::beg);
    uint8_t rootDesc[8];
    if (!stream.read(reinterpret_cast<char *>(rootDesc), 8))
    {
        error = "failed to read XDVDFS root descriptor";
        return false;
    }

    uint32_t rootSector = readU32LE(rootDesc);
    uint32_t rootSize = readU32LE(rootDesc + 4);

    std::vector<XdvdfsEntry> entries;
    if (!readDirEntries(stream, partitionBase, rootSector, rootSize, "", entries, error))
        return false;

    bool foundXex = false;
    for (const auto &e : entries)
    {
        std::string lowerName = e.path;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if (lowerName == "default.xex")
        {
            foundXex = true;
            break;
        }
    }

    if (!foundXex)
    {
        error = "valid XDVDFS filesystem found, but default.xex is missing";
        return false;
    }

    return true;
}

bool listXdvdfsFiles(const std::filesystem::path &isoPath,
                     std::vector<XdvdfsEntry> &entries,
                     std::string &error)
{
    std::ifstream stream(isoPath, std::ios::binary);
    if (!stream)
    {
        error = "cannot open disc image: " + isoPath.string();
        return false;
    }

    int64_t base = detectPartitionBase(stream);
    if (base < 0)
    {
        error = "no valid XDVDFS volume descriptor found";
        return false;
    }

    uint64_t descOffset = static_cast<uint64_t>(base) + 32ULL * kXdvdfsSectorSize + kXdvdfsMagicLen;
    stream.seekg(static_cast<std::streamoff>(descOffset), std::ios::beg);
    uint8_t rootDesc[8];
    if (!stream.read(reinterpret_cast<char *>(rootDesc), 8))
    {
        error = "failed to read XDVDFS root descriptor";
        return false;
    }

    uint32_t rootSector = readU32LE(rootDesc);
    uint32_t rootSize = readU32LE(rootDesc + 4);

    entries.clear();
    return readDirEntries(stream, static_cast<uint64_t>(base), rootSector, rootSize, "", entries, error);
}

bool extractXdvdfs(const std::filesystem::path &isoPath,
                   const std::filesystem::path &outDir,
                   const std::function<bool(double fraction, const std::string &currentFile)> &progress,
                   std::string &error,
                   const std::atomic<bool> *cancel,
                   const std::vector<std::string> &fileFilter)
{
    std::vector<XdvdfsEntry> entries;
    if (!listXdvdfsFiles(isoPath, entries, error))
        return false;

    std::ifstream stream(isoPath, std::ios::binary);
    if (!stream)
    {
        error = "cannot open disc image: " + isoPath.string();
        return false;
    }

    int64_t base = detectPartitionBase(stream);
    if (base < 0)
    {
        error = "partition lost";
        return false;
    }
    uint64_t partitionBase = static_cast<uint64_t>(base);

    // Calculate total bytes to extract
    uint64_t totalBytes = 0;
    std::vector<const XdvdfsEntry *> toExtract;
    for (const auto &e : entries)
    {
        if (e.isDirectory)
            continue;

        if (!fileFilter.empty())
        {
            bool match = false;
            for (const auto &filter : fileFilter)
            {
                if (e.path == filter || e.path.rfind(filter + "/", 0) == 0)
                {
                    match = true;
                    break;
                }
            }
            if (!match)
                continue;
        }

        toExtract.push_back(&e);
        totalBytes += e.size;
    }

    if (totalBytes == 0)
        totalBytes = 1;

    uint64_t bytesExtracted = 0;
    constexpr size_t kChunkSize = 4 * 1024 * 1024; // 4 MB chunk
    std::vector<char> buffer(kChunkSize);

    for (const auto *entry : toExtract)
    {
        if (cancel && cancel->load())
        {
            error = "cancelled by user";
            return false;
        }

        std::filesystem::path destPath = outDir / entry->path;
        std::error_code ec;
        std::filesystem::create_directories(destPath.parent_path(), ec);

        uint64_t fileOffset = partitionBase + static_cast<uint64_t>(entry->startSector) * kXdvdfsSectorSize;
        stream.seekg(static_cast<std::streamoff>(fileOffset), std::ios::beg);
        if (!stream)
        {
            error = "seek error for " + entry->path;
            return false;
        }

        std::ofstream out(destPath, std::ios::binary);
        if (!out)
        {
            error = "failed to create output file: " + destPath.string();
            return false;
        }

        uint32_t remaining = entry->size;
        while (remaining > 0)
        {
            if (cancel && cancel->load())
            {
                error = "cancelled by user";
                return false;
            }

            size_t take = std::min<size_t>(kChunkSize, remaining);
            if (!stream.read(buffer.data(), take))
            {
                error = "read error extracting " + entry->path;
                return false;
            }
            if (!out.write(buffer.data(), take))
            {
                error = "write error for " + destPath.string();
                return false;
            }
            remaining -= static_cast<uint32_t>(take);
            bytesExtracted += take;

            if (progress)
            {
                double frac = static_cast<double>(bytesExtracted) / static_cast<double>(totalBytes);
                if (!progress(frac, entry->path))
                {
                    error = "cancelled";
                    return false;
                }
            }
        }
        out.close();
    }

    return true;
}

} // namespace dw5e::setup
