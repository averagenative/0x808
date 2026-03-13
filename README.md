# Sequencer_C

A cross-platform drum/sample sequencer and synthesizer written in C99. Combines a drum grid, piano roll, section-based song arrangement, multi-type synthesis, and sample browsing in a single lightweight package.

## Features

- **Drum grid** — 16-track step sequencer with velocity, pitch per step, mute/solo
- **Piano roll** — melodic note editing with variable-length notes
- **3 synthesis modes** — subtractive, FM (4-op), and wavetable with 50 presets
- **SoundFont support** — load .sf2 files via TinySoundFont
- **Sample browser** — load WAV/MP3/FLAC samples, 72 bundled CC0 drum samples
- **Song arrangement** — pattern chaining with song/perform modes
- **Effects** — per-track and master bus filter, delay, reverb
- **Export** — offline render to WAV (16/24/32-bit)
- **Project save/load** — JSON-based .sqproj format
- **Undo/redo** — Ctrl+Z / Ctrl+Shift+Z with 32 levels
- **Swing/shuffle** — per-transport swing control (0-100%)
- **Velocity humanization** — per-track random velocity variation

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
| Escape | Quit |

## Building

### Requirements

- C99 compiler (GCC, Clang, or MSVC)
- CMake 3.16+
- SDL2 (for GUI)
- OpenGL 3.3 (for GUI)

All other dependencies are vendored single-header libraries in `deps/`.

### Linux

```bash
sudo apt install libsdl2-dev libgl-dev   # Debian/Ubuntu
cmake -B build -DCMAKE_BUILD_TYPE=Release
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

### Build Targets

| Target | Description |
|--------|-------------|
| `sequencer_gui` | Full GUI application (SDL2 + OpenGL + Nuklear) |
| `sequencer_standalone` | Terminal-only audio engine (miniaudio) |
| `fm_synth_test` | Synthesis test — all 50 presets |
| `project_test` | Save/load round-trip test |
| `swing_humanize_test` | Swing timing + humanization test |

## Running

```bash
# GUI version
./build/sequencer_gui

# Terminal version (triggers samples on keypress)
./build/sequencer_standalone

# Run tests
./build/fm_synth_test && ./build/project_test && ./build/swing_humanize_test
```

## Architecture

```
┌─────────────────────────────────────────────┐
│  Layer 3: Host Wrapper                      │
│  Standalone (miniaudio) OR Plugin (CPLUG)   │
├─────────────────────────────────────────────┤
│  Layer 2: GUI (Nuklear + SDL2 + OpenGL)     │
│  Drum grid, piano roll, knobs, arrangement  │
├─────────────────────────────────────────────┤
│  Layer 1: DSP Engine (pure C, zero deps)    │
│  Sequencer, sampler, synth, mixer, effects  │
│  Input: transport state → Output: float buf │
└─────────────────────────────────────────────┘
```

The engine (Layer 1) has zero knowledge of GUI or audio drivers. It receives transport info and produces float audio buffers. Audio runs on a separate thread from the GUI.

## Bundled Samples

72 CC0 drum samples from classic machines:
- **TR-808** — bass drum, snare, clap, hi-hat, cowbell, cymbal, toms, etc.
- **TR-909** — bass drum, snare, clap, hi-hat, ride, crash, toms
- **TR-505** — bass drum, snare, hi-hat, toms, clap, rimshot, etc.
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
| [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) | Public Domain | Immediate-mode GUI |
| [dr_wav/dr_mp3/dr_flac](https://github.com/mackron/dr_libs) | Public Domain | Audio file decoding |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) | MIT | SoundFont2 synthesis |
| [cJSON](https://github.com/DaveGamble/cJSON) | MIT | JSON parsing for project files |

## License

MIT
