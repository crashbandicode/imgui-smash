#pragma once

// L3 + R3 toggles ImGui gamepad navigation. With navigation on, the D-pad,
// face buttons, L/R and ZL/ZR also drive every ImGui window this host draws
// (Online Deluxe's, in its external mode), while the game and other plugins
// still receive every press. Since 1.0.2 the chord does nothing unless
// sd:/ultimate/imgui_smash/config.ini sets gamepad_nav_toggle=on.

namespace nav_toggle {

// One stick click held and the other newly pressed.
inline bool chord(bool hold_left, bool press_right, bool hold_right, bool press_left) {
  return (hold_left && press_right) || (hold_right && press_left);
}

// Navigation after this frame: always off while the toggle is disabled,
// otherwise flipped by the chord.
inline bool next(bool current, bool enabled, bool chord_pressed) {
  if (!enabled) {
    return false;
  }
  return chord_pressed ? !current : current;
}

}  // namespace nav_toggle
