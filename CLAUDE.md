# 0x808 — Drum Machine / Sequencer / Synthesizer

## Quick Reference

- **Build**: `cd build && cmake .. && make -j$(nproc)`
- **Build GTK**: `cmake .. -DBUILD_GTK=ON`
- **Build with ASAN**: `cmake .. -DENABLE_ASAN=ON`
- **Run tests**: `bash scripts/test_all.sh quick` (from project root)
- **Run full tests**: `bash scripts/test_all.sh full` (includes ASAN + UBSAN + fuzzing)

## Architecture

4-layer design — keep layers clean:
- **Layer 1 — Engine** (`src/engine/`): Pure C99 DSP. No stdio, no stdlib in the audio path. No GUI knowledge.
- **Layer 2 — App Controller** (`src/app/`): C99 shared logic (shortcuts, panel state, recording config). No platform deps.
- **Layer 3 — GUI** (`src/gui/` ImGui C++, `src/gui_gtk/` GTK C): Frontend rendering. Each frontend is independent.
- **Layer 4 — Host Wrappers** (`src/standalone/`, `src/plugin/`): SDL2 audio, plugin lifecycle.

## Rules

### Frontend Parity
- **CRITICAL**: `main_gui.c` and `main_gtk.c` must have identical default patterns, synth setup, effects, and BPM. If you change one, change both.
- Same applies to: `pattern_presets.cpp` ↔ `gtk_presets.c`, any startup behavior.

### Debugging
- Prefer ASAN over GDB for crash debugging. Build with `-DENABLE_ASAN=ON`.
- Use GDB MCP server only for tricky scenarios (e.g., inspecting state at specific breakpoints).

### Audio Path
- `sq_engine_process()` must be real-time safe: no malloc, no printf, no file I/O, no locks.
- Exception: streaming recorder does small disk writes (~1-2KB) that hit OS page cache.

### Code Style
- Engine: C99, `sq_` prefix for all public types/functions
- GUI ImGui: C++ with `extern "C"` blocks for engine includes
- GUI GTK: Plain C, `gtk_` prefix for widget constructors
- No unnecessary abstractions. Direct struct access is intentional (not a getter/setter API).

### Testing
- Run `scripts/test_all.sh quick` before committing significant engine changes
- Tests must run from project root (samples/ dir must be accessible)
- Pre-existing flaky test: `engine_render_test` virtual keyboard timing is non-deterministic

### Cross-Platform
- Maintain Linux + Windows (MinGW cross-compile) builds
- WSLg audio is broken — use offline render tests or Windows exe for real audio testing
- Recording output defaults: `~/Music/0x808/` (Linux), `%USERPROFILE%\Music\0x808\` (Windows)

## Senior Developer Checklist

Review every change against these items (accumulated from real corrections):

- [ ] No silent fallbacks in audio code — explicit failure over silent corruption
- [ ] Frontend parity — if changing ImGui, change GTK too (and vice versa)
- [ ] Synth presets: release times ≤ 2.5s for pads, ≤ 0.5s for plucks/basses
- [ ] No heap allocation in `sq_engine_process()` audio path
- [ ] New engine fields: update both `sq_engine_init()` and `sq_engine_shutdown()`
- [ ] New panels: add to `sq_panel_t` enum, handle in both frontends + toolbar
- [ ] Plugin builds: verify no standalone-only code leaks into plugin GUI
- [ ] Test with ASAN on significant engine changes
- [ ] Cross-platform: use `#ifdef _WIN32` guards for platform-specific code
- [ ] Recording: test auto-increment filenames after changes to recorder
