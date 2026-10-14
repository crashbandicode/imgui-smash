# imgui-smash (`deluxe` branch)

> **Disclaimer:** the `deluxe` branch changes were developed with the
> assistance of AI (Claude, Codex and other coding agents) under the
> operator's direction and review, and tested as recorded in Online Deluxe's
> `docs/VALIDATION.md` and `docs/IMGUI_DEPENDENCY.md`.

A [Dear ImGui](https://github.com/ocornut/imgui) backend on NVN for
**Super Smash Bros. Ultimate**, forked from
[Coolsonickirby/imgui-smash](https://github.com/Coolsonickirby/imgui-smash)
(`main` here is upstream `bb47b205`). It is the host plugin whose exported
functions let other Skyline plugins draw their own ImGui windows, and it is
the renderer that [Online Deluxe](https://git.syntaxbyexample.com/intpa/ssbu-online-deluxe-rewrite)
links statically. Dear ImGui 1.92.7 WIP.

## What the `deluxe` branch changes

- **Renderer correction.** Per-slot command and control storage with fence
  checks and a frame guard, so a frame that is still in flight is never
  overwritten (the double-image and flicker fix measured in Deluxe's
  `docs/VALIDATION.md`).
- **Smaller budgets.** One 64 KiB command page and 16 KiB of control memory
  per draw slot; the unused default fonts, the demo window and the debug tools
  are compiled out.
- **Standalone companion crate.** No `imgui-api` dependency; `build.rs` links
  only an archive whose size and SHA-256 match its pin, and the archive is
  built deterministically in a pinned devkitA64 image
  (`deterministic-archive.cmake`).
- **Releases carry both artifacts.** `libimgui_smash.nro` (the standalone
  host) and `libimgui_smash.a` (the archive Deluxe links). The pinned pair
  for each Deluxe version is recorded in Deluxe's `docs/IMGUI_DEPENDENCY.md`.

## Installing the host (plugin user)

Only needed when another plugin draws its own windows (for example Elite Legal
Stages). Deluxe alone does not need it: it carries the renderer inside its own
NRO and reuses this host only when a compatible one is present.

- Download `libimgui_smash.nro` from the release.
- Place it in `sd:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/`.

With no plugin using it, nothing visible happens in game.

## Plugging in (plugin developer)

Two separate doors:

| You want | Use |
| --- | --- |
| Rows in Online Deluxe's menu (toggles, modes, status) with no UI code | Deluxe's plugin API and Rust SDK: [docs/PLUGINS.md](https://git.syntaxbyexample.com/intpa/ssbu-online-deluxe-rewrite/src/branch/main/docs/PLUGINS.md) |
| Your own Dear ImGui windows | this host, below |

To draw your own windows, link the host's exports and set up the context from
your `main`:

- Download `libimgui_smash.a` from the release into `lib/` in your crate and
  add a `build.rs`:

  ```rust
  fn main() {
      let proj_dir = env!("CARGO_MANIFEST_DIR");
      println!("cargo:rerun-if-changed={}/lib/", proj_dir);
      println!("cargo:rustc-link-search={}/lib/", proj_dir);
  }
  ```

- In `lib.rs`:

  ```rust
  #[link(name = "imgui_smash")]
  extern "C" {}
  ```

- Depend on the bindings and register your context callback:

  ```toml
  imgui-api = { git = "https://github.com/Coolsonickirby/imgui-api" }
  ```

  ```rust
  use imgui_api::bindings::*;

  unsafe extern "C" fn setup_imgui_context(imgui_ctx: *mut u64) {
      igSetCurrentContext(imgui_ctx as _);
  }

  pub fn main() {
      unsafe { imgui_api::imgui_setup_context(setup_imgui_context); }
  }
  ```

The host NRO exports the same symbols as upstream (`src/lib.rs`):
`imgui_smash_add_on_pre_init_wrapper`, `imgui_smash_add_on_new_frame_wrapper`,
`imgui_smash_add_on_draw_frame_wrapper`, `imgui_smash_show_mouse_wrapper`,
`imgui_setup_context_export`, `imgui_get_context_export`, and a no-op
`imgui_show_demo_window_wrapper` (the demo is compiled out). Upstream's
[example gist](https://gist.github.com/Coolsonickirby/89f2a895b0e06177cbbfd21523b0ac3e)
shows a window drawn from the new-frame callback.

## Building

Requirements: Docker (for the pinned devkitA64 image), Rust with
`cargo-skyline` and `cargo skyline update-std`, PowerShell 7.

```text
pwsh ./Build-Standalone.ps1 -Action archive   # deterministic libimgui_smash.a -> lib/cmake-build
pwsh ./Build-Standalone.ps1 -Action clippy    # cargo skyline clippy
pwsh ./Build-Standalone.ps1 -Action skyline   # cargo skyline build --release -> target/aarch64-skyline-switch/release
```

`build.rs` refuses an archive whose size or SHA-256 differs from its pin.
Optional diagnostic features: `frame-guard-stats`, `draw-stats`. The upstream
`main` branch keeps the original Makefile flow.

## Credits

- [Coolsonickirby](https://github.com/Coolsonickirby): modified imgui-xeno to work with Smash and created the host plugin
- [blujay](https://github.com/blu-dev): Input Visualizer mod, the reference for the NVN rendering implementation
- [imgui](https://github.com/ocornut/imgui), [cimgui](https://github.com/cimgui/cimgui), [bindgen](https://github.com/rust-lang/rust-bindgen)
- Original base: [CraftyBoss](https://github.com/CraftyBoss/MP1R-Exlaunch-Base), [exlaunch](https://github.com/shadowninja108/exlaunch/), [exlaunch-cmake](https://github.com/EngineLessCC/exlaunch-cmake/), [BDSP](https://github.com/Martmists-GH/BDSP), [Sanae](https://github.com/Sanae6), [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere), [oss-rtld](https://github.com/Thog/oss-rtld), [roccodev](https://github.com/roccodev), [BlockBuilder57](https://github.com/BlockBuilder57), [AlexCSDev](https://github.com/AlexCSDev)
