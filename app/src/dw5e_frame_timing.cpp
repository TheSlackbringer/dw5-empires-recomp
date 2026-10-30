// Frame-time counter (issue 008, config/frame_timing_hooks.toml): logs once per second where the
// presenting game thread's frame time goes. Debug aid, off by default (DW5E-DEBUG).
//
// The game waits for the GPU by spinning in D3D_BlockOnFence 0x822D2FE0 (a delay loop around a GPU
// read-pointer check, no kernel wait), so CPU time cannot tell its work from its waiting. The two
// hooks bracket that function (nested calls count once); D3D_Swap 0x822DD688 marks each frame.
// Kernel waits and the GPU command processor's waits are timed in the runtime (frame_timing.h).
// Per frame: work = frame time - fence wait - kernel wait. Kernel waits made inside a fence wait
// count as kernel wait only.

#include <thread>

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/frame_timing.h>

REXCVAR_DEFINE_BOOL(dw5e_frame_timing, false, "DW5E",
                    "Log once per second where each game frame's time goes: game work, GPU fence "
                    "wait, kernel waits, GPU thread (debug)");

namespace {

namespace ft = rex::system::frame_timing;

uint64_t Now() { return rex::chrono::Clock::QueryHostTickCount(); }

thread_local int fence_depth = 0;
thread_local uint64_t fence_start = 0;
thread_local uint64_t fence_kernel_start = 0;
thread_local uint64_t fence_ticks = 0;  // fence wait since the last swap, without kernel waits

// Owned by the presenting thread (the first thread that swaps).
std::thread::id presenter;
uint64_t last_swap = 0;
uint64_t kernel_at_last_swap = 0;
ft::Window window;

}  // namespace

void dw5e_fence_wait_begin() {
  if (!ft::enabled()) {
    return;
  }
  if (fence_depth++ == 0) {
    fence_start = Now();
    fence_kernel_start = ft::ThreadKernelWaitTicks();
  }
}

void dw5e_fence_wait_end() {
  if (fence_depth == 0) {
    return;  // entered before timing was enabled
  }
  if (--fence_depth == 0) {
    const uint64_t kernel = ft::ThreadKernelWaitTicks() - fence_kernel_start;
    fence_ticks += (Now() - fence_start) - kernel;
  }
}

void dw5e_frame_swap() {
  if (!REXCVAR_GET(dw5e_frame_timing)) {
    return;
  }
  if (!ft::enabled()) {
    ft::Enable();
    presenter = std::this_thread::get_id();
  }
  if (std::this_thread::get_id() != presenter) {
    return;
  }
  const uint64_t now = Now();
  const uint64_t kernel_now = ft::ThreadKernelWaitTicks();
  if (last_swap) {
    ft::Report r;
    if (window.AddFrame(now - last_swap, fence_ticks, kernel_now - kernel_at_last_swap,
                        ft::gpu_wait_totals(), rex::chrono::Clock::QueryHostTickFrequency(), &r)) {
      REXLOG_INFO(
          "frame timing: {} frames in {:.2f} s, {:.2f} ms avg (p95 {:.2f}); main: work {:.2f}, "
          "fence wait {:.2f}, kernel wait {:.2f} ms; GPU thread: busy {:.2f}, waiting for "
          "commands {:.2f}, WAIT_REG_MEM {:.2f} ms",
          r.frames, r.seconds, r.frame_ms, r.p95_ms, r.work_ms, r.fence_ms, r.kernel_ms,
          r.gpu_busy_ms, r.gpu_wait_commands_ms, r.gpu_wait_reg_mem_ms);
    }
  }
  last_swap = now;
  kernel_at_last_swap = kernel_now;
  fence_ticks = 0;
}
