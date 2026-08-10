# ReaSolotus

* Solo bus extension to improve live mixing experience with REAPER.
* Uses discrete Mix and Solo buses (tracks) to achieve non-destructive soloing (uninterrupted output).
* Also known as Cue / PFL / Listen.
* Works by capturing solo commands and managing track routing automatically.
* 'Hijacks' tracks routed to Main / Master, and reroutes them through Mix and/or Solo bus when soloed/unsoloed.
* Works in tandem with control surfaces, mouse, and keyboard shortcuts.
* Folder tracks (parent/child structure) and are currently not supported.
* WARNING: Flattens existings parent/child structures and replaces parent send with ordinary send.
* Manual routing (e.g. grouping) can be done with sends.
* WARNING: changes REAPER default solo mode to 'ignore-routing'.
* REAPER Master track is used as control room / headphones output.
* Mix track is main / program / PA / stream / tape output. Freely assignable to any hardware output(s).
* Creating separate live mixing projects is recommended.
* Projects need to be initialized with ReaSolotus Init Actions.
* Tracks are soloed as PFL (post-fx).
* If no tracks are soloed then Mix is monitored.

![image](https://i.imgur.com/jSFMUHN.gif)

## Build

Dependencies are fetched with CMake `FetchContent` on first configure and cached under `.deps` at the repository root. Deleting `build/` no longer forces WDL or `reaper-sdk` to be redownloaded.

This project now defaults to CMake 4.

### Local configure

```powershell
cmake -S . -B build
cmake --build build
```

### Windows presets

The repository includes Windows Ninja presets in [CMakePresets.json](CMakePresets.json). They expect CMake and Ninja to be installed.

```powershell
cmake --preset windows-msvc-ninja-debug
cmake --build --preset windows-msvc-ninja-debug
```

### CI targets

The GitHub Actions matrix currently targets:

* Windows x64
* Windows arm64ec
* macOS arm64
* Linux x86_64
* Linux aarch64

## About

Each track gets assigned with a (muted) send to Solo bus. When track is soloed it's listened through this send. Perhaps 'monitoring bus' would be technically more accurate term. Similarly all top level tracks get assigned to Mix bus, which could also be thought of as a 'program out'. This is the routing which ReaSolotus manages automatically resulting in uninterrupted output while being able to monitor individual tracks, and this is also how most live mixing consoles are usually operated.
