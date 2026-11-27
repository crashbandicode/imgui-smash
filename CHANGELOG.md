# Release notes

## v1.3.0

**imgui-smash v1.3.0 · 2026-11-25** — An overlay host for players whose plugins draw ImGui windows.

### What you need

| Thing | Upstream | Our fork | What the fork changes |
| --- | --- | --- | --- |
| Switch firmware | [latest](https://en-americas-support.nintendo.com/app/answers/detail/a_id/22525) | — | — |
| Atmosphère | [latest](https://github.com/Atmosphere-NX/Atmosphere/releases/latest) | [Atmosphère fork](https://git.syntaxbyexample.com/intpa/atmosphere-ro-cleanup-p1) | Cleans up after a failed module load. |
| Super Smash Bros. Ultimate | 13.0.5 | — | — |
| Skyline | [latest](https://github.com/skyline-dev/skyline/releases/latest) | [Skyline fork](https://git.syntaxbyexample.com/intpa/skyline) | Protects the logging queue when multiple threads write. |

### Install

1. Fully close Smash. Open the [downloads for v1.3.0](https://git.syntaxbyexample.com/intpa/imgui-smash/releases/tag/v1.3.0).
2. Copy `libimgui_smash.nro` to `sd:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libimgui_smash.nro`.
3. Developers can link `libimgui_smash.a` on their computer. Keep `SHA256SUMS.txt`, `manifest.json` on your computer for download verification. Restart Smash.

**Update:** Replace the existing NRO in the same folder and restart Smash. Keep your navigation settings.

**Uninstall:** Close Smash, remove the NRO from that folder, and restart Smash.

### What changed

| Feature | Change |
| --- | --- |
| [Shared fonts](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.3.0/README.md) | Names in different scripts now share one text size. |
| [Font loading](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.3.0/README.md) | Drawing waits for text fonts and adds later fonts between frames. |
| [Missing characters](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.3.0/README.md) | Previously refused characters can draw after the font refreshes. |

### Tested on

| Feature | Hardware | Eden |
| --- | --- | --- |
| Shared fonts | no | yes (embedded) |
| Font loading | no | no |
| Missing characters | no | no |

“No” means the release records no test of that feature on that platform.

Eden ran the renderer embedded in Online Deluxe. The standalone host has no recorded gameplay test.

### Known limits

- Online Deluxe includes its own renderer. Install the standalone host only for plugins that need it. [Details](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.3.0/README.md).

## v1.2.0

**imgui-smash v1.2.0 · 2026-11-13** — An overlay host for players whose plugins draw ImGui windows.

### What you need

| Thing | Upstream | Our fork | What the fork changes |
| --- | --- | --- | --- |
| Switch firmware | [latest](https://en-americas-support.nintendo.com/app/answers/detail/a_id/22525) | — | — |
| Atmosphère | [latest](https://github.com/Atmosphere-NX/Atmosphere/releases/latest) | [Atmosphère fork](https://git.syntaxbyexample.com/intpa/atmosphere-ro-cleanup-p1) | Cleans up after a failed module load. |
| Super Smash Bros. Ultimate | 13.0.5 | — | — |
| Skyline | [latest](https://github.com/skyline-dev/skyline/releases/latest) | [Skyline fork](https://git.syntaxbyexample.com/intpa/skyline) | Protects the logging queue when multiple threads write. |

### Install

1. Fully close Smash. Open the [downloads for v1.2.0](https://git.syntaxbyexample.com/intpa/imgui-smash/releases/tag/v1.2.0).
2. Copy `libimgui_smash.nro` to `sd:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libimgui_smash.nro`.
3. Developers can link `libimgui_smash.a` on their computer. Keep `SHA256SUMS.txt`, `manifest.json` on your computer for download verification. Restart Smash.

**Update:** Replace the existing NRO in the same folder and restart Smash. Keep your navigation settings.

**Uninstall:** Close Smash, remove the NRO from that folder, and restart Smash.

### What changed

| Feature | Change |
| --- | --- |
| [Player names](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.2.0/README.md) | Displays accented Latin, Japanese, Korean and Chinese names. |
| [Font memory](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.2.0/README.md) | Uses a smaller character index when every connected plugin supports it. |
| [Overlay diagnostics](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.2.0/README.md) | Plugins can read font and renderer counters. |
| [Navigation](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.2.0/README.md) | L3 + R3 navigation requires opting in. |
| [Drawing](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.2.0/README.md) | Skips unavailable display textures. |

### Tested on

| Feature | Hardware | Eden |
| --- | --- | --- |
| Player names | no | yes |
| Font memory | no | yes |
| Overlay diagnostics | no | yes |
| Navigation | no | no |
| Drawing | no | no |

“No” means the release records no test of that feature on that platform.

### Known limits

- Online Deluxe includes its own renderer. Install the standalone host only for plugins that need it. [Details](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.2.0/README.md).

## v1.0.1

**imgui-smash v1.0.1 · 2026-10-24** — An overlay host for players whose plugins draw ImGui windows.

### What you need

| Thing | Upstream | Our fork | What the fork changes |
| --- | --- | --- | --- |
| Switch firmware | [23.0.1](https://en-americas-support.nintendo.com/app/answers/detail/a_id/22525) | — | — |
| Atmosphère | [1.12.1](https://github.com/Atmosphere-NX/Atmosphere/releases/latest) | [Atmosphère fork](https://git.syntaxbyexample.com/intpa/atmosphere-ro-cleanup-p1) | Cleans up after a failed module load. |
| Super Smash Bros. Ultimate | 13.0.5 | — | — |
| Skyline | [beta](https://github.com/skyline-dev/skyline/releases/latest) | [Skyline fork](https://git.syntaxbyexample.com/intpa/skyline) | Protects the logging queue when multiple threads write. |

### Install

1. Fully close Smash. Open the [downloads for v1.0.1](https://git.syntaxbyexample.com/intpa/imgui-smash/releases/tag/v1.0.1).
2. Copy `libimgui_smash.nro` to `sd:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libimgui_smash.nro`.
3. Developers can link `libimgui_smash.a` on their computer. Keep `manifest.json` on your computer for download verification. Restart Smash.

**Update:** Replace the existing NRO in the same folder and restart Smash.

**Uninstall:** Close Smash, remove the NRO from that folder, and restart Smash.

### What changed

| Feature | Change |
| --- | --- |
| [Overlay renderer](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.0.1/README.md) | Packages the Deluxe renderer and smaller drawing buffers for standalone plugins. |

### Tested on

| Feature | Hardware | Eden |
| --- | --- | --- |
| Overlay renderer | no | no |

“No” means the release records no test of that feature on that platform.

### Known limits

- Online Deluxe includes its own renderer. Install the standalone host only for plugins that need it. [Details](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/v1.0.1/README.md).

## deluxe-0.3.9

**imgui-smash deluxe-0.3.9 · 2026-10-14** — An overlay host for players whose plugins draw ImGui windows.

### What you need

| Thing | Upstream | Our fork | What the fork changes |
| --- | --- | --- | --- |
| Switch firmware | [latest](https://en-americas-support.nintendo.com/app/answers/detail/a_id/22525) | — | — |
| Atmosphère | [latest](https://github.com/Atmosphere-NX/Atmosphere/releases/latest) | [Atmosphère fork](https://git.syntaxbyexample.com/intpa/atmosphere-ro-cleanup-p1) | Cleans up after a failed module load. |
| Super Smash Bros. Ultimate | 13.0.5 | — | — |
| Skyline | [latest](https://github.com/skyline-dev/skyline/releases/latest) | [Skyline fork](https://git.syntaxbyexample.com/intpa/skyline) | Protects the logging queue when multiple threads write. |

### Install

1. Fully close Smash. Open the [downloads for deluxe-0.3.9](https://git.syntaxbyexample.com/intpa/imgui-smash/releases/tag/deluxe-0.3.9).
2. Copy `libimgui_smash.nro` to `sd:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libimgui_smash.nro`.
3. Developers can link `libimgui_smash.a` on their computer. Restart Smash.

**Update:** Replace the existing NRO in the same folder and restart Smash.

**Uninstall:** Close Smash, remove the NRO from that folder, and restart Smash.

### What changed

| Feature | Change |
| --- | --- |
| [Overlay memory](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/deluxe-0.3.9/README.md) | Cuts drawing memory from 5 MiB to 640 KiB. |

### Tested on

| Feature | Hardware | Eden |
| --- | --- | --- |
| Overlay memory | no | yes (Deluxe) |

### Known limits

- Online Deluxe includes its own renderer. Install the standalone host only for plugins that need it. [Details](https://git.syntaxbyexample.com/intpa/imgui-smash/src/tag/deluxe-0.3.9/README.md).

The unpublished 1.0.2 and 1.1.0 changes shipped in 1.2.0.
