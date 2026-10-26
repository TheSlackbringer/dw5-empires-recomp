// Right-stick camera for the battle follow camera (config/camera_hooks.toml).
//
// Yaw: the battle camera is a leash. Each tick 0x822AE118 -> 0x822AC7A8 keeps the eye's current
// horizontal direction around the focus point and only corrects its distance (0x822AB570), then
// eases yaw/pitch toward the eye->target direction (0x822AB660). Rotating the stored eye around
// the focus before the tick therefore turns the camera, and the turn persists.
//
// Pitch: the game owns the eye height (clamps against focus height and terrain), so pitch is an
// overlay instead. After the tick the eye is orbited vertically around the target by a
// stick-accumulated offset and view pitch is recomputed like 0x822A9348 does; the next tick
// starts by restoring exactly what the game had, so its own logic never sees the offset.
// The view matrix (0x822AA558) is built from eye + the pitch/yaw angles (0x822A9530).

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include <rex/cvar.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>

REXCVAR_DEFINE_DOUBLE(dw5e_rstick_camera_speed, 0.05, "DW5E",
                      "Right-stick camera turn speed in radians per camera tick at full deflection "
                      "(0 disables)");
REXCVAR_DEFINE_BOOL(dw5e_rstick_camera_invert, false, "DW5E",
                    "Invert the right-stick camera turn direction");
// Off by default: tested 2026-10-12, free pitch lets the view swing past the character and adds
// little in Musou combat. Kept for experiments (try 0.03).
REXCVAR_DEFINE_DOUBLE(dw5e_rstick_camera_pitch_speed, 0.0, "DW5E",
                      "Right-stick camera pitch speed in radians per camera tick at full deflection "
                      "(0 disables)");
REXCVAR_DEFINE_BOOL(dw5e_rstick_camera_invert_y, false, "DW5E",
                    "Invert the right-stick camera pitch direction");

namespace {

constexpr uint32_t kViewArray = 0x82846FD0;  // 6 views x 256 bytes, accessor 0x822AA950
constexpr uint32_t kViewStride = 0x100;
constexpr uint32_t kViewPitch = 0x20;
constexpr uint32_t kViewEye = 0x30;
constexpr uint32_t kViewTarget = 0x40;
constexpr uint32_t kViewEyeCopy = 0x50;
constexpr uint32_t kPadSlot0 = 0x82AAB2E8;  // pad manager slot 0 (236 bytes), filled by 0x822B9FF0
constexpr uint32_t kPadRightX = 0x18;       // RX + 128, 0..255
constexpr uint32_t kPadRightY = 0x1C;       // ~RY + 128 (game negates Y: push up = low values)
constexpr float kDeadzone = 0.2f;
constexpr float kMaxPitchOffset = 0.7f;     // radians either way from the game's own elevation
constexpr float kMinElevation = 0.03f;      // keep the eye above the target
constexpr float kMaxElevation = 1.3f;       // and short of straight down

uint8_t* Guest(uint32_t addr) { return rex::Runtime::instance()->virtual_membase() + addr; }

uint32_t LoadU32(uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, Guest(addr), 4);
  return std::byteswap(v);
}

float LoadF32(uint32_t addr) { return std::bit_cast<float>(LoadU32(addr)); }

void StoreF32(uint32_t addr, float f) {
  uint32_t v = std::byteswap(std::bit_cast<uint32_t>(f));
  std::memcpy(Guest(addr), &v, 4);
}

struct Vec3 {
  float x, y, z;
};

Vec3 LoadVec3(uint32_t addr) { return {LoadF32(addr), LoadF32(addr + 4), LoadF32(addr + 8)}; }

void StoreVec3(uint32_t addr, const Vec3& v) {
  StoreF32(addr, v.x);
  StoreF32(addr + 4, v.y);
  StoreF32(addr + 8, v.z);
}

bool SameBits(const Vec3& a, const Vec3& b) { return std::memcmp(&a, &b, sizeof(Vec3)) == 0; }

// Stick axis from a pad slot field, -1..1 after the dead zone.
float Axis(uint32_t field) {
  float v = (static_cast<float>(LoadU32(kPadSlot0 + field) & 0xFF) - 127.0f) / 128.0f;
  if (std::fabs(v) < kDeadzone)
    return 0.0f;
  return std::copysign(std::min(1.0f, (std::fabs(v) - kDeadzone) / (1.0f - kDeadzone)), v);
}

