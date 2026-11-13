# imgui-smash

> **Project note:** This fork is AI-assisted, human-directed and reviewed.
> Upstream authors retain credit; [validation and known limits](docs/VALIDATION.md)
> describe what has actually been checked.

A [Dear ImGui](https://github.com/ocornut/imgui) backend on NVN for
**Super Smash Bros. Ultimate**, forked from
[Coolsonickirby/imgui-smash](https://github.com/Coolsonickirby/imgui-smash)
at [`bb47b205`](https://github.com/Coolsonickirby/imgui-smash/commit/bb47b205ed0be2fb864269ee2d37fc886b81221e).
It is the host plugin whose exported
functions let other Skyline plugins draw their own ImGui windows, and it is
the renderer that [Online Deluxe](https://git.syntaxbyexample.com/intpa/ssbu-online-deluxe-rewrite)
links statically. The former `deluxe`, `unicode-shared-font` and
`sparse-codepoint-index` branches are now included in `main`.
Dear ImGui **1.92.7 WIP (19263)** remains pinned.

**[Download v1.2.0](https://git.syntaxbyexample.com/intpa/imgui-smash/releases/tag/v1.2.0)**
— standalone NRO, static archive and SHA-256 manifest. 1.2.0 is built from
the same source Online Deluxe 0.10.4 links (`9405a49`), plus 1.0.2's
texture-table guard and navigation toggle, so Deluxe drawing through it gets
the fonts Deluxe has built in: names beyond ASCII show as text, not `?`. It
pages the codepoint index when every client can read it, exports its counters
and takes no lock in the hook path. The standalone stays optional; Deluxe
doesn't need it. (1.1.0 was staged but never published; its changes are in
1.2.0.)

For **Smash Ultimate 13.0.5**. 1.2.0 is not on the operator's card. Its
runtime record is from Eden (2026-11-13), not a console: Online Deluxe drew
its window through it as it does through its built-in host (pairs imp-opp9
and imp-opp10), with the opponent label `Æsir テスト` in each character's own
glyph. From 2026-10-22 the
standalone on the operator's Switch was v1.0.1 plus the texture-table guard
below ([the Switch's build](docs/VALIDATION.md#the-switchs-build-2026-10-22)). It was
on the card when Online Deluxe 0.10.4 ran there on 2026-11-10, with
[Switch firmware 23.0.1](https://en-americas-support.nintendo.com/app/answers/detail/a_id/22525/~/nintendo-switch-system-updates-and-change-history)
and [Atmosphère 1.12.1](https://github.com/Atmosphere-NX/Atmosphere/releases/tag/1.12.1).
A 1.0.2 build replaced it on 2026-11-12; no console run with that build is
recorded. See [the evidence](docs/VALIDATION.md).

## What this fork changes

- **Renderer correction.** Per-slot command and control storage with fence
   checks and a frame guard. Busy slots are skipped rather than overwritten;
   recorded Eden runs cover embedded and standalone hosts. This is not a claim
   to fix every intermittent visibility issue.
- **Smaller budgets.** One 64 KiB command page and 16 KiB of control memory
  per draw slot; the unused default fonts, the demo window and the debug tools
  are compiled out.
- **Standalone companion crate.** No `imgui-api` dependency; `build.rs` links
  only an archive whose size and SHA-256 match its pin, and the archive is
  built deterministically in a pinned devkitA64 image
  (`deterministic-archive.cmake`).
- **Two artifacts, one renderer.** `libimgui_smash.nro` is the standalone
   host; `libimgui_smash.a` is the archive Deluxe links. The exact source and
   artifact identities are in the release manifest and [validation record](docs/VALIDATION.md).
- **Bounded swapchain texture table.** At most three texture pointers are
  copied, and the overlay draws only after a successful acquire with an index
  inside that table (the Switch build of 2026-10-22; released in 1.1.0).
- **Gamepad navigation off by default (since 1.1.0).** L3 + R3 no longer
  toggles ImGui gamepad navigation unless `config.ini` enables it (below).

The font work below came from the former `unicode-shared-font` and
`sparse-codepoint-index` branches. Online Deluxe 0.10.4 links it, and since
1.1.0 the standalone has it too.

- **Text beyond ASCII.**
  - **Fonts.** JetBrains Mono's ASCII subset stays the first font at 18 px.
    The console's system shared fonts (`nn::pl`: Standard with Latin and
    Japanese, Chinese, Korean, Nintendo Extended) are merged after it, read in
    place from the memory the system maps, not copied.
  - **Static set.** ASCII, Latin-1 Supplement and Latin Extended-A
    (European names) are rasterized once at start-up and never change.
  - **Dynamic set.** Any other character is rasterized when a string draws
    it. Measuring text loads advances only.
  - **Drops between frames.** Between two frames, the host reads the last
    frame's draw data. Once a dynamic glyph that was on screen has gone about
    a second without a draw, it drops every dynamic glyph and repacks the
    atlas into a new texture through the backend's texture requests
    (`ImGuiBackendFlags_RendererHasTextures`). What is still on screen loads
    again in the next frame, so the dynamic set is the text on screen, not
    everything ever shown.
  - **Atlas.** Alpha8 (an R8 texture sampled as white with that alpha),
    512x128 to start, at most 1024x1024. No dynamic glyph is rasterized once
    packing would pass half of it, so ImGui's "Out of texture memory" assert
    cannot fire.
  - **Fallback.** A missing character draws the fallback glyph (U+FFFD when a
    font has it) for that character only.
  - **Stats.** `imgui_smash_font_stats` reports the static and dynamic
    counts, rebuilds and texture and index bytes;
    `imgui_smash_font_dynamic_set` lists the dynamic set.
- **Paged codepoint index.**
  - **The cost it removes.** Dear ImGui indexes a font's glyphs in two
    arrays with an entry per codepoint up to the highest one loaded:
    6 bytes per codepoint, about 330 KB once a Hangul glyph is loaded.
  - **The patch.** `patches/imgui/0001-paged-codepoint-index.patch` (applied
    by CMake and `host-tests/run.py` to a copy of `extern/imgui`; the
    submodule stays upstream) adds `ImFontAtlasFlags_PagedCodepointIndex`.
    Bakes made with it keep the index in 256-codepoint pages, allocated
    only where glyphs are: a 512-byte page map plus 1.5 KB per page in use.
    Every read and write of the index goes through two accessors and one
    setter. No struct changes size or offset.
  - **Opt-in.** `imgui_smash_font_use_paged_index(1)`, called before
    `imgui_smash_init`, turns it on for the overlay's fonts. It is off by
    default: a plugin built with an unpatched Dear ImGui reads the index
    directly. Since 1.2.0 the standalone turns it on when every plugin that
    names one of its context exports has declared a patched reader
    (`imgui_smash_declare_paged_index_reader_v1`, [DEVELOPMENT](docs/DEVELOPMENT.md));
    otherwise it stays dense.
  - **Tests.** `paged_index_test` builds the patched and the unpatched Dear
    ImGui. It requires identical glyphs, advances, sizes, wraps and vertices
    from the dense and the paged index, and times `CalcTextSize`.
    `font_setup_test` runs every overlay test in both modes.
- **Releases carry both artifacts.** `libimgui_smash.nro` (the standalone
  host) and `libimgui_smash.a` (the archive Deluxe links). The pinned pair
  for each Deluxe version is recorded in Deluxe's `docs/IMGUI_DEPENDENCY.md`.

## Installing the host (plugin user)

Only needed when another plugin draws its own ImGui windows through this
host. Online Deluxe doesn't need it: it carries the renderer inside its own
NRO. While this host is loaded, Deluxe doesn't start its own: it draws in this
host if the exports and the ImGui version match, and its window stays off if
they don't. Drawing in this host means this host's font. From 1.1.0 that is
the font Deluxe has built in; v1.0.1's and 1.0.2's were ASCII only, so names
outside ASCII showed `?` in Deluxe's window.

- Download **`libimgui_smash.nro`** from [v1.2.0](https://git.syntaxbyexample.com/intpa/imgui-smash/releases/tag/v1.2.0).
- Place it in `sd:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/`.
- Replace an existing copy rather than installing a second host; fully restart Smash.

The `.a` archive is for developers, not the SD card. With no plugin using the host,
nothing visible happens in game; its demo window is intentionally disabled.

### Gamepad navigation (L3 + R3)

Up to 1.0.1, holding one stick click and pressing the other toggled ImGui
gamepad navigation: the D-pad, face buttons, L/R and ZL/ZR then also drove
the windows this host draws that accept ImGui navigation, while the game
still received every press. (Online Deluxe's windows opt out of ImGui
navigation and read the pad themselves.) Since 1.1.0 the chord does nothing
unless `sd:/ultimate/imgui_smash/config.ini` contains:

```ini
gamepad_nav_toggle = on
```

The file is read once at boot; without it, or with `off`, navigation stays off.
Touch and a USB keyboard/mouse are unchanged.

## Plugging in (plugin developer)

- **Rows in Deluxe's menu:** use its [plugin API and Rust SDK](https://git.syntaxbyexample.com/intpa/ssbu-online-deluxe-rewrite/src/branch/main/docs/PLUGINS.md).
- **Your own ImGui windows:** use this host's callbacks with matching ImGui bindings.

See [developer integration and build instructions](docs/DEVELOPMENT.md) for exports,
context ownership, the pinned archive and checks.

## Building

Requires Docker, Rust with `cargo-skyline`/its target standard library, and
PowerShell 7. Initialize the pinned ImGui submodule before building:

```text
git submodule update --init --recursive
pwsh -NoLogo -NoProfile -NonInteractive -File ./Build-Standalone.ps1 -Action archive
pwsh -NoLogo -NoProfile -NonInteractive -File ./Build-Standalone.ps1 -Action clippy
pwsh -NoLogo -NoProfile -NonInteractive -File ./Build-Standalone.ps1 -Action skyline
```

Host tests (g++ with C++23; warnings are errors for this repository's code):

```text
python3 -I imgui-smash/host-tests/run.py
```

Whether the standalone still links the archive Online Deluxe pins (same
digest, size, image, Dear ImGui commit and patches, and the same archive
sources as the commit Deluxe names):

```text
python3 -I tools/deluxe_parity.py <Online Deluxe checkout>
python3 -I tools/test_deluxe_parity.py
```

`host-tests/fixtures/label-fixture.ttf` is a TrueType subset of Noto Sans CJK
JP (SIL OFL 1.1, `LICENSE-label-fixture.txt`), built by `make_fixture.py`.

`build.rs` refuses an archive whose size or SHA-256 differs from its pin.
The `clippy` action checks the **host** target; Skyline checks and the C++
tests are listed in [DEVELOPMENT.md](docs/DEVELOPMENT.md#checks). Changes by
version: [CHANGELOG.md](CHANGELOG.md).
Diagnostic features `frame-guard-stats` and `draw-stats` are off in releases.
Releases use this pinned build, not upstream's `imgui-smash/Makefile`.

## Credits

Backend license: [GPL v2](imgui-smash/LICENSE). Upstream and vendored licenses
remain in their source directories.

- [Coolsonickirby](https://github.com/Coolsonickirby): modified imgui-xeno to work with Smash and created the host plugin
- [blujay](https://github.com/blu-dev): Input Visualizer mod, the reference for the NVN rendering implementation
- [imgui](https://github.com/ocornut/imgui), [cimgui](https://github.com/cimgui/cimgui), [bindgen](https://github.com/rust-lang/rust-bindgen)
- Original base: [CraftyBoss](https://github.com/CraftyBoss/MP1R-Exlaunch-Base), [exlaunch](https://github.com/shadowninja108/exlaunch/), [exlaunch-cmake](https://github.com/EngineLessCC/exlaunch-cmake/), [BDSP](https://github.com/Martmists-GH/BDSP), [Sanae](https://github.com/Sanae6), [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere), [oss-rtld](https://github.com/Thog/oss-rtld), [roccodev](https://github.com/roccodev), [BlockBuilder57](https://github.com/BlockBuilder57), [AlexCSDev](https://github.com/AlexCSDev)
