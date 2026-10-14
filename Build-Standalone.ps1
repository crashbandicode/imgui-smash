param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("archive", "clippy", "build", "skyline")]
    [string]$Action,
    # Directory holding libimgui_smash.a. The archive action writes it here.
    [string]$ArtifactDir = (Join-Path $PSScriptRoot "lib/cmake-build"),
    [switch]$FrameGuardStats
)

# archive: pinned devkitA64 image, no network, deterministic ar/ranlib rules
#          (deterministic-archive.cmake). Builds imgui-smash/ into lib/cmake-build.
# skyline: cargo skyline build --release. build.rs refuses any archive whose
#          size and SHA-256 differ from its pin.

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$image = "devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282"
$env:PATH = (Join-Path $env:HOME ".cargo/bin") + [IO.Path]::PathSeparator + $env:PATH
$env:CARGO_TARGET_DIR = Join-Path $root "target"
$env:CARGO_NET_OFFLINE = "true"
$env:CARGO_BUILD_JOBS = "2"
$env:IMGUI_SMASH_ARTIFACT_DIR = $ArtifactDir
Set-Location -LiteralPath $root

Write-Output "action=$Action"
Write-Output "artifactdir=$ArtifactDir"
Write-Output "targetdir=$env:CARGO_TARGET_DIR"
Write-Output "frame_guard_stats=$FrameGuardStats"

function Invoke-DevkitCmake([string[]]$CmakeArgs) {
    $user = "{0}:{1}" -f (& id -u), (& id -g)
    & docker run --rm --user $user --network none --memory 2g --cpus 2 `
        --entrypoint /usr/bin/cmake -v "${root}:/work" -w /work $image @CmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake in devkit image failed: $LASTEXITCODE" }
}

switch ($Action) {
    "archive" {
        if ($ArtifactDir -ne (Join-Path $root "lib/cmake-build")) {
            throw "the archive action always builds into lib/cmake-build"
        }
        Invoke-DevkitCmake @("-C", "/work/deterministic-archive.cmake",
            "-DCMAKE_TOOLCHAIN_FILE=/work/imgui-smash/cmake/toolchain.cmake",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
            "/work/imgui-smash", "-B", "/work/lib/cmake-build")
        Invoke-DevkitCmake @("--build", "/work/lib/cmake-build", "-j", "2")
        Get-FileHash -Algorithm SHA256 (Join-Path $ArtifactDir "libimgui_smash.a") | Format-List
        return
    }
    "clippy" {
        if ($FrameGuardStats) {
            & cargo clippy --all-targets --offline --features frame-guard-stats -- -D warnings
        } else {
            & cargo clippy --all-targets --offline -- -D warnings
        }
    }
    "build" {
        if ($FrameGuardStats) {
            throw "host build stays --no-default-features"
        }
        & cargo build --offline --no-default-features
    }
    "skyline" {
        if ($FrameGuardStats) {
            & cargo skyline build --release --features frame-guard-stats
        } else {
            & cargo skyline build --release
        }
    }
}
if ($LASTEXITCODE -ne 0) { throw "cargo $Action failed: $LASTEXITCODE" }
