# Changelog

## On `main` after 1.0.2 (not released)

- **Text beyond ASCII**, merged 2026-11-13 from the `unicode-shared-font` and
  `sparse-codepoint-index` branches (`966835c`, `bec80ec`, `9405a49`): the
  console's shared fonts after JetBrains Mono's ASCII subset, a static Latin
  set plus a dynamic set of the text on screen, and an opt-in paged codepoint
  index. See the README.
- Online Deluxe 0.10.4 links an archive built from `9405a49` (`26ea2c3b…`).
  The standalone crate still pins 1.0.2's archive `7be7d40f…`, which predates
  the merge.

## 1.0.2 (2026-11-11, not released yet)

- **L3 + R3 is off by default.** Holding one stick click and pressing the other
  no longer toggles ImGui gamepad navigation unless
  `sd:/ultimate/imgui_smash/config.ini` contains `gamepad_nav_toggle = on`
  (read once at boot). With navigation on, the D-pad and face buttons also
  drive every window the host draws (Online Deluxe's included) while the game
  still receives every press; TrainingRooms' Kirby-hat shortcut (B + one stick
  click) could complete the chord by accident.
- New C function `imgui_smash_set_gamepad_nav_toggle(bool)`; the standalone
  calls it before `imgui_smash_init`.
- Carries the texture-table guard of the Switch build below.
- Archive `7be7d40f…` (13,884,166 bytes). A 1.0.2 standalone (`f72a6b8d…`)
  has been on the operator's card since 2026-11-12.

## Switch build (2026-10-22, now on `main`)

- The standalone on the Switch from 2026-10-22 to 2026-11-12 (`d8e987fe…`) was
  v1.0.1 plus a private patch: the backend's swapchain texture table holds at
  most three pointers, and the overlay draws only after a successful acquire
  with an index inside it (`overlay_texture_policy.hpp`). Archive `1237aa46…`
  (13,861,332 bytes). Committed with its host test; the commit rebuilds both
  files byte-identically.

## 1.0.1

- The `deluxe` renderer merged into `main`; see [docs/VALIDATION.md](docs/VALIDATION.md).
