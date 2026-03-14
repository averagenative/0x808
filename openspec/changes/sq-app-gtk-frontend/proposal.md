## Why

The standalone and plugin GUI hosts duplicate ~300 lines of app logic (keyboard shortcuts, panel state management, playhead computation, layout). This duplication makes it error-prone to add new shortcuts or panels. Additionally, building a GTK 4.0 frontend requires the same app logic but with completely different rendering — without a shared layer, we'd triplicate the logic.

## What Changes

- Extract `sq_app` — a non-rendering app controller library (C++) that owns:
  - Keyboard shortcut dispatch (space, escape, 1-9 patterns, ctrl+s/o/c/v/z, etc.)
  - Panel visibility state (browser, mixer, piano roll, keyboard, etc.)
  - Visual playhead computation (wall-clock based)
  - Undo/redo coordination
  - Mode management (pattern/song/perform)
- Refactor `gui.cpp` (standalone) and `plugin_gui.cpp` to delegate app logic to `sq_app`
- Build a GTK 4.0 frontend (`src/gui_gtk/`) as an alternate native Linux frontend
  - Uses `sq_engine` for audio/sequencing and `sq_app` for app logic
  - Renders with GTK 4.0 widgets and Cairo custom drawing
  - Optional build target behind `BUILD_GTK` CMake option

## Capabilities

### New Capabilities
- `app-controller`: Shared non-rendering app logic layer (shortcuts, panel state, playhead, undo coordination)
- `gtk-frontend`: GTK 4.0 native Linux frontend with toolbar, drum grid, piano roll, synth editor, and supporting panels

### Modified Capabilities

## Impact

- New source directory: `src/app/` for `sq_app` library
- New source directory: `src/gui_gtk/` for GTK frontend
- `src/gui/gui.cpp` — refactored to delegate to `sq_app`
- `src/plugin/plugin_gui.cpp` — refactored to delegate to `sq_app`
- `CMakeLists.txt` — new `sq_app` library target, new `sequencer_gtk` executable target
- New dependency: GTK 4.0 (optional, Linux only)
- Existing ImGui builds unaffected (sq_app is additive)
