// Debug: F9 dumps guest memory to notes/snapshots/snap_NNN.bin for diffing (camera hunt).
//
// File format (all little-endian): "DW5SNAP1", then records of
//   u32 guest_address, u32 length, length bytes of raw (big-endian guest) data.
// Unmapped 64 KiB chunks are skipped.

#include <sys/uio.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/keybinds.h>

namespace {

constexpr uint32_t kChunk = 0x10000;

struct Range {
  uint32_t begin, end;
};
// Guest virtual heap (64 KiB pages; game objects live here) and the XEX image incl. .data.
constexpr Range kRanges[] = {{0x40000000u, 0x80000000u}, {0x82000000u, 0x82B50000u}};

std::atomic<int> g_counter{0};

bool ReadChunk(const uint8_t* host, std::vector<uint8_t>& buf) {
  iovec local{buf.data(), buf.size()};
  iovec remote{const_cast<uint8_t*>(host), buf.size()};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(buf.size());
}

void TakeSnapshot() {
  const uint8_t* base = rex::Runtime::instance()->virtual_membase();
  const char* home = std::getenv("HOME");
  std::filesystem::path dir = std::filesystem::path(home ? home : ".") / "Projects/dw5e-port/notes/snapshots";
  std::filesystem::create_directories(dir);
  int n = g_counter.fetch_add(1) + 1;
  std::filesystem::path path = dir / fmt::format("snap_{:03}.bin", n);

  std::ofstream out(path, std::ios::binary);
  out.write("DW5SNAP1", 8);
  std::vector<uint8_t> buf(kChunk);
  uint64_t saved = 0;
  for (const auto& r : kRanges) {
    for (uint64_t a = r.begin; a < r.end; a += kChunk) {
      if (!ReadChunk(base + a, buf)) continue;
      uint32_t hdr[2] = {static_cast<uint32_t>(a), kChunk};
      out.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
      out.write(reinterpret_cast<const char*>(buf.data()), kChunk);
      saved += kChunk;
    }
  }
  REXLOG_INFO("[snapshot] #{} saved {} MiB to {}", n, saved >> 20, path.string());
}

}  // namespace

void Dw5eRegisterSnapshotBind() {
  rex::ui::RegisterBind("bind_dw5e_snapshot", "F9", "Dump guest memory snapshot (camera hunt)",
                        [] { TakeSnapshot(); });
}
