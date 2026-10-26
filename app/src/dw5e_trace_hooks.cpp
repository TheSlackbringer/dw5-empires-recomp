// Temporary mid-asm trace hooks for the player-object use-after-free (see config/trace_hooks.toml).

#include <mutex>
#include <unordered_set>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>

namespace {

std::mutex g_mutex;
std::unordered_set<uint32_t> g_players;  // objects constructed as the derived player class

uint32_t RefCount(uint32_t obj) {
  uint8_t* base = rex::Runtime::instance()->virtual_membase();
  return __builtin_bswap32(*reinterpret_cast<uint32_t*>(base + obj + 4));
}

bool Tracked(uint32_t obj) {
  std::lock_guard lock(g_mutex);
  return g_players.contains(obj);
}

}  // namespace

void dw5e_trace_ctor(PPCRegister& r30) {
  {
    std::lock_guard lock(g_mutex);
    g_players.insert(r30.u32);
  }
  REXLOG_INFO("[trace] ctor     obj=0x{:08X}", r30.u32);
}

void dw5e_trace_created(PPCRegister& r3) {
  REXLOG_INFO("[trace] created  obj=0x{:08X} (stored in worker global+28) ref={}", r3.u32,
              r3.u32 ? RefCount(r3.u32) : 0);
}

void dw5e_trace_addref(PPCRegister& r3) {
  if (Tracked(r3.u32))
    REXLOG_INFO("[trace] addref   obj=0x{:08X} ref {} -> {}", r3.u32, RefCount(r3.u32),
                RefCount(r3.u32) + 1);
}

void dw5e_trace_release(PPCRegister& r3) {
  if (Tracked(r3.u32))
    REXLOG_INFO("[trace] release  obj=0x{:08X} ref {} -> {}", r3.u32, RefCount(r3.u32),
                RefCount(r3.u32) - 1);
}

void dw5e_trace_dtor(PPCRegister& r3) {
  bool tracked = false;
  {
    std::lock_guard lock(g_mutex);
    tracked = g_players.erase(r3.u32) > 0;
  }
  REXLOG_INFO("[trace] DTOR     obj=0x{:08X}{}", r3.u32, tracked ? "" : " (untracked)");
}

void dw5e_trace_callback(PPCRegister& r3) {
  REXLOG_INFO("[trace] callback arg=0x{:08X}", r3.u32);
}

void dw5e_trace_worker(PPCRegister& r3) {
  REXLOG_INFO("[trace] worker   obj=0x{:08X}{}", r3.u32,
              Tracked(r3.u32) ? "" : "  <-- NOT ALIVE (destroyed or never constructed)");
}
