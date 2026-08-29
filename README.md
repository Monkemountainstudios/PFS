# PFS — Probabilistic Fractal Sequencer

PFS is a two-track generative sample instrument. This repository contains both:

- the original browser version published through GitHub Pages; and
- a native JUCE plugin for Windows and macOS (VST3, Audio Unit on macOS, and standalone).

The plugin follows the DAW's tempo and transport automatically. Its **PLAY** button provides an internal clock when the DAW is stopped or when using the standalone app. PFS accepts and passes through ordinary MIDI, follows incoming MIDI Clock/Start/Stop, emits 24-PPQN MIDI clock from its internal transport, and sends generated notes on channels 1 and 2. This allows the standalone app to lead or follow external hardware through the MIDI ports selected in its audio/MIDI settings.

## Plugin controls

- Click a tree node to enable or disable it.
- Scroll over a node to change its pitch (C2–C6).
- The large illuminated `1` and `2` buttons show or hide each tree. A hidden track with active nodes flashes on quarter notes.
- **VARIATION** chooses a random left/right branch; disabling it always follows the left branch.
- Track rates are `1`, `1/2`, and `1/4` of the base sixteenth-note clock.
- Ratchet probability, repeat count, fade, and per-track routing mirror the web version.
- **FUAP!** substitutes `audio/fuap.ogg` and lets the complete sample play.
- Sample, filter, gate, volume, pan, reverb, mute, and swing mirror the web version.
- Each track has its own `-12`, `-1`, `+1`, and `+12` transpose controls.

All controls and all 62 node states are saved with the DAW project.

## Downloading automatic builds

Open the repository's **Actions** tab, choose a successful **Build PFS plugins** run, and download either `pfs-windows-x64` or `pfs-macos-universal` from its Artifacts section. Artifacts from ordinary workflow runs are unsigned development builds.

Install locations:

- Windows VST3: `C:\Program Files\Common Files\VST3`
- macOS VST3: `~/Library/Audio/Plug-Ins/VST3`
- macOS Audio Unit: `~/Library/Audio/Plug-Ins/Components`

Rescan plugins in the DAW after installation. macOS may quarantine an unsigned build; public distribution should use an Apple Developer ID, hardened-runtime signing, and notarization.

In the standalone app, use **Options** to choose the Windows Audio device type, output device, sample rate, buffer size, and MIDI input/output ports. PFS accepts both mono and stereo output-device layouts.

## Building locally

Requirements: Git, CMake 3.22+, and either Visual Studio 2022 (Windows) or Xcode (macOS). CMake downloads the pinned JUCE dependency during configuration.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Build products are written below `build/PFS_artefacts/Release/`.

## Source layout

- `Source/SequencerEngine.h` — framework-independent five-level traversal engine
- `Source/PluginProcessor.*` — sample scheduling, audio/MIDI, DAW sync, and saved state
- `Source/PluginEditor.*` — resizable native interface
- `Tests/` — deterministic sequencer tests
- `.github/workflows/build-plugins.yml` — Windows x64 and macOS universal CI builds
- `index.html`, `program.js`, `style.css` — existing web version

JUCE is fetched at the version pinned in `CMakeLists.txt`. Review JUCE's current licensing terms before distributing the plugin commercially.
