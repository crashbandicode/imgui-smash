use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

/// Digest of the historical prebuilt static archive. This harness records the
/// archive it was given and refuses that prebuilt file.
const PREBUILT_NOT_USED: &str = "0042ff0592313228b0746ee9214091249f7c70a1797e1826d990c27ca2fe0e45";

/// Static archive built from this repository's imgui-smash/ by
/// `Build-Standalone.ps1 -Action archive` (renderer correction; demo, debug
/// tools and default fonts off; 64 KiB + 16 KiB draw-slot budgets; the
/// three-slot swapchain texture table, overlay_texture_policy.hpp; the L3 + R3
/// navigation toggle off unless config.ini enables it; 1.1.0: the shared
/// fonts, the static and dynamic glyph sets and the opt-in paged codepoint
/// index). Its source is the one Online Deluxe 0.10.4 links (9405a49) plus
/// 1.0.2's two fixes; `tools/deluxe_parity.py` compares the pins. Any other
/// file is refused.
const PINNED_SHA256: &str = "c1e4471b354565ab97b2c8d5b5698a70f774a991c540fce7fc12511fb42c0d88";
const PINNED_SIZE: u64 = 14_820_452;

fn main() {
    println!("cargo:rerun-if-env-changed=IMGUI_SMASH_ARTIFACT_DIR");
    if env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("switch") {
        return;
    }

    // aarch64-skyline-switch links with ld.lld. Bind this NRO's references to
    // its own global functions. Undefined NN/skyline imports stay dynamic.
    println!("cargo:rustc-cdylib-link-arg=-Bsymbolic-functions");

    let dir = env::var("IMGUI_SMASH_ARTIFACT_DIR").unwrap_or_else(|_| {
        panic!(
            "IMGUI_SMASH_ARTIFACT_DIR must be a directory containing libimgui_smash.a; build.rs does not download an archive"
        )
    });
    let lib = PathBuf::from(&dir).join("libimgui_smash.a");
    println!("cargo:rerun-if-changed={}", lib.display());

    let metadata = fs::metadata(&lib).unwrap_or_else(|err| {
        panic!("cannot read {}: {err}", lib.display())
    });
    if !metadata.is_file() {
        panic!("{} is not a file", lib.display());
    }

    let hashed = Command::new("/usr/bin/sha256sum")
        .arg(&lib)
        .output()
        .unwrap_or_else(|err| panic!("sha256sum failed to start: {err}"));
    if !hashed.status.success() {
        panic!(
            "sha256sum exited {}: {}",
            hashed.status,
            String::from_utf8_lossy(&hashed.stderr)
        );
    }
    let stdout = String::from_utf8_lossy(&hashed.stdout);
    let digest = stdout
        .split_whitespace()
        .next()
        .unwrap_or("")
        .to_ascii_lowercase();
    if digest.len() != 64 || !digest.chars().all(|ch| ch.is_ascii_hexdigit()) {
        panic!("sha256sum returned an unexpected digest: {stdout}");
    }
    if digest == PREBUILT_NOT_USED {
        panic!("refusing prebuilt static archive {PREBUILT_NOT_USED}");
    }
    if metadata.len() != PINNED_SIZE || digest != PINNED_SHA256 {
        panic!(
            "refusing archive size {} digest {digest}; pinned size {PINNED_SIZE} digest {PINNED_SHA256}",
            metadata.len()
        );
    }

    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("Cargo OUT_DIR"));
    let record = format!(
        "path={}\nsize={}\nsha256={digest}\n",
        lib.display(),
        metadata.len()
    );
    fs::write(out_dir.join("imgui_smash_archive.txt"), record).expect("record archive hash");
    println!("cargo:rustc-env=IMGUI_SMASH_ARCHIVE_SHA256={digest}");
    println!("cargo:rustc-env=IMGUI_SMASH_ARCHIVE_SIZE={}", metadata.len());
    println!("cargo:rustc-link-search=native={dir}");
}
