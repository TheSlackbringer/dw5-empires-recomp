/**
 * @file        system/frame_timing.cpp
 * @brief       Debug aid: where each guest frame's time goes (see frame_timing.h).
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/system/frame_timing.h>

#include <atomic>

#include <rex/chrono/clock.h>

namespace rex::system::frame_timing {

namespace {
std::atomic<bool> enabled_{false};
std::atomic<uint64_t> gpu_wait_for_commands_{0};
std::atomic<uint64_t> gpu_wait_reg_mem_{0};
thread_local uint64_t thread_kernel_wait_ticks_ = 0;
}  // namespace

void Enable() { enabled_.store(true, std::memory_order_relaxed); }
bool enabled() { return enabled_.load(std::memory_order_relaxed); }

uint64_t ThreadKernelWaitTicks() { return thread_kernel_wait_ticks_; }

ScopedKernelWait::ScopedKernelWait() {
  if (enabled()) {
    start_ = chrono::Clock::QueryHostTickCount();
  }
}

ScopedKernelWait::~ScopedKernelWait() {
  if (start_) {
    thread_kernel_wait_ticks_ += chrono::Clock::QueryHostTickCount() - start_;
  }
}

void AddGpuWaitForCommands(uint64_t ticks) {
  gpu_wait_for_commands_.fetch_add(ticks, std::memory_order_relaxed);
}

void AddGpuWaitRegMem(uint64_t ticks) {
  gpu_wait_reg_mem_.fetch_add(ticks, std::memory_order_relaxed);
}

GpuWaitTotals gpu_wait_totals() {
  return {gpu_wait_for_commands_.load(std::memory_order_relaxed),
          gpu_wait_reg_mem_.load(std::memory_order_relaxed)};
}

}  // namespace rex::system::frame_timing
