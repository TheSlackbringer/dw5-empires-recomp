// Exercise native input and the actual application startup hook without a
// window, attached devices, or retail/generated game code.
#include <rex/image_info.h>
constexpr rex::PPCImageInfo PPCImageConfig{};
#include "../src/dw5e_app.h"

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/platform/env.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/windowed_app_context.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// This unrelated F9 hook is never invoked by the path-config tests.
void Dw5eRegisterSnapshotBind() {}

namespace {
using namespace rex::input;
using rex::input::mnk::MnkInputDriver;
using rex::X_RESULT;
using rex::X_STATUS;
using rex::ui::VirtualKey;
int failures = 0;

void Check(bool ok, const std::string& message) {
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

class HeadlessContext final : public rex::ui::WindowedAppContext {
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};

class TestApp final : public Dw5eApp {
 public:
  explicit TestApp(HeadlessContext& ctx)
      : Dw5eApp(ctx, "dw5e-keyboard-test", PPCImageConfig) {}
};

void ApplyAppDefaults() {
  HeadlessContext context;
  TestApp app(context);
  rex::PathConfig paths;
  paths.game_data_root = "unused-test-game-root";
  paths.user_data_root = "unused-test-user-root";
  app.OnConfigurePaths(paths);
}

struct SavedFlag {
  std::string value, default_value;
  rex::cvar::Source source;
};
using Flags = std::map<std::string, SavedFlag>;

Flags SnapshotFlags() {
  Flags result;
  for (const auto& flag : rex::cvar::GetRegistry()) {
    result.emplace(flag.name, SavedFlag{flag.getter(), flag.default_value, flag.source});
  }
  return result;
}

void RestoreFlags(const Flags& saved) {
  for (auto& flag : rex::cvar::GetRegistry()) {
    const auto& original = saved.at(flag.name);
    flag.setter(original.value);
    flag.default_value = original.default_value;
    flag.source = original.source;
  }
}

void Key(MnkInputDriver& driver, VirtualKey key, bool down) {
  rex::ui::KeyEvent event(nullptr, key, 1, !down, false, false, false, false);
  if (down) driver.OnKeyDown(event);
  else driver.OnKeyUp(event);
}

struct Expected {
  uint16_t buttons = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
  uint8_t lt = 0, rt = 0;
};

void Expect(const X_INPUT_STATE& state, Expected expected, const std::string& label) {
  const auto& pad = state.gamepad;
  Check(pad.buttons == expected.buttons && pad.thumb_lx == expected.lx &&
            pad.thumb_ly == expected.ly && pad.thumb_rx == expected.rx &&
            pad.thumb_ry == expected.ry && pad.left_trigger == expected.lt &&
            pad.right_trigger == expected.rt,
        label + " (buttons=" + std::to_string(static_cast<uint16_t>(pad.buttons)) +
            ", LX=" + std::to_string(static_cast<int16_t>(pad.thumb_lx)) +
            ", LY=" + std::to_string(static_cast<int16_t>(pad.thumb_ly)) +
            ", RX=" + std::to_string(static_cast<int16_t>(pad.thumb_rx)) +
            ", RY=" + std::to_string(static_cast<int16_t>(pad.thumb_ry)) + ")");
}

X_INPUT_STATE Read(MnkInputDriver& driver, DeviceId id) {
  X_INPUT_STATE state{};
  Check(driver.GetDeviceState(id, &state) == X_ERROR_SUCCESS, "keyboard state readable");
  return state;
}

void TestKeys(MnkInputDriver& driver, DeviceId id) {
  struct Binding { VirtualKey key; Expected expected; const char* name; };
  const Binding bindings[] = {
      {VirtualKey::kW, {.ly = 32767}, "W left-stick up"},
      {VirtualKey::kS, {.ly = -32767}, "S left-stick down"},
      {VirtualKey::kA, {.lx = -32767}, "A left-stick left"},
      {VirtualKey::kD, {.lx = 32767}, "D left-stick right"},
      {VirtualKey::kUp, {.buttons = X_INPUT_GAMEPAD_DPAD_UP}, "Up D-pad only"},
      {VirtualKey::kDown, {.buttons = X_INPUT_GAMEPAD_DPAD_DOWN}, "Down D-pad only"},
      {VirtualKey::kLeft, {.buttons = X_INPUT_GAMEPAD_DPAD_LEFT}, "Left D-pad only"},
      {VirtualKey::kRight, {.buttons = X_INPUT_GAMEPAD_DPAD_RIGHT}, "Right D-pad only"},
      {VirtualKey::kZ, {.buttons = X_INPUT_GAMEPAD_X}, "Z attack without Back"},
      {VirtualKey::kNumpad0, {.buttons = X_INPUT_GAMEPAD_X}, "Numpad0 attack"},
      {VirtualKey::kV, {.buttons = X_INPUT_GAMEPAD_Y}, "V charge"},
      {VirtualKey::kNumpad1, {.buttons = X_INPUT_GAMEPAD_Y}, "Numpad1 charge"},
      {VirtualKey::kC, {.buttons = X_INPUT_GAMEPAD_B}, "C Musou/cancel"},
      {VirtualKey::kBack, {.buttons = X_INPUT_GAMEPAD_B}, "Backspace cancel"},
      {VirtualKey::kX, {.buttons = X_INPUT_GAMEPAD_A}, "X jump without Start"},
      {VirtualKey::kSpace, {.buttons = X_INPUT_GAMEPAD_A}, "Space jump without Start"},
      {VirtualKey::kQ, {.buttons = X_INPUT_GAMEPAD_LEFT_SHOULDER}, "Q guard"},
      {VirtualKey::kE, {.buttons = X_INPUT_GAMEPAD_RIGHT_SHOULDER}, "E bow"},
      {VirtualKey::kReturn, {.buttons = X_INPUT_GAMEPAD_START}, "Enter pause"},
      {VirtualKey::kTab, {.buttons = X_INPUT_GAMEPAD_BACK}, "Tab map"},
      {VirtualKey::kJ, {.rx = -32767}, "J right-stick left only"},
      {VirtualKey::kL, {.rx = 32767}, "L right-stick right only"},
      {VirtualKey::kI, {.ry = 32767}, "I right-stick up only"},
      {VirtualKey::kK, {.ry = -32767}, "K right-stick down only"},
      {VirtualKey::k1, {.lt = 255}, "1 left trigger"},
      {VirtualKey::k3, {.rt = 255}, "3 right trigger"},
      {VirtualKey::kF, {.buttons = X_INPUT_GAMEPAD_LEFT_THUMB}, "F left-stick click"},
      {VirtualKey::kG, {.buttons = X_INPUT_GAMEPAD_RIGHT_THUMB}, "G right-stick click"},
  };
  for (const auto& binding : bindings) {
    Key(driver, binding.key, true);
    Expect(Read(driver, id), binding.expected, binding.name);
    Key(driver, binding.key, false);
    Expect(Read(driver, id), {}, std::string(binding.name) + " releases to neutral");
  }
  Key(driver, VirtualKey::kW, true);
  Key(driver, VirtualKey::kZ, true);
  Expect(Read(driver, id), {.buttons = X_INPUT_GAMEPAD_X, .ly = 32767}, "move + attack");
  Key(driver, VirtualKey::kQ, true);
  Expect(Read(driver, id), {.buttons = X_INPUT_GAMEPAD_X | X_INPUT_GAMEPAD_LEFT_SHOULDER,
                           .ly = 32767}, "move + guard + attack");
  rex::ui::UISetupEvent focus;
  driver.OnLostFocus(focus);
  Expect(Read(driver, id), {}, "focus loss clears held inputs");
  Key(driver, VirtualKey::kX, true);
  driver.OnGotFocus(focus);
  Expect(Read(driver, id), {}, "background key press stays neutral after focus regain");
  Key(driver, VirtualKey::kX, true);
  Expect(Read(driver, id), {.buttons = X_INPUT_GAMEPAD_A}, "fresh press after focus regain");
  Key(driver, VirtualKey::kX, false);
  for (auto key : {VirtualKey::kW, VirtualKey::kS, VirtualKey::kA, VirtualKey::kD,
                   VirtualKey::kI, VirtualKey::kK, VirtualKey::kJ, VirtualKey::kL}) {
    Key(driver, key, true);
  }
  Expect(Read(driver, id), {}, "opposite stick keys cancel");
  driver.OnLostFocus(focus);
  driver.OnGotFocus(focus);
}

class FakeGamepads final : public InputDriver {
 public:
  FakeGamepads() : InputDriver(nullptr, 0) {}
  std::array<X_INPUT_STATE, 2> states{};
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    for (uint64_t i = 0; i < states.size(); ++i) {
      DeviceInfo info;
      info.id = static_cast<DeviceId>(0x1001 + i);
      info.name = "In-process test gamepad";
      out.push_back(info);
    }
  }
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* state) override {
    const auto index = static_cast<uint64_t>(id) - 0x1001;
    if (index >= states.size()) return X_ERROR_DEVICE_NOT_CONNECTED;
    *state = states[index];
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(DeviceId, uint32_t, X_INPUT_CAPABILITIES*) override {
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(DeviceId, X_INPUT_VIBRATION*) override { return X_ERROR_SUCCESS; }
  X_RESULT GetDeviceKeystroke(DeviceId, uint32_t, X_INPUT_KEYSTROKE*) override {
    return X_ERROR_EMPTY;
  }
};

void TestGamepads() {
  InputSystem system(nullptr);
  auto keyboard = std::make_unique<MnkInputDriver>(nullptr, 0);
  auto* keys = keyboard.get();
  // Keyboard enumerates first deliberately: it must not consume a pad slot.
  system.AddDriver(std::move(keyboard));
  auto pads = std::make_unique<FakeGamepads>();
  auto* physical = pads.get();
  system.AddDriver(std::move(pads));
  system.SetDeviceAssignment(std::make_unique<SlotAssignment>());

  for (uint32_t bit = 1; bit <= 0x8000; bit <<= 1) {
    physical->states[0].gamepad = {};
    physical->states[0].gamepad.buttons = static_cast<uint16_t>(bit);
    physical->states[0].gamepad.left_trigger = 123;
    physical->states[0].gamepad.right_trigger = 231;
    physical->states[0].gamepad.thumb_lx = -32768;
    physical->states[0].gamepad.thumb_ly = 32767;
    physical->states[0].gamepad.thumb_rx = -12345;
    physical->states[0].gamepad.thumb_ry = 23456;
    physical->states[1].gamepad = physical->states[0].gamepad;
    physical->states[1].gamepad.buttons = static_cast<uint16_t>(bit ^ 0xFFFF);
    for (uint32_t user = 0; user < 2; ++user) {
      X_INPUT_STATE state{};
      Check(system.GetState(user, &state) == X_ERROR_SUCCESS, "gamepad retains player slot");
      Check(std::memcmp(&state.gamepad, &physical->states[user].gamepad,
                        sizeof(state.gamepad)) == 0,
            "all physical gamepad buttons, triggers and axes pass through unchanged");
    }
  }
  physical->states[0].gamepad = {};
  physical->states[0].gamepad.buttons = X_INPUT_GAMEPAD_B;
  physical->states[0].gamepad.thumb_rx = 12000;
  Key(*keys, VirtualKey::kW, true);
  Key(*keys, VirtualKey::kZ, true);
  Key(*keys, VirtualKey::kQ, true);
  X_INPUT_STATE state{};
  Check(system.GetState(0, &state) == X_ERROR_SUCCESS, "merged player-one state");
  Expect(state, {.buttons = X_INPUT_GAMEPAD_B | X_INPUT_GAMEPAD_X | X_INPUT_GAMEPAD_LEFT_SHOULDER,
                 .ly = 32767, .rx = 12000}, "keyboard + unchanged gamepad share player one");
  Check(system.GetState(1, &state) == X_ERROR_SUCCESS &&
            std::memcmp(&state.gamepad, &physical->states[1].gamepad, sizeof(state.gamepad)) == 0,
        "keyboard input does not affect player two");
  Check(rex::cvar::SetFlagByName("mnk_mode", "false"), "disable keyboard explicitly");
  ApplyAppDefaults();
  Check(system.GetState(0, &state) == X_ERROR_SUCCESS &&
            std::memcmp(&state.gamepad, &physical->states[0].gamepad, sizeof(state.gamepad)) == 0,
        "keyboard opt-out preserves gamepad exactly");
}

void TestOverrides(const Flags& initial, const std::filesystem::path& config) {
  RestoreFlags(initial);
  ApplyAppDefaults();
  {
    std::ofstream file(config);
    file << "mnk_mode = false\nkeybind_x = 'H'\nmnk_mouse = false\n";
  }
  rex::cvar::LoadConfig(config);
  ApplyAppDefaults();
  Check(!rex::cvar::Query<bool>("mnk_mode") && rex::cvar::GetFlagByName("keybind_x") == "H" &&
            rex::cvar::GetFlagSource("keybind_x") == rex::cvar::Source::kConfig,
        "saved config bindings and explicit keyboard opt-out win");
  rex::cvar::ResetToDefault("keybind_x");
  rex::cvar::ResetToDefault("mnk_mode");
  Check(rex::cvar::GetFlagByName("keybind_x") == "Z,Numpad0" &&
            rex::cvar::Query<bool>("mnk_mode"), "reset uses application defaults");

  RestoreFlags(initial);
  Check(rex::platform::env::set("REX_MNK_MODE", "false"), "set test-only keyboard environment");
  Check(rex::platform::env::set("REX_KEYBIND_X", "L"), "set test-only binding environment");
  rex::cvar::ApplyEnvironment();
  ApplyAppDefaults();
  rex::cvar::LoadConfig(config);
  Check(!rex::cvar::Query<bool>("mnk_mode") && rex::cvar::GetFlagByName("keybind_x") == "L" &&
            rex::cvar::GetFlagSource("keybind_x") == rex::cvar::Source::kEnvironment,
        "explicit environment values (including old SDK default) win over application/config");

  RestoreFlags(initial);
  char program[] = "keyboard-test", enable[] = "--mnk_mode", bind[] = "--keybind_x=T";
  char* argv[] = {program, enable, bind};
  rex::cvar::Init(3, argv);
  rex::cvar::ApplyEnvironment();
  ApplyAppDefaults();
  rex::cvar::LoadConfig(config);
  Check(rex::cvar::Query<bool>("mnk_mode") && rex::cvar::GetFlagByName("keybind_x") == "T" &&
            rex::cvar::GetFlagSource("keybind_x") == rex::cvar::Source::kCommandLine,
        "command-line enable/custom binding win over environment/config");

  RestoreFlags(initial);
  char disable[] = "--no-mnk_mode";
  char* disabled_argv[] = {program, disable};
  rex::cvar::Init(2, disabled_argv);
  ApplyAppDefaults();
  Check(!rex::cvar::Query<bool>("mnk_mode"), "command-line opt-out wins");

  RestoreFlags(initial);
  Check(rex::cvar::SetFlagByName("mnk_mode", "true") &&
            rex::cvar::SetFlagByName("keybind_x", "R"), "set explicit runtime bindings");
  ApplyAppDefaults();
  Check(rex::cvar::Query<bool>("mnk_mode") && rex::cvar::GetFlagByName("keybind_x") == "R",
        "runtime bindings survive repeated default application");
}
}  // namespace

int main() {
  try {
    rex::LogConfig logging;
    logging.log_to_console = true;
    rex::InitLogging(logging);
    const auto initial = SnapshotFlags();
    ApplyAppDefaults();
    Check(rex::cvar::Query<bool>("mnk_mode"), "ordinary app startup enables keyboard");
    Check(!rex::cvar::Query<bool>("mnk_mouse"), "ordinary app startup leaves mouse capture off");
    for (const auto& flag : rex::cvar::GetRegistry()) {
      if (flag.name != "mnk_mode" && !flag.name.starts_with("keybind_")) {
        const auto& original = initial.at(flag.name);
        Check(flag.getter() == original.value && flag.default_value == original.default_value &&
                  flag.source == original.source, "unrelated setting unchanged: " + flag.name);
      }
    }

    MnkInputDriver driver(nullptr, 0);
    Check(driver.Setup() == X_STATUS_SUCCESS, "headless keyboard setup");
    std::vector<DeviceInfo> devices;
    driver.EnumerateDevices(devices);
    Check(devices.size() == 1 && devices[0].synthetic, "keyboard available without physical pad");
    if (!devices.empty()) TestKeys(driver, devices[0].id);
    TestGamepads();

    const auto config = std::filesystem::temp_directory_path() /
        ("dw5e-keyboard-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".toml");
    TestOverrides(initial, config);
    std::filesystem::remove(config);
    if (failures) return 1;
    std::cout << "PASS: application keyboard defaults, all bindings/releases/chords, focus, settings precedence, "
                 "and unchanged gamepad state/player slots (headless)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
