#include "dw5e_input.h"

#include <rex/cvar.h>

#include <string_view>
#include <utility>

void Dw5eConfigureKeyboardDefaults() {
  // Only keyboard bindings change. Physical controller drivers and assignment
  // keep their existing behavior. Digits avoid modifier chords for triggers.
  static constexpr std::pair<std::string_view, std::string_view> defaults[] = {
      {"mnk_mode", "true"},
      {"keybind_a", "X,Space"},
      {"keybind_b", "C,Backspace"},
      {"keybind_x", "Z,Numpad0"},
      {"keybind_y", "V,Numpad1"},
      {"keybind_left_shoulder", "Q"},
      {"keybind_right_shoulder", "E"},
      {"keybind_left_trigger", "1"},
      {"keybind_right_trigger", "3"},
      {"keybind_lstick_up", "W"},
      {"keybind_lstick_down", "S"},
      {"keybind_lstick_left", "A"},
      {"keybind_lstick_right", "D"},
      {"keybind_lstick_press", "F"},
      {"keybind_rstick_up", "I"},
      {"keybind_rstick_down", "K"},
      {"keybind_rstick_left", "J"},
      {"keybind_rstick_right", "L"},
      {"keybind_rstick_press", "G"},
      {"keybind_dpad_up", "Up"},
      {"keybind_dpad_down", "Down"},
      {"keybind_dpad_left", "Left"},
      {"keybind_dpad_right", "Right"},
      {"keybind_start", "Return"},
      {"keybind_back", "Tab"},
  };

  // CLI/environment have already been parsed. Preserve their source/value,
  // and leave these as defaults so subsequent config loading still wins.
  // Update reset metadata too; SetFlagByName would incorrectly mark these as
  // runtime overrides. No registry entries are added/removed during startup.
  for (auto& flag : rex::cvar::GetRegistry()) {
    for (const auto& [name, value] : defaults) {
      if (flag.name != name) continue;
      flag.default_value = value;
      if (flag.source == rex::cvar::Source::kDefault) {
        flag.setter(value);
      }
      break;
    }
  }
}
