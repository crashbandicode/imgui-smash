# Changelog

## 1.1.0 (2026-11-13)

The standalone catches up with the ImGui Online Deluxe links: its archive is
built from the same source Deluxe 0.10.4 links (`9405a49`), plus 1.0.2's
texture-table guard and navigation toggle. Changes since v1.0.1:

- **Text beyond ASCII** (`966835c`, `bec80ec`, `9405a49`, from the former
  `unicode-shared-font` and `sparse-codepoint-index` branches): the console's
  shared fonts after JetBrains Mono's ASCII subset, a static Latin set plus a
  dynamic set of the text on screen, and an opt-in paged codepoint index
  (off in the standalone). In Eden, Online Deluxe 0.10.4 drawing through
  this standalone showed its opponent label's own glyphs instead of `?`.
- **Swapchain texture-table guard** (`97ca947`, the Switch build below).
- **L3 + R3 is off by default** (`c27ab7e`, 1.0.2 below).
- Archive `c1e4471b…` (14,820,452 bytes), built by
  `Build-Standalone.ps1 -Action archive`; two clean builds and Deluxe's own
  `tools/prepare_imgui.py` commands gave the same bytes.
- `host-tests/run.py` runs all nine host tests (adds `nav_toggle` and
  `overlay_texture_policy`); `tools/deluxe_parity.py` compares this pin with
  Online Deluxe's.

## 1.0.2 (2026-11-11, never released; its changes are in 1.1.0)

- **L3 + R3 is off by default.** Holding one stick click and pressing the other
  no longer toggles ImGui gamepad navigation unless
  `sd:/ultimate/imgui_smash/config.ini` contains `gamepad_nav_toggle = on`
  (read once at boot). With navigation on, the D-pad and face buttons also
  drive the host's windows that accept ImGui navigation while the game still
  receives every press; TrainingRooms' Kirby-hat shortcut (B + one stick
  click) could complete the chord by accident.
- New C function `imgui_smash_set_gamepad_nav_toggle(bool)`; the standalone
  calls it before `imgui_smash_init`.
- Carries the texture-table guard of the Switch build below.
- Archive `7be7d40f…` (13,884,166 bytes). A 1.0.2 standalone (`f72a6b8d…`)
  was installed on the operator's card on 2026-11-12.

## Switch build (2026-10-22, now on `main`)

- The standalone on the Switch from 2026-10-22 to 2026-11-12 (`d8e987fe…`) was
  v1.0.1 plus a private patch: the backend's swapchain texture table holds at
  most three pointers, and the overlay draws only after a successful acquire
  with an index inside it (`overlay_texture_policy.hpp`). Archive `1237aa46…`
  (13,861,332 bytes). Committed with its host test; the commit rebuilds both
  files byte-identically.

## 1.0.1

- The `deluxe` renderer merged into `main`; see [docs/VALIDATION.md](docs/VALIDATION.md).
