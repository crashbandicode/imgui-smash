# Validation and provenance

## Release line

**v1.0.1** merges the previously published `deluxe` renderer into `main` and
refreshes its documentation. The Rust crate already used version 1.0.1 on that
branch. This release retains the runtime sources of `deluxe-0.3.9`
(`77cdab71985f200ce7a0de027db7a00be80e4507`); it is not another renderer redesign.
Older `1.0.0`, `v1.0.0` and `deluxe-0.3.9` tags remain historical releases.

Source lineage:

| Source | Identity |
| --- | --- |
| Coolsonickirby/imgui-smash upstream base | `bb47b205ed0be2fb864269ee2d37fc886b81221e` |
| Dear ImGui submodule | `2b415e642b002f4312ac02f6bfa955960f8bd3b6` — 1.92.7 WIP (19263) |
| Renderer correction | `ae77450` — frame guard, per-slot storage and fence checks |
| Standalone wrapper | `8ef779e`, `7f0e47c` — prefixed hook, pinned static link |
| Demo/debug/default-font trims | `b923d90`, `ef98524`, `5ec9928` |
| Draw-slot budgets | `c185303` — 64 KiB command + 16 KiB control per slot |
| Archive pin | `77cdab7` — `09b296a0…` |

The static archive is 13,851,844 bytes, SHA-256
`09b296a0f5c67641d229bb4a6d8b170f75281368da3957d666af424476bc2731`.
The historical default-feature standalone from `deluxe-0.3.9` is 888,832 bytes,
SHA-256 `946de84f98546cb791640e40fbf5848ea3965c511631ef18b32e5c805b67d50e`.
The **v1.0.1 manifest** records the newly packaged NRO, source commit, archive,
submodule and build checks; use it for the release's exact identity.

## Recorded runtime checks

The October 14 Eden campaign exercised the reduced-budget renderer in three
configurations: standalone with Deluxe and Elite, Deluxe's embedded host, and
reversed standalone load order. The external diagnostic run recorded 29,658
submissions, no overflow/failed/pending skips and maxima 1,448 command bytes /
120 control bytes. The embedded run recorded 13,201 submissions, no overflow,
and maxima 1,040 / 120 bytes. The reversed-order run was clean. These counts belong
to those diagnostic fixtures, not to a new v1.0.1 run or a performance benchmark.

The standalone was installed on the Switch SD with readback verification on
October 14. Installation alone was not a console runtime test. On October 25 the
operator reported the Deluxe/FX stack working without issues in Eden and on an
Erista Switch; that broad report did not identify a standalone ImGui binary or
repeat the complete host/load-order matrix.

Detailed shared evidence is recorded in Online Deluxe's
[validation ledger](https://git.syntaxbyexample.com/intpa/ssbu-online-deluxe-rewrite/src/branch/main/docs/VALIDATION.md)
and [ImGui dependency record](https://git.syntaxbyexample.com/intpa/ssbu-online-deluxe-rewrite/src/branch/main/docs/IMGUI_DEPENDENCY.md).
Historical records retain their original candidate identities and dates.

## Compatibility and scope

The integration target is Smash Ultimate 13.0.5. Latest runtime identities checked
on 2026-10-25 are Switch firmware 23.0.1, Atmosphère 1.12.1 and Skyline beta
`2eb226fa9e4ab023dc00cb0db10ad4ec50ac4fa3`; authoritative links are in the
[README](../README.md). These version references are not additional runtime tests.
Skyline's `beta` runtime tag is distinct from the Rust `skyline` crate version.

Frame ownership and storage-lifetime defects were identified in source and
corrected. Earlier visibility loss was intermittent: passing the recorded
transitions does not establish that every possible invisible-menu symptom has
the same cause. No FPS/input-latency improvement or universal plugin compatibility
is claimed. Arbitrary callbacks, other ImGui ABIs and untested game versions are
outside the recorded checks.

## Reproducing a release

Use the pinned submodule/image and [build/check commands](DEVELOPMENT.md).
Release packages include the NRO, static archive and SHA-256 manifest. Optimized
debug symbols and full source snapshots are retained outside Git. Source remains
available through the release tag and pinned submodule; compiled dependencies
are not committed to the repository.
