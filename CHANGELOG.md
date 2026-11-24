# Changelog

## 1.3.0 (2026-11-24, not released)

A new archive (`5605e032…`, 14,838,316 bytes, from `imgui-smash/` at
`f6fe1fa`). Not yet run in a game.

- **The console's shared fonts are the only fonts.** The embedded JetBrains
  Mono NL is gone, with its files and licence text. Before, ASCII came from
  that font and every other character from the shared fonts, and the two were
  sized by different line boxes: text from the shared fonts was drawn 1.32
  times bigger than ASCII. With one font family there is nothing to mismatch.
- **What a client sees.**
  - Text is proportional, not monospace. The digits still share one advance.
  - Font size still means line height. Capitals keep the share of the line
    they had (55.3%), from each font's own cap height and line box, so they
    are as tall as before; the baseline keeps its row at 18 px.
  - Every further shared font is drawn at the first one's em.
- **No font, no frame.** While no shared font is loaded and valid, the host
  starts no ImGui frame and calls no client. It asks for the fonts once and
  then checks about once every 60 host frames, without a lock or a wait of
  its own. Whether the `nn::pl` calls themselves can wait is not established;
  1.2.0 made the same calls once at initialization.
- **A refused glyph draws after the next rebuild.** A glyph the fill guard
  refused stayed marked "not found" across a rebuild when the fallback glyph
  was U+FFFD, so it kept drawing the fallback. A rebuild now clears those
  marks for dynamic characters. The console's standard font has no U+FFFD, so
  this did not show there.
- **Size.** The embedded font was 13,694 bytes of read-only data and a
  17,896-byte copy on the heap at run time. The NRO's size is in the release
  notes.

## 1.2.0 (2026-11-13, not released)

The same archive as 1.1.0 (`c1e4471b…`); only the Rust shell changes.

- **Paged codepoint index when every client reads it.** When the context is
  created, the host lists the loaded plugins whose read-only segment names one
  of its context exports (`src/clients.rs`). If each of them has called the new
  `imgui_smash_declare_paged_index_reader_v1(fn in its own text)`, the fonts
  index glyphs in 256-codepoint pages, as Online Deluxe's built-in host does;
  otherwise the index stays dense, as in 1.1.0, so a client built with an
  unpatched Dear ImGui still reads it. `imgui_smash_host_clients_v1` reports the
  choice.
- **The archive's counters are exported** (`imgui_smash_font_stats_wrapper`,
  `imgui_smash_font_dynamic_set_wrapper`, `imgui_smash_draw_stats_wrapper`,
  `imgui_smash_frame_guard_stats_wrapper`), so a client that draws through this
  host can report the overlay's fonts and renderer as the built-in host does.
- **No lock in the hook path.** The context-setup callbacks, the declarations
  and the choice are atomics (`clients::Slots`, `clients::Published`) instead
  of mutexes: the callbacks run inside the NVN hook that creates the context,
  and 1.1.0 held a mutex there (also while the callbacks ran) that a plugin
  registering a callback could hold. Up to 64 callbacks are kept, in order;
  the list no longer allocates. `tests/hook_locks.rs` fails on any lock
  primitive in the Rust shell or the archive's C++.
- Every 1.1.0 export is unchanged.

## 1.1.0 (2026-11-13, staged but never published; its changes are in 1.2.0)

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
