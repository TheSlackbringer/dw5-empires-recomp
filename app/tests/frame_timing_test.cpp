// Frame-time counter (issue 008): fixed frames and waits must give the expected per-frame report.
#include <rex/system/frame_timing.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
namespace ft = rex::system::frame_timing;
int failures = 0;

void Check(bool ok, const std::string& message) {
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool Near(double a, double b, double tolerance) {
  return std::fabs(a - b) <= tolerance;
}

constexpr uint64_t kFreq = 10'000'000;  // 100 ns ticks
constexpr uint64_t kMs = kFreq / 1000;
}  // namespace

int main() {
  {
    // 60 frames of 16.7 ms: 6 ms fence, 1 ms kernel; GPU waits 10 ms commands + 2 ms WAIT_REG_MEM
    // per frame. The report closes once one second of frames is in.
    ft::Window w;
    ft::Report r;
    ft::GpuWaitTotals gpu{1000 * kMs, 50 * kMs};  // totals before the window: subtracted
    const uint64_t frame = 16'667 * kFreq / 1'000'000;
    int closed_at = -1;
    for (int i = 0; i < 70 && closed_at < 0; ++i) {
      if (i > 0) {  // the first frame starts the window at the totals it sees
        gpu.for_commands += 10 * kMs;
        gpu.reg_mem += 2 * kMs;
      }
      if (w.AddFrame(frame, 6 * kMs, 1 * kMs, gpu, kFreq, &r)) {
        closed_at = i;
      }
    }
    Check(closed_at == 59, "60 frames of 16.7 ms close a one-second window (closed at " +
                               std::to_string(closed_at) + ")");
    Check(r.frames == 60, "60 frames reported");
    Check(Near(r.seconds, 1.0, 0.001), "window 1.0 s");
    Check(Near(r.frame_ms, 16.667, 0.001), "frame 16.667 ms");
    Check(Near(r.p95_ms, 16.667, 0.001), "p95 16.667 ms");
    Check(Near(r.fence_ms, 6.0, 1e-9) && Near(r.kernel_ms, 1.0, 1e-9), "fence 6 ms, kernel 1 ms");
    Check(Near(r.work_ms, 9.667, 0.001), "work = frame - fence - kernel");
    // 59 frames of GPU waits after the first: 590 + 118 ms over 60 frames.
    Check(Near(r.gpu_wait_commands_ms, 590.0 / 60, 1e-9), "GPU wait for commands per frame");
    Check(Near(r.gpu_wait_reg_mem_ms, 118.0 / 60, 1e-9), "GPU WAIT_REG_MEM per frame");
    Check(Near(r.gpu_busy_ms, r.frame_ms - r.gpu_wait_commands_ms - r.gpu_wait_reg_mem_ms, 1e-9),
          "GPU busy = frame - GPU waits");

    // The next window starts from the totals at the close.
    for (int i = 0; i < 25; ++i) {
      gpu.for_commands += 30 * kMs;
      if (w.AddFrame(40 * kMs, 0, 0, gpu, kFreq, &r)) {
        closed_at = i;
      }
    }
    Check(closed_at == 24, "25 frames of 40 ms close the next window");
    Check(r.frames == 25 && Near(r.frame_ms, 40.0, 1e-9), "25 frames of 40 ms");
    Check(Near(r.gpu_wait_commands_ms, 30.0, 1e-9) && Near(r.gpu_wait_reg_mem_ms, 0, 1e-9),
          "second window counts only its own GPU waits");
    Check(Near(r.work_ms, 40.0, 1e-9), "no waits: all work");
  }
  {
    // One slow frame among fast ones shows in p95, not hidden by the average.
    ft::Window w;
    ft::Report r;
    bool closed = false;
    for (int i = 0; i < 100 && !closed; ++i) {
      const uint64_t frame = (i % 10 == 9) ? 50 * kMs : 10 * kMs;
      closed = w.AddFrame(frame, 0, 0, {}, kFreq, &r);
    }
    Check(closed, "mixed frames close a window");
    Check(Near(r.p95_ms, 50.0, 1e-9), "p95 catches the 50 ms frames");
  }
  {
    // No tick frequency: never reports.
    ft::Window w;
    ft::Report r;
    Check(!w.AddFrame(kFreq * 2, 0, 0, {}, 0, &r), "no report without a tick frequency");
  }
  if (failures) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "frame timing tests passed\n";
  return 0;
}
