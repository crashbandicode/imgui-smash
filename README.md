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
links statically. The former `deluxe` branch is now included in `main`.
Dear ImGui **1.92.7 WIP (19263)** remains pinned.

**[Download v1.0.1](https://git.syntaxbyexample.com/intpa/imgui-smash/releases/tag/v1.0.1)**
— standalone NRO, static archive and SHA-256 manifest.

For **Smash Ultimate 13.0.5**. Current runtime references, checked 2026-10-25:
[Switch firmware 23.0.1](https://en-americas-support.nintendo.com/app/answers/detail/a_id/22525/~/nintendo-switch-system-updates-and-change-history#current),
[Atmosphère 1.12.1](https://github.com/Atmosphere-NX/Atmosphere/releases/tag/1.12.1),
and [Skyline beta `2eb226f`](https://github.com/skyline-dev/skyline/releases/tag/beta).
The renderer has recorded Eden validation; the operator also reports the Deluxe/FX
stack working on an Erista Switch. Those reports are separate from testing this
new release package. See [the evidence](docs/VALIDATION.md).

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

## Installing the host (plugin user)

Only needed when another plugin draws its own windows (for example Elite Legal
Stages). Deluxe alone does not need it: it carries the renderer inside its own
NRO and reuses this host only when a compatible one is present.

- Download **`libimgui_smash.nro`** from [v1.0.1](https://git.syntaxbyexample.com/intpa/imgui-smash/releases/tag/v1.0.1).
- Place it in `sd:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/`.
- Replace an existing copy rather than installing a second host; fully restart Smash.

The `.a` archive is for developers, not the SD card. With no plugin using the host,
nothing visible happens in game; its demo window is intentionally disabled.

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

`build.rs` refuses an archive whose size or SHA-256 differs from its pin.
The `clippy` action checks the **host** target; Skyline checks and the four C++
tests are listed in [DEVELOPMENT.md](docs/DEVELOPMENT.md#checks).
Diagnostic features `frame-guard-stats` and `draw-stats` are off in releases.
The old Makefile/bind.sh flow was replaced by this pinned build.

## Credits

Backend license: [GPL v2](imgui-smash/LICENSE). Upstream and vendored licenses
remain in their source directories.

- [Coolsonickirby](https://github.com/Coolsonickirby): modified imgui-xeno to work with Smash and created the host plugin
- [blujay](https://github.com/blu-dev): Input Visualizer mod, the reference for the NVN rendering implementation
- [imgui](https://github.com/ocornut/imgui), [cimgui](https://github.com/cimgui/cimgui), [bindgen](https://github.com/rust-lang/rust-bindgen)
- Original base: [CraftyBoss](https://github.com/CraftyBoss/MP1R-Exlaunch-Base), [exlaunch](https://github.com/shadowninja108/exlaunch/), [exlaunch-cmake](https://github.com/EngineLessCC/exlaunch-cmake/), [BDSP](https://github.com/Martmists-GH/BDSP), [Sanae](https://github.com/Sanae6), [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere), [oss-rtld](https://github.com/Thog/oss-rtld), [roccodev](https://github.com/roccodev), [BlockBuilder57](https://github.com/BlockBuilder57), [AlexCSDev](https://github.com/AlexCSDev)