// Rotate the XZ components of the vec4 at `vec` around (cx, cz).
void RotateAround(uint32_t vec, float cx, float cz, float c, float s) {
  float dx = LoadF32(vec + 0) - cx;
  float dz = LoadF32(vec + 8) - cz;
  StoreF32(vec + 0, cx + dx * c - dz * s);
  StoreF32(vec + 8, cz + dx * s + dz * c);
}

// Pitch overlay state for view 0 (the camera tick runs on one guest thread).
float g_pitch_offset = 0.0f;
bool g_tick_view0 = false;
bool g_overlay = false;
Vec3 g_game_eye, g_our_eye;
float g_game_pitch, g_our_pitch;

void UndoOverlay(uint32_t view) {
  if (!g_overlay)
    return;
  g_overlay = false;
  // Only restore what is still ours; if the game rewrote it in between, keep the game's value.
  if (SameBits(LoadVec3(view + kViewEye), g_our_eye))
    StoreVec3(view + kViewEye, g_game_eye);
  if (std::bit_cast<uint32_t>(LoadF32(view + kViewPitch)) == std::bit_cast<uint32_t>(g_our_pitch))
    StoreF32(view + kViewPitch, g_game_pitch);
}

void ApplyOverlay(uint32_t view) {
  if (g_pitch_offset == 0.0f)
    return;
  const Vec3 eye = LoadVec3(view + kViewEye);
  const Vec3 tgt = LoadVec3(view + kViewTarget);
  const float vx = eye.x - tgt.x, vy = eye.y - tgt.y, vz = eye.z - tgt.z;
  const float h = std::hypot(vx, vz);
  if (h < 1.0f)
    return;
  const float r = std::hypot(h, vy);
  // World is Y-down: elevation above the target is positive when eye.y < target.y.
  const float elev = std::atan2(-vy, h);
  const float new_elev = std::clamp(elev + g_pitch_offset, kMinElevation, kMaxElevation);
  if (std::fabs(new_elev - elev) < 1e-5f)
    return;
  const float nh = r * std::cos(new_elev);
  const Vec3 new_eye{tgt.x + vx / h * nh, tgt.y - r * std::sin(new_elev), tgt.z + vz / h * nh};
  // Same formula as 0x822A9348: pitch = -atan2(target.y - eye.y, horizontal distance).
  const float new_pitch = -std::atan2(tgt.y - new_eye.y, nh);

  g_game_eye = eye;
  g_game_pitch = LoadF32(view + kViewPitch);
  StoreVec3(view + kViewEye, new_eye);
  StoreF32(view + kViewPitch, new_pitch);
  g_our_eye = LoadVec3(view + kViewEye);
  g_our_pitch = LoadF32(view + kViewPitch);
  g_overlay = true;
}

}  // namespace

void dw5e_camera_tick_begin(PPCRegister& r3, PPCRegister& r4) {
  g_tick_view0 = r3.u32 == 0 && r4.u32 != 0;
  if (!g_tick_view0)
    return;
  const uint32_t view = kViewArray;
  UndoOverlay(view);

  const double speed = REXCVAR_GET(dw5e_rstick_camera_speed);
  float x = Axis(kPadRightX);
  if (speed != 0.0 && x != 0.0f) {
    if (REXCVAR_GET(dw5e_rstick_camera_invert))
      x = -x;
    // World is Y-down, so a positive XZ rotation turns the camera the opposite way to the stick.
    const float angle = -x * static_cast<float>(speed);
    const float c = std::cos(angle), s = std::sin(angle);
    const float fx = LoadF32(r4.u32 + 0), fz = LoadF32(r4.u32 + 8);
    RotateAround(view + kViewEye, fx, fz, c, s);
    RotateAround(view + kViewEyeCopy, fx, fz, c, s);
  }

  const double pitch_speed = REXCVAR_GET(dw5e_rstick_camera_pitch_speed);
  if (pitch_speed == 0.0) {
    g_pitch_offset = 0.0f;
    return;
  }
  // Push up (negative after the game's negation) = camera moves down and looks up.
  float up = -Axis(kPadRightY);
  if (REXCVAR_GET(dw5e_rstick_camera_invert_y))
    up = -up;
  g_pitch_offset = std::clamp(g_pitch_offset - up * static_cast<float>(pitch_speed),
                              -kMaxPitchOffset, kMaxPitchOffset);
}

void dw5e_camera_tick_end(PPCRegister&) {
  if (g_tick_view0)
    ApplyOverlay(kViewArray);
  g_tick_view0 = false;
}

void dw5e_camera_snap_begin(PPCRegister& r3) {
  // The snap path recentres the camera (L1); drop the pitch offset with it.
  if (r3.u32 != 0)
    return;
  UndoOverlay(kViewArray);
  g_pitch_offset = 0.0f;
}
