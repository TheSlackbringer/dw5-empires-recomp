/**
 * @file        system/frame_timing.h
 * @brief       Debug aid: where each guest frame's time goes (game work, waits, GPU thread).
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace rex::system::frame_timing {

// Off until Enable(); while off every recording call returns at once. Times are host ticks
// (chrono::Clock::QueryHostTickCount).
void Enable();
bool enabled();

// Time the calling thread has spent in guest kernel waits (XObject waits, XThread::Delay) since
// Enable(). Recorded by ScopedKernelWait.
uint64_t ThreadKernelWaitTicks();

class ScopedKernelWait {
 public:
  ScopedKernelWait();
  ~ScopedKernelWait();
  ScopedKernelWait(const ScopedKernelWait&) = delete;
  ScopedKernelWait& operator=(const ScopedKernelWait&) = delete;

 private:
  uint64_t start_ = 0;
};

// GPU command processor thread: time waiting for new commands from the guest, and time inside
// WAIT_REG_MEM. Totals since Enable().
void AddGpuWaitForCommands(uint64_t ticks);
void AddGpuWaitRegMem(uint64_t ticks);
struct GpuWaitTotals {
  uint64_t for_commands = 0;
  uint64_t reg_mem = 0;
};
GpuWaitTotals gpu_wait_totals();

// Averages per frame over one report window.
struct Report {
  uint32_t frames = 0;
  double seconds = 0;
  double frame_ms = 0;
  double p95_ms = 0;
  double work_ms = 0;    // frame - fence wait - kernel wait
  double fence_ms = 0;   // guest spin on GPU fences
  double kernel_ms = 0;  // guest kernel waits
  double gpu_busy_ms = 0;  // window - GPU waits
  double gpu_wait_commands_ms = 0;
  double gpu_wait_reg_mem_ms = 0;
};

// Collects the frames of the presenting guest thread and closes a report once at least one
// second of frames has been added. Single-threaded: call from the presenting thread only.
class Window {
 public:
  // `frame` = time since the previous frame, `fence` and `kernel` = waits within it, `gpu` =
  // GPU wait totals now. Returns true and fills `out` when a window closes.
  bool AddFrame(uint64_t frame, uint64_t fence, uint64_t kernel, const GpuWaitTotals& gpu,
                uint64_t tick_frequency, Report* out) {
    if (!started_) {
      started_ = true;
      gpu_start_ = gpu;
    }
    frames_.push_back(frame);
    frame_sum_ += frame;
    fence_sum_ += fence;
    kernel_sum_ += kernel;
    if (!tick_frequency || frame_sum_ < tick_frequency) {
      return false;
    }
    const double ms_per_tick = 1000.0 / static_cast<double>(tick_frequency);
    const double n = static_cast<double>(frames_.size());
    Report r;
    r.frames = static_cast<uint32_t>(frames_.size());
    r.seconds = static_cast<double>(frame_sum_) / static_cast<double>(tick_frequency);
    r.frame_ms = frame_sum_ * ms_per_tick / n;
    r.fence_ms = fence_sum_ * ms_per_tick / n;
    r.kernel_ms = kernel_sum_ * ms_per_tick / n;
    r.work_ms = r.frame_ms - r.fence_ms - r.kernel_ms;
    const size_t p95 = std::min(frames_.size() - 1, frames_.size() * 95 / 100);
    std::nth_element(frames_.begin(), frames_.begin() + p95, frames_.end());
    r.p95_ms = frames_[p95] * ms_per_tick;
    const uint64_t gpu_cmd = gpu.for_commands - gpu_start_.for_commands;
    const uint64_t gpu_reg = gpu.reg_mem - gpu_start_.reg_mem;
    r.gpu_wait_commands_ms = gpu_cmd * ms_per_tick / n;
    r.gpu_wait_reg_mem_ms = gpu_reg * ms_per_tick / n;
    r.gpu_busy_ms = r.frame_ms - r.gpu_wait_commands_ms - r.gpu_wait_reg_mem_ms;
    *out = r;
    frames_.clear();
    frame_sum_ = fence_sum_ = kernel_sum_ = 0;
    gpu_start_ = gpu;
    return true;
  }

 private:
  bool started_ = false;
  std::vector<uint64_t> frames_;
  uint64_t frame_sum_ = 0;
  uint64_t fence_sum_ = 0;
  uint64_t kernel_sum_ = 0;
  GpuWaitTotals gpu_start_;
};

}  // namespace rex::system::frame_timing
