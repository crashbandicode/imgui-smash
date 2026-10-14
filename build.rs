use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

/// Digest of the historical prebuilt static archive. This harness records the
/// archive it was given and refuses that prebuilt file.
const PREBUILT_NOT_USED: &str = "0042ff0592313228b0746ee9214091249f7c70a1797e1826d990c27ca2fe0e45";

/// Static archive built from this repository's imgui-smash/ by
/// `Build-Standalone.ps1 -Action archive` (renderer correction; demo, debug
/// tools and default fonts off). Any other file is refused.
const PINNED_SHA256: &str = "8a06be16dbfd50d10bac81ee5bdd2ffa9cd3472773151cc45f1c2101541f8f7c";
const PINNED_SIZE: u64 = 13_851_556;

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
