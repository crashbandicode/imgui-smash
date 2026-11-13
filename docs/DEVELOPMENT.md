# Developer guide

## Choose the interface

For toggles, modes and status cells in Online Deluxe's window, use the
[Deluxe SDK](https://git.syntaxbyexample.com/intpa/ssbu-online-deluxe-rewrite/src/branch/main/sdk/README.md).
It does not require clients to draw ImGui or install this standalone host.

For a separate window, the host publishes callbacks and its context. Consumers
must use bindings and compiled ImGui code matching **1.92.7 WIP (19263)**, pinned
here to `2b415e642b002f4312ac02f6bfa955960f8bd3b6`. Do not treat an arbitrary
newest `imgui-api`/ImGui checkout as ABI-compatible. The
[upstream callback example](https://gist.github.com/Coolsonickirby/89f2a895b0e06177cbbfd21523b0ac3e)
illustrates the registration pattern; select and verify matching bindings for
your consumer.

The standalone's [Rust glue](../src/lib.rs) preserves these upstream exports:

- `imgui_smash_add_on_pre_init_wrapper`
- `imgui_smash_add_on_new_frame_wrapper`
- `imgui_smash_add_on_draw_frame_wrapper`
- `imgui_smash_show_mouse_wrapper`
- `imgui_setup_context_export`
- `imgui_get_context_export`
- `imgui_show_demo_window_wrapper` — retained as a no-op.

Register context setup during plugin initialization; set the consumer's current
context to the pointer supplied by the host. Draw from the host's frame callback,
balance Begin/End and Push/Pop, and do not start another renderer for a borrowed
context. `ig*` calls are not a dynamically exported API promised by this host:
consumers may link matching cimgui/ImGui code from the static archive.

For that static linkage, put `libimgui_smash.a` in your crate's `lib/` directory:

```rust
// build.rs
fn main() {
    let root = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    println!("cargo:rerun-if-changed={root}/lib/libimgui_smash.a");
    println!("cargo:rustc-link-search=native={root}/lib");
}
```

```rust
#[link(name = "imgui_smash", kind = "static")]
unsafe extern "C" {}
```

The explicit static link avoids accidentally choosing a stale same-named `.so`.
The host itself uses `-Bsymbolic-functions` and a uniquely prefixed bootstrap hook.

## Build the pinned pair

Use Linux, PowerShell 7, Git, Docker and the Skyline Rust toolchain. The root
`rust-toolchain.toml` supplies the host Rust pin; `cargo skyline` uses its target
toolchain/standard library. The build requires `/usr/bin/sha256sum`.

```powershell
git submodule update --init --recursive
pwsh -NoLogo -NoProfile -NonInteractive -File ./Build-Standalone.ps1 -Action archive
pwsh -NoLogo -NoProfile -NonInteractive -File ./Build-Standalone.ps1 -Action skyline
```

The archive action uses devkitA64 image
`devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282`,
two CPUs, 2 GiB, no container network, and deterministic `ar`/`ranlib` flags.
It writes `lib/cmake-build/libimgui_smash.a`; Rust writes
`target/aarch64-skyline-switch/release/libimgui_smash.nro` and the unstripped `.so`.
Where Docker is not available, rootless Podman with `--userns=keep-id` runs the
same image; 1.1.0's archive was built that way, through a `docker` wrapper on
`PATH` that adds the flag.

The pinned archive (1.1.0) is **14,820,452 bytes**, SHA-256
`c1e4471b354565ab97b2c8d5b5698a70f774a991c540fce7fc12511fb42c0d88`. It is
built from the same source Online Deluxe 0.10.4 links (`9405a49`), plus
1.0.2's texture-table guard (`97ca947`) and navigation toggle (`c27ab7e`).
`build.rs` checks both before linking and never downloads it. Host-only builds
do not require the archive. The script's Cargo operations are offline; populate
the pinned dependencies and Docker image before using it offline.
`python3 -I tools/deluxe_parity.py <Online Deluxe checkout>` compares this pin
with Deluxe's `patches/imgui-smash/archive-pin.json` and `build.rs`.

## Checks

Run from the repository root (one Cargo invocation at a time):

```powershell
cargo test
cargo clippy --all-targets -- -D warnings
cargo build --no-default-features
$env:IMGUI_SMASH_ARTIFACT_DIR = "$PWD/lib/cmake-build"
cargo skyline clippy --features frame-guard-stats,draw-stats -- -- -D warnings
cargo skyline build --release --no-default-features
```

The Rust host configuration excludes the renderer; its successful build is not
a renderer test. The C++ host tests build with the host g++ (C++23, warnings
are errors for this repository's code) and write executables outside the
repository. `run.py` builds and runs all nine: `command_memory_reserve`,
`draw_slot_rules`, `frame_guard`, `frame_guard_concurrent`, `texture_rules`,
`font_setup`, `paged_index` (these two also build Dear ImGui, patched and
unpatched), `nav_toggle` and `overlay_texture_policy`:

```powershell
python3 -I imgui-smash/host-tests/run.py
python3 -I tools/test_deluxe_parity.py
```

The full inherited native backend build has existing warnings; this is distinct
from the warnings-denied focused tests and Rust Clippy gates.

## Diagnostics and limits

- `frame-guard-stats` and `draw-stats` add diagnostic writers; neither is enabled
  in the release NRO. Build diagnostic artifacts separately.
- Demo/debug tooling and bundled Proggy fonts are compiled out. The backend
  supplies JetBrains Mono NL's ASCII subset and, since 1.1.0,
  merges the console's shared fonts after it (`font_setup.cpp`,
  `shared_fonts.cpp`); calling `AddFontDefault` is not supported by this
  configuration. An ImGui assertion can abort the game.
- The backend's InputHelper reads FullKey, Handheld and JoyDual pads, not
  GameCube controllers. Consumers such as Deluxe can poll their own input.
- Gamepad navigation (L3 + R3) is off unless `sd:/ultimate/imgui_smash/config.ini`
  sets `gamepad_nav_toggle = on` (since 1.0.2, first released in 1.1.0). The standalone reads the file in its
  `main` and calls `imgui_smash_set_gamepad_nav_toggle` before
  `imgui_smash_init`; another host linking the archive gets the same default
  and can call it too.
- Slots are reused only after a nonblocking completion check. Busy/overflowed
  draws can be skipped; these bounded budgets do not promise arbitrary UI sizes.

See [validation and provenance](VALIDATION.md) for the existing runtime evidence.
