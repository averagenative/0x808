## Context

This is a greenfield project building a cross-platform drum/sample sequencer with multi-type synthesis in C99. There is no existing codebase. The developer is learning C through this project, so the architecture must be approachable and incrementally buildable.

The audio software landscape offers either heavyweight C++ frameworks (JUCE) or bare-metal audio APIs. We're choosing a middle path: single-header C libraries that provide building blocks without framework lock-in. This keeps the codebase small, the build simple, and the learning curve manageable.

Constraints:
- Must run on Linux, Windows, and macOS
- Must work both as a standalone app and as a DAW plugin (VST3/CLAP)
- Audio callback must be real-time safe (no allocations, no locks, no I/O)
- All dependencies must be free/open-source with permissive licenses

## Goals / Non-Goals

**Goals:**
- Build a working drum sequencer with GUI in pure C99
- Support sample playback, 3 synthesis types, effects, and song arrangement
- Cross-platform builds from a single CMake project
- Clean 3-layer architecture allowing standalone and plugin from the same engine
- Well-commented code suitable for learning C
- Ship with bundled CC0 samples so it works out of the box

**Non-Goals:**
- Full DAW functionality (audio recording, mixing console, plugin hosting)
- MIDI file import/export (may be added later)
- Mobile platform support (iOS/Android)
- Network features (collaboration, cloud sync)
- Complex routing (send/return buses, sidechain) — keep it simple
- Sample editing (trim, normalize, fade) — use external tools

## Decisions

### Decision 1: C99 with single-header libraries (not JUCE/C++)

**Choice**: Pure C99 with vendored single-header deps (miniaudio, dr_libs, Nuklear, TinySoundFont)

**Alternatives considered**:
- **JUCE (C++)**: Most batteries-included option for audio apps. Built-in plugin formats, widgets, audio engine. Rejected because: C++ complexity is high for a learner, GPL license for open-source use, large framework dependency, and the user prefers C.
- **Pure C with platform APIs**: Writing WASAPI/CoreAudio/ALSA directly. Rejected because: massive platform-specific code, reinventing what miniaudio already solves.
- **Rust**: Strong safety guarantees, growing audio ecosystem. Rejected because: user wants C, and the single-header C library ecosystem is more mature for audio.

**Rationale**: Single-header libs give us framework-level functionality with zero build complexity. Each dependency is one `.h` file copied into `deps/`. No package managers, no submodules, no linking headaches. Perfect for learning.

### Decision 2: 3-layer architecture (Engine / GUI / Host)

**Choice**: Strict separation into DSP engine (pure C, no deps), GUI (Nuklear+SDL2), and host wrappers (miniaudio for standalone, CPLUG for plugins).

**Rationale**: The engine layer has zero knowledge of how audio reaches speakers or how the GUI is drawn. This means:
- The same `sq_engine_process()` function works in standalone and plugin contexts
- The engine is testable without a GUI or audio device
- GUI can be replaced (e.g., swap Nuklear for Dear ImGui) without touching audio code
- Plugin wrapper is a thin adapter, not a rewrite

**Interface**: The engine exposes one primary function:
```c
void sq_engine_process(sq_engine_t *engine, float *output, uint32_t num_frames);
```
This is called by either miniaudio's callback or CPLUG's process function.

### Decision 3: Nuklear for GUI (not Dear ImGui, not Qt)

**Choice**: Nuklear immediate-mode GUI with SDL2+OpenGL backend

**Alternatives considered**:
- **Dear ImGui**: More widget ecosystem, but C++ only. Would require a C++ compilation step for the GUI layer.
- **Qt**: Full widget toolkit, but enormous dependency, C++ only, complex licensing.
- **raylib + raygui**: Simple and C-friendly, but less suited for dense audio UIs (many small controls).

**Rationale**: Nuklear is ANSI C, single-header, public domain — perfectly matches our C99 stack. Immediate-mode rendering means no widget state management complexity. Custom widgets (knobs, piano roll) will need to be drawn manually using Nuklear's drawing API, but this gives full control over appearance.

### Decision 4: Threading model — audio callback + main thread GUI

**Choice**: Two threads only. miniaudio runs the audio callback on its own thread. The main thread runs the SDL2 event loop and Nuklear rendering.

**Communication**:
- **GUI → Engine**: Atomic writes for simple values (BPM, volume, play/stop). Lock-free SPSC (single-producer single-consumer) ring buffer for structural changes (pattern edits, sample assignments). GUI writes commands; audio thread reads and applies them at the start of each buffer.
- **Engine → GUI**: Atomic reads for display values (current step, current level). The GUI polls these each frame.

**Critical rule**: The audio callback must NEVER:
- Allocate or free memory (`malloc`/`free`)
- Acquire a mutex or lock
- Perform file I/O
- Call any function that might block

**Rationale**: This is standard practice in professional audio software. The audio callback runs at high priority with strict timing requirements (typically 256-1024 samples at 44100 Hz = 5.8-23.2ms deadlines). Any blocking operation causes audible glitches.

### Decision 5: Wavetable-based oscillators (not naive/computed)

**Choice**: Pre-compute waveforms into lookup tables at startup. Oscillators read from tables using interpolated lookup.

