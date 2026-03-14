# 0x808

**Drum machine & synth workstation — C engine, multi-frontend GUI**

![0x808 demo](0x808_demo.gif)

> **[Watch full demo video](https://projects.dcmichael.com/0x808_demo.mp4)**

A standalone drum machine, step sequencer, and synthesizer — also available as a VST3/CLAP plugin and a GTK 4.0 native Linux frontend. 72 bundled drum samples, 50 synth presets, pattern-based arrangement, and offline WAV/MP3 export. All dependencies vendored, zero external runtime dependencies.

## Screenshots

<img src="screenshots/0x808_light_theme.png" width="270"> <img src="screenshots/0x808_dark_theme.png" width="270"> <img src="screenshots/0x808_hacker_theme.png" width="270">

7 built-in themes (Dark, Light, Hacker, Midnight, Amber, Vaporwave, Neon) + user-defined themes via JSON files in the `themes/` folder.

## Features

- **Drum grid** — 16-track step sequencer with velocity, pitch per step, mute/solo
- **Piano roll** — melodic note editing with variable-length notes
- **3 synthesis modes** — subtractive, FM (4-op), and wavetable with 50 presets
- **SoundFont support** — load .sf2 files via TinySoundFont
- **Sample browser** — load WAV/MP3/FLAC samples, 72 bundled CC0 drum samples
- **Song arrangement** — pattern chaining with song/perform modes
- **Effects** — per-track and master bus filter, delay, reverb, overdrive, fuzz, chorus
- **Export** — offline render to WAV (16/24/32-bit) and MP3 (128-320k)
- **Project save/load** — JSON-based .sqproj format
- **Undo/redo** — Ctrl+Z / Ctrl+Shift+Z with 32 levels
- **Swing/shuffle** — per-transport swing control (0-100%)
- **Velocity humanization** — per-track random velocity variation
- **Pattern presets** — 12 drum patterns + 10 bass lines (House, Trap, DnB, Lo-fi, etc.)
- **Multiple frontends** — ImGui (standalone + plugin) and GTK 4.0 (Linux native)

## Frontends

| Frontend | Binary | Platform | Notes |
|----------|--------|----------|-------|
| **ImGui Standalone** | `0x808` / `0x808.exe` | Linux, Windows | SDL2 + OpenGL + Dear ImGui |
| **GTK 4.0** | `0x808_gtk` | Linux | Native GTK widgets + Cairo rendering |
| **VST3 Plugin** | `0x808.vst3` | Linux, Windows | Embeds in DAW host window |
| **CLAP Plugin** | `0x808.clap` | Linux, Windows | Embeds in DAW host window |
| **CLI** | `0x808_cli` | Linux, Windows | Terminal-only audio engine |

All GUI frontends share the `sq_app` controller library (C99) for keyboard shortcuts, panel state, playhead, and undo coordination. See `docs/FRONTEND_FEATURES.md` for the full feature parity specification.

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Space | Play / Stop |
| 1-9 | Select pattern |
| + | New pattern |
| Ctrl+S | Save project |
| Ctrl+O | Load project |
| Ctrl+C | Copy pattern |
| Ctrl+V | Paste pattern |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z | Redo |
| Ctrl+T | Cycle theme |
| Escape | Quit |

## Mouse Controls

### Drum Grid (Step Sequencer)

| Action | Behavior |
|--------|----------|
| **Left-click** | Toggle pad on/off (velocity 100) |
| **Left-click + drag** | Paint pads on or off across the grid |
| **Right-click (tap)** | Open velocity/pitch popup editor |
| **Right-click + hold + drag left/right** | Adjust velocity in real-time |
| **Right-click + hold + drag up/down** | Adjust pitch offset in real-time |

### Piano Roll

| Action | Behavior |
|--------|----------|
| **Left-click** | Place or remove a note |
| **Scroll wheel** | Scroll the view vertically |

### Virtual Keyboard

| Action | Behavior |
|--------|----------|
| **Click keys** | Trigger synth notes |
| **Click + drag** | Glissando across keys |
| **QWERTY keys** (when Piano panel is open) | Play notes — Z-M = lower octave, Q-P = upper octave |
| **<< / >>** | Shift keyboard octave range |

### Toolbar Buttons

| Action | Behavior |
|--------|----------|
| **Click PRESETS** | Toggle pattern presets dialog open/closed |
| **Click EXPORT** | Toggle export dialog open/closed |
| **Click THEME** | Cycle through themes (GTK) |
| **Click panel buttons** | Toggle panel visibility (BROWSE, MIXER, PIANO, KEYS) |

## Building

### Requirements

- C11 compiler (GCC, Clang, or MSVC) — engine, app controller, and tests
- C++17 compiler — ImGui GUI layer
- CMake 3.16+
- SDL2 (for GUI and audio)
- OpenGL 3.3 (for ImGui targets)
- GTK 4.0 (optional, for GTK frontend)

All other dependencies are vendored in `deps/`.

### Linux

```bash
sudo apt install libsdl2-dev libgl-dev g++   # Debian/Ubuntu
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Linux with GTK Frontend

```bash
sudo apt install libsdl2-dev libgl-dev libgtk-4-dev g++
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_GTK=ON
cmake --build build -j$(nproc)
```

### macOS

```bash
brew install sdl2
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

### Windows (MSVC + vcpkg)

```powershell
vcpkg install sdl2:x64-windows
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Windows (MinGW cross-compile from Linux)

```bash
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64
cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
cmake --build build_win -j$(nproc)
```

### Build Targets

| Target | Output | Description |
|--------|--------|-------------|
| `sequencer_gui` | `0x808` / `0x808.exe` | ImGui GUI (SDL2 + OpenGL + Dear ImGui) |
| `sequencer_gtk` | `0x808_gtk` | GTK 4.0 GUI (optional, `-DBUILD_GTK=ON`) |
| `sequencer_standalone` | `0x808_cli` | Terminal-only audio engine |
| `sequencer_vst3` | `0x808.vst3` | VST3 plugin |
| `sequencer_clap` | `0x808.clap` | CLAP plugin |

## Running

```bash
# ImGui GUI
./build/0x808

# GTK GUI (if built with -DBUILD_GTK=ON)
./build/0x808_gtk

# Terminal mode
./build/0x808_cli
```

## Testing

16 test targets covering DSP, synthesis, effects, project I/O, undo, edge cases, fuzzing, plugin loading, and snapshot regression.

```bash
# Run the full test suite
./scripts/test_all.sh

# Full mode includes AddressSanitizer + UBSan
./scripts/test_all.sh full
```

## Architecture

```
+---------------------------------------------+
|  Layer 4: Host Wrappers                     |
|  Standalone | Plugin (VST3/CLAP) | GTK      |
+---------------------------------------------+
|  Layer 3: GUI Components                    |
|  ImGui (C++): grid, piano, knobs, etc.     |
|  GTK (C): Cairo drawing, native widgets     |
+---------------------------------------------+
|  Layer 2: App Controller (sq_app, C99)      |
|  Shortcuts, panel state, playhead, undo     |
+---------------------------------------------+
|  Layer 1: DSP Engine (pure C99, zero deps)  |
|  Sequencer, sampler, synth, mixer, effects  |
|  Input: transport state -> Output: float buf|
+---------------------------------------------+
```

The engine (Layer 1) has zero knowledge of GUI or audio drivers. The app controller (Layer 2) provides shared non-rendering logic used by all frontends. Audio runs on a separate thread from the GUI.

## Bundled Samples

72 CC0 drum samples from classic machines:
- **TR-808** — bass drum, snare, clap, hi-hat, cowbell, cymbal, toms
- **TR-909** — bass drum, snare, clap, hi-hat, ride, crash, toms
- **TR-505** — bass drum, snare, hi-hat, toms, clap, rimshot
- **MRK-2** — kicks, snares, hi-hats, claps, toms
- **CR-78 / LM-2** — additional percussion

## Synth Presets

50 built-in presets across 3 synthesis modes:

**Subtractive**: Bass, Pad, Lead, Pluck, Stab, plus classic-inspired presets (Moog, 303 Acid, Juno Pad, OBX Brass, Prophet Strings, ARP Lead, SH Rez, and more)

**FM**: Bell, E.Piano, Metal, Bass, Pad, DX Piano, DX Vibes, FM Organ, FM Marimba, FM Clav, FM Strings, FM Koto, FM Flute, FM Harmonics, DX Sitar

**Wavetable**: Sweep, Harmonic, PWM, Vocal, WT Glass, WT Wobble, WT Choir

## Dependencies (vendored)

| Library | License | Purpose |
|---------|---------|---------|
| [miniaudio](https://miniaud.io/) | Public Domain | Cross-platform audio I/O |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT | Immediate-mode GUI (C++) |
| [dr_wav/dr_mp3/dr_flac](https://github.com/mackron/dr_libs) | Public Domain | Audio file decoding |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) | MIT | SoundFont2 synthesis |
| [cJSON](https://github.com/DaveGamble/cJSON) | MIT | JSON parsing for project files |
| [Shine](https://github.com/toots/shine) | LGPL-2 | Fixed-point MP3 encoder |

## License

MIT
