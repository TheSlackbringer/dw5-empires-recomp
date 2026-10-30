/**
 * @file        system/guest_frame_stats.h
 * @brief       Guest frame rate for the debug overlay: one guest swap (VdSwap) is one game frame.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace rex::system {

struct GuestFrameStats {
  double fps = 0;            // guest swaps per second over the last second
  double frame_time_ms = 0;  // average time between those swaps
  uint64_t frame_count = 0;  // guest swaps since start
};

// Counts guest swaps and averages their rate over the last second. RecordSwap runs on the GPU
// command processor thread, Stats on the UI thread. Times are host ticks (any monotonic clock).
class GuestFrameRateMeter {
 public:
  void RecordSwap(uint64_t now_ticks) {
    std::lock_guard<std::mutex> lock(mutex_);
    ticks_[count_ % kCapacity] = now_ticks;
    ++count_;
  }

  GuestFrameStats Stats(uint64_t now_ticks, uint64_t tick_frequency) const {
    std::lock_guard<std::mutex> lock(mutex_);
    GuestFrameStats stats;
    stats.frame_count = count_;
    if (!tick_frequency) {
      return stats;
    }
    // Swaps within the last second, newest first; the ring bounds the window at high rates.
    uint64_t newest = 0, oldest = 0;
    size_t in_window = 0;
    size_t available = count_ < kCapacity ? static_cast<size_t>(count_) : kCapacity;
    for (size_t i = 0; i < available; ++i) {
      uint64_t t = ticks_[(count_ - 1 - i) % kCapacity];
      if (t > now_ticks || now_ticks - t > tick_frequency) {
        break;
      }
      if (!in_window) {
        newest = t;
      }
      oldest = t;
      ++in_window;
    }
    if (in_window >= 2 && newest > oldest) {
      double span_s = double(newest - oldest) / double(tick_frequency);
      stats.fps = double(in_window - 1) / span_s;
      stats.frame_time_ms = 1000.0 / stats.fps;
    }
    return stats;
  }

 private:
  static constexpr size_t kCapacity = 256;
  mutable std::mutex mutex_;
  std::array<uint64_t, kCapacity> ticks_{};
  uint64_t count_ = 0;
};

}  // namespace rex::system