**Alternatives considered**:
- **Naive waveforms**: Compute `sin()`, direct phase comparison for square, etc. Simple but produces aliasing at high frequencies.
- **BLIT/BLEP**: Band-limited synthesis. Best quality but complex to implement.

**Rationale**: Wavetables are the best balance of quality and simplicity. Pre-compute anti-aliased waveforms at multiple octaves (mipmapped wavetables). Lookup is just array indexing with interpolation — very fast, very cache-friendly, and the same mechanism supports all oscillator types AND the wavetable synth scanning feature.

### Decision 6: CPLUG for plugin wrapping (not raw VST3 SDK)

**Choice**: Use CPLUG, a C99 plugin wrapper that exports to VST3, CLAP, and AUv2.

**Alternatives considered**:
- **Raw VST3 SDK**: C++ only, complex API with COM-like interfaces. Would require C++ for the plugin layer.
- **DPF (DISTRHO Plugin Framework)**: C++ based. Good but requires C++.
- **iPlug2**: C++ framework similar to JUCE but lighter. Still C++.

**Rationale**: CPLUG is the only C99-compatible plugin wrapper. It provides a simple callback interface (`process`, `get_parameter`, `create_gui`) that maps directly to our engine API. The plugin layer becomes ~200 lines of glue code.

### Decision 7: Parameter smoothing for click-free knob control

**Choice**: All parameter changes from knobs are smoothed using a one-pole lowpass filter on the audio thread: `smoothed = smoothed + 0.001 * (target - smoothed)`.

**Rationale**: When a user turns a knob, the value jumps discretely. Applying these jumps directly to audio parameters (especially filter cutoff) causes audible clicks/zipper noise. Smoothing spreads the change over a few milliseconds, making it inaudible. The coefficient (0.001) is tuned per parameter type.

### Decision 8: CMake build system

**Choice**: CMake with per-target configurations.

**Rationale**: CMake is the de facto standard for cross-platform C/C++ builds. It generates Makefiles on Linux, Xcode projects on macOS, and Visual Studio solutions on Windows. GitHub Actions CI can build all three from the same CMakeLists.txt.

## Risks / Trade-offs

**[Nuklear piano roll complexity]** → Custom-drawing a full piano roll (variable-length notes, scrolling, zooming) with Nuklear's immediate-mode API is the most complex GUI task. **Mitigation**: Start with the simpler drum grid (Phase 3), learn Nuklear's custom drawing API, then tackle the piano roll (Phase 5). If Nuklear proves too limiting, the GUI layer can be swapped without touching the engine.

**[Plugin GUI embedding]** → Embedding Nuklear+SDL2 inside a DAW's plugin window requires creating an SDL2 window from a host-provided native window handle (HWND/NSView/XWindow). This is platform-specific and not well-documented. **Mitigation**: CPLUG provides examples of this pattern. Fallback: the plugin opens its GUI in a separate window (less integrated but functional).

**[Lock-free correctness]** → Lock-free data structures are subtle and easy to get wrong, especially for someone learning C. **Mitigation**: Use the simplest possible patterns — atomic booleans for play/stop, atomic floats for parameter values. Only introduce a ring buffer when structural changes (pattern edits) are needed. Use well-tested SPSC ring buffer implementations.

**[Audio thread safety]** → Accidentally calling `malloc`, `printf`, or other blocking functions in the audio callback. **Mitigation**: Keep the engine layer pure — no `#include <stdio.h>`, no `#include <stdlib.h>` in engine `.c` files (allocations happen at init time only). Code review discipline.

**[Cross-platform audio latency]** → Different platforms have different audio latency characteristics. WASAPI exclusive mode (Windows) can achieve low latency, but shared mode has higher latency. **Mitigation**: miniaudio handles backend selection. Expose buffer size as a user-configurable setting. Accept that latency varies by platform.

**[FM synthesis complexity]** → 4-operator FM with configurable algorithms is significantly more complex than subtractive synthesis. **Mitigation**: Implement in Phase 6, after subtractive synth is working and the developer has more C experience. Start with 2-3 fixed algorithms before making them fully configurable.

**[Wavetable file format]** → No standard format for wavetable files exists. Different synths use different conventions. **Mitigation**: Define our own simple format (WAV file with fixed cycle length), document it clearly, and provide converter scripts for common formats (Serum wavetables).

## Open Questions

1. **Sample rate handling**: Should we resample all loaded samples to the engine's output rate at load time, or resample on-the-fly during playback? Load-time resampling uses more memory but simplifies the playback code.

2. **Project file format**: JSON (via cJSON, human-readable, easy to debug) vs custom binary format (smaller, faster)? Leaning toward JSON for learning/debugging friendliness.

3. **Undo/redo scope**: Should undo/redo cover all actions (pattern edits, parameter changes, arrangement changes) or only structural changes (pattern edits, arrangement)? Continuous parameter changes (knob turns) generate too many states for naive undo.

4. **Bundled sample licensing verification**: Need to audit each bundled sample to confirm CC0/public domain status before shipping. Consider a script that downloads samples from verified sources during the build process rather than committing audio files to git.
