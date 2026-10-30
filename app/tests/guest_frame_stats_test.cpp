// Guest frame rate shown by the F3 overlay (issue 007): fixed swap times must give the expected rate.
#include <rex/system/guest_frame_stats.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
using rex::system::GuestFrameRateMeter;
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

// Records `frames` swaps at `fps` starting at `start`; returns the time of the last swap.
uint64_t Feed(GuestFrameRateMeter& meter, uint64_t start, double fps, int frames) {
  uint64_t t = start;
  for (int i = 0; i < frames; ++i) {
    t = start + static_cast<uint64_t>(std::llround(i * kFreq / fps));
    meter.RecordSwap(t);
  }
  return t;
}
}  // namespace

int main() {
  {
    GuestFrameRateMeter meter;
    auto s = meter.Stats(kFreq, kFreq);
    Check(s.frame_count == 0 && s.fps == 0, "no swaps: no frames, 0 FPS");
  }
  {
    GuestFrameRateMeter meter;
    uint64_t last = Feed(meter, 1000, 60.0, 180);
    auto s = meter.Stats(last, kFreq);
    Check(Near(s.fps, 60.0, 0.05), "60 FPS steady: got " + std::to_string(s.fps));
    Check(Near(s.frame_time_ms, 1000.0 / 60.0, 0.02), "60 FPS frame time: got " + std::to_string(s.frame_time_ms));
    Check(s.frame_count == 180, "frame count 180: got " + std::to_string(s.frame_count));
  }
  {
    GuestFrameRateMeter meter;
    uint64_t last = Feed(meter, 1000, 30.0, 90);
    Check(Near(meter.Stats(last, kFreq).fps, 30.0, 0.05), "30 FPS (movies)");
  }
  {
    // Rate change: only the last second counts.
    GuestFrameRateMeter meter;
    uint64_t last = Feed(meter, 1000, 60.0, 120);
    last = Feed(meter, last + kFreq / 25, 25.0, 50);
    Check(Near(meter.Stats(last, kFreq).fps, 25.0, 0.05), "60 then 25 FPS: shows 25");
  }
  {
    // Stall: no swaps for over a second -> 0 FPS, frames still counted.
    GuestFrameRateMeter meter;
    uint64_t last = Feed(meter, 1000, 60.0, 60);
    auto s = meter.Stats(last + 2 * kFreq, kFreq);
    Check(s.fps == 0 && s.frame_count == 60, "stall: 0 FPS, 60 frames counted");
  }
  {
    // More swaps than the ring holds within a second (uncapped rates): still a sane rate.
    GuestFrameRateMeter meter;
    uint64_t last = Feed(meter, 1000, 1000.0, 2000);
    Check(Near(meter.Stats(last, kFreq).fps, 1000.0, 1.0), "1000 FPS beyond the ring size");
  }
  if (failures) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "PASS guest_frame_stats_test\n";
  return 0;
}
