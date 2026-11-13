# imgui-smash: A modified fork of imgui-xeno to get Dear ImGui backend working for Smash

> Upstream's original README, kept for its credits. This fork's pinned build, checks and changes are in the
> [repository README](../README.md) and [docs/DEVELOPMENT.md](../docs/DEVELOPMENT.md).

A [Dear ImGui](https://github.com/ocornut/imgui) backend that leverages NVN to support and target Super Smash Bros. Ultimate specifically (will probably also work with other games that imgui-xeno doesn't work with.)

## Main Credits

This fork is created from the work of [imgui-xeno](https://github.com/roccodev/imgui-xeno), which was worked on by [roccodev](https://github.com/roccodev), [BlockBuilder57](https://github.com/BlockBuilder57), and [AlexCSDev](https://github.com/AlexCSDev).

This implementation is a fork of [mp1r-practice-mod](https://github.com/MetroidPrimeModding/mp1r-practice-mod).   
In particular, it extracts [CraftyBoss](https://github.com/CraftyBoss)'s original NVN backend for use in other games.

It also includes some workarounds from the [Super Mario Odyssey adaptation](https://github.com/Amethyst-szs/smo-lunakit), 
made by [Amethyst-szs](https://github.com/Amethyst-szs).

The implementation of putting the rendered data on screen was based off of [blujay](https://github.com/blu-dev)'s [Input Visualizer mod](https://gamebanana.com/mods/379280).

## Building

You should only be building this if you're going to make any modifications directly to this source code or the imgui code. If you want to use this for creating windows in-game, download and use the shared main plugin that loads the library.

To get the library, install CMake, then run
```
make
```

The shared library (`libimgui_smash.a`) can be found in the `cmake-build-minsizerel` directory.

## API usage

To use this library, follow the guide in the README.md up one folder.

## Configuration

Most parameters can be configured in the `user_config` source directory.  
Particularly, you can add ImGui directives in the `imgui_user_config.h` file.

## Original repo credits

- [CraftyBoss](https://github.com/CraftyBoss/MP1R-Exlaunch-Base) - thanks for the base <3
- [exlaunch](https://github.com/shadowninja108/exlaunch/)
- [exlaunch-cmake](https://github.com/EngineLessCC/exlaunch-cmake/)
- [BDSP](https://github.com/Martmists-GH/BDSP)
- [Sanae](https://github.com/Sanae6)
- [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)
- [oss-rtld](https://github.com/Thog/oss-rtld)
- [roccodev](https://github.com/roccodev)
- [BlockBuilder57](https://github.com/BlockBuilder57)
- [AlexCSDev](https://github.com/AlexCSDev)