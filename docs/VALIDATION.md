# Validation and provenance

## Release line

**1.0.2** (`main`, not yet tagged or released) carries the Switch build's
texture-table guard (below) and puts the L3 + R3 gamepad-navigation toggle
behind `gamepad_nav_toggle` in `sd:/ultimate/imgui_smash/config.ini`, off by
default. Archive 13,884,166 bytes, SHA-256
`7be7d40f4690ffa07c5e4b7cfc5d2a622e5b045766e9802f2f811db284fe0380`. Its host
checks are the Rust `config` tests and `host-tests/nav_toggle_test.cpp`. A
1.0.2 standalone, 892,928 bytes, SHA-256
`f72a6b8d5e94d72ec510e1c82d4b218114e24ea98d9b0e1bc05b5615a0deffe7`, replaced
the Switch's build (below) on the operator's card on 2026-11-12 (card-install
receipt `card-install-rc9/4-imgui`). It reads `imgui_smash/config.ini` and
imports none of the shared-font functions, so its font is ASCII only. No
runtime record with it exists yet.

**On `main` after 1.0.2** (merged 2026-11-13, not released): the font work
from the former `unicode-shared-font` and `sparse-codepoint-index` branches
(`966835c`, `bec80ec`, `9405a49`). Online Deluxe 0.10.4 links an archive
built from `9405a49` (14,788,410 bytes, SHA-256 `26ea2c3b…`); its records,
Eden runs of the fonts included, are in Deluxe's
[ImGui dependency record](https://git.syntaxbyexample.com/intpa/ssbu-online-deluxe-rewrite/src/branch/main/docs/IMGUI_DEPENDENCY.md).
The standalone crate on `main` still pins 1.0.2's archive `7be7d40f…`, built
before the merge, so a standalone built from `main` doesn't have the font work
yet. On 2026-11-13 `python3 -I imgui-smash/host-tests/run.py` passed all seven
tests it runs on `main` (`038f6a5`), and `nav_toggle_test` and
`overlay_texture_policy_test` passed with the commands in
[DEVELOPMENT.md](DEVELOPMENT.md#checks).

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
| Swapchain texture-table guard | the Switch's build of 2026-10-22 — `1237aa46…` |

The v1.0.1 static archive is 13,851,844 bytes, SHA-256
`09b296a0f5c67641d229bb4a6d8b170f75281368da3957d666af424476bc2731`.
The historical default-feature standalone from `deluxe-0.3.9` is 888,832 bytes,
SHA-256 `946de84f98546cb791640e40fbf5848ea3965c511631ef18b32e5c805b67d50e`.
The **v1.0.1 manifest** records the newly packaged NRO, source commit, archive,
submodule and build checks; use it for the release's exact identity.

## The Switch's build (2026-10-22)

The standalone on the Switch SD from 2026-10-22 to 2026-11-12 was not the
published v1.0.1 NRO. It was v1.0.1 with one renderer change, written on 2026-10-22 as a private
memory-safety patch and built locally: `overlay_texture_policy.hpp` caps the
backend's private swapchain texture table at three entries (the old `memcpy`
copied any count into it), keeps the native count and result, and draws the
overlay only after a successful acquire with an index inside that table.

- Archive: 13,861,332 bytes, SHA-256
  `1237aa46888d53eeb1badbca59bb9ac4213d228f84665c0e5ddccd47a775eb1e`.
- NRO: 888,832 bytes, SHA-256
  `d8e987fea04f70f680838a293f2f3834517490e608f8ecc60fbed55fbcc3d5b6`.
- The commit that adds the change to `main` rebuilds both byte-identically
  (the pinned devkitA64 image run by rootless Podman, then `cargo skyline build
  --release`). `host-tests/overlay_texture_policy_test.cpp` checks the policy.

## Recorded runtime checks

The October 14 Eden campaign exercised the reduced-budget renderer in three
configurations: standalone with Deluxe and the original Elite Legal Stages
NRO (it drew its own ImGui menu; its replacement, Legal Stages + Loser
Counterpick, draws none), Deluxe's embedded host, and
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

The integration target is Smash Ultimate 13.0.5. The October 25 report named
Switch firmware 23.0.1, Atmosphère 1.12.1 and Skyline beta
`2eb226fa9e4ab023dc00cb0db10ad4ec50ac4fa3`; the Switch still ran firmware
23.0.1 and Atmosphère 1.12.1 when Online Deluxe 0.10.4 ran there on
2026-11-10, with the Switch's build above on the card. These version
references are not additional tests of this host. Skyline's `beta` runtime
tag is distinct from the Rust `skyline` crate version.

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
