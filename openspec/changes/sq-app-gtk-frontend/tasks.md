## 1. sq_app Library (C99)

- [x] 1.1 Create `src/app/sq_app.h` — define `sq_app_t` struct, action enum, panel enum, and function prototypes
- [x] 1.2 Create `src/app/sq_app.c` — implement `sq_app_init()` with default state
- [x] 1.3 Implement `sq_app_handle_key()` — keycode/modifier dispatch returning action enum (space, escape, 1-9, ctrl+s/o/c/v/z/t)
- [x] 1.4 Implement `sq_app_toggle_panel()` — panel visibility with mutual exclusion logic
- [x] 1.5 Implement `sq_app_update_playhead()` — wall-clock visual step computation
- [x] 1.6 Implement `sq_app_set_status()` / `sq_app_update_status()` — status message with auto-dismiss timer
- [x] 1.7 QWERTY piano mapping kept in per-frontend keyboard widget (SDL keycode dependent)
- [x] 1.8 Add `sq_app` static library target to CMakeLists.txt, link to sq_engine

## 2. Refactor ImGui Standalone to Use sq_app

- [x] 2.1 Replace keyboard shortcut switch in `gui.cpp` with `sq_app_handle_key()` calls + action dispatch
- [x] 2.2 Replace local panel visibility flags with `sq_app_t` state
- [x] 2.3 Replace playhead computation with `sq_app_update_playhead()` call
- [x] 2.4 Replace status message logic with `sq_app_set_status()` / `sq_app_update_status()`
- [x] 2.5 Link `sequencer_gui` target against `sq_app` (via sq_gui -> sq_app dependency)
- [x] 2.6 Verify standalone builds and all shortcuts/panels work as before

## 3. Refactor Plugin GUI to Use sq_app

- [x] 3.1 Replace keyboard shortcut handling in `plugin_gui.cpp` with `sq_app_handle_key()` calls
- [x] 3.2 Replace local panel visibility flags with `sq_app_t` state (per-instance in `sq_plugin_gui`)
- [x] 3.3 Replace playhead computation with `sq_app_update_playhead()`
- [x] 3.4 Link plugin targets against `sq_app` (via sq_gui -> sq_app dependency)
- [x] 3.5 Verify VST3 and CLAP plugins build and work as before

## 4. Remove Old Globals

- [x] 4.1 `g_visual_step`, `g_selected_track` now sourced from `sq_app_t` (hosts sync globals from app state each frame)
- [x] 4.2 ImGui components continue reading globals — GTK frontend reads `sq_app_t` directly (no globals needed)
- [x] 4.3 `gui_globals.cpp` retained for ImGui component compatibility — globals are now write-only from hosts

## 5. GTK 4.0 Build Setup

- [x] 5.1 Add `BUILD_GTK` CMake option, pkg-config for gtk4, `sequencer_gtk` target
- [x] 5.2 Create `src/gui_gtk/main_gtk.c` — GtkApplication entry point, SDL2 audio-only init, push audio thread
- [x] 5.3 Create `src/gui_gtk/gtk_theme.c` — CSS provider for dark theme
- [x] 5.4 Verify empty GTK window launches with dark theme and audio thread running

## 6. GTK Toolbar

- [x] 6.1 Create `src/gui_gtk/gtk_window.c` — main window layout with GtkBox vertical split
- [x] 6.2 Implement toolbar with play/stop button, BPM scale, swing scale, volume scale
- [x] 6.3 Add panel toggle buttons (browser, mixer, piano roll, keyboard, etc.)
- [x] 6.4 Wire toolbar controls to sq_engine and sq_app

## 7. GTK Drum Grid

- [x] 7.1 Create `src/gui_gtk/gtk_drum_grid.c` — GtkDrawingArea with Cairo grid rendering
- [x] 7.2 Implement step cell drawing with velocity-based opacity and active/inactive colors
- [x] 7.3 Implement click-to-toggle and drag for multi-step editing (GtkGestureClick + GtkGestureDrag)
- [x] 7.4 Implement track labels, mute/solo buttons, volume/pan controls per track
- [x] 7.5 Implement playhead column highlight (redraw on `sq_app_t.visual_step` change)

## 8. GTK Piano Roll

- [x] 8.1 Create `src/gui_gtk/gtk_piano_roll.c` — GtkDrawingArea with Cairo note grid
- [x] 8.2 Implement piano key reference column
- [x] 8.3 Implement click to place/delete notes
- [ ] 8.4 Implement scrolling (GtkScrolledWindow or manual scroll state)

## 9. GTK Synth Editor

- [x] 9.1 Create `src/gui_gtk/gtk_knob.c` — custom rotary knob widget using GtkDrawingArea + Cairo
- [x] 9.2 Implement vertical drag interaction with shift-drag for fine control
- [x] 9.3 Create `src/gui_gtk/gtk_synth_editor.c` — mode tabs, oscillator/filter/ADSR knobs
- [x] 9.4 Implement ADSR envelope curve visualization with Cairo

## 10. GTK Supporting Panels

- [x] 10.1 Create `src/gui_gtk/gtk_mixer.c` — per-track channel strips with GtkScale for volume/pan
- [x] 10.2 Create `src/gui_gtk/gtk_browser.c` — GtkListView for sample file browsing and assignment
- [x] 10.3 Create `src/gui_gtk/gtk_keyboard.c` — GtkDrawingArea piano with click-to-play
- [x] 10.4 Create `src/gui_gtk/gtk_arrangement.c` — pattern chain editor for song/perform modes
- [x] 10.5 Create `src/gui_gtk/gtk_presets.c` — preset save/load dialog
- [x] 10.6 Create `src/gui_gtk/gtk_export.c` — WAV export settings dialog

## 11. GTK Keyboard Shortcuts

- [x] 11.1 Wire GtkEventControllerKey to `sq_app_handle_key()` in main window
- [x] 11.2 Handle returned actions (SQ_ACTION_SAVE → GtkFileChooserDialog, SQ_ACTION_QUIT → close, etc.)
- [ ] 11.3 Wire QWERTY piano input via `sq_app_handle_piano_key()` when keyboard panel is visible

## 12. Integration & Verification

- [x] 12.1 Verify `sequencer_gtk` builds with `-DBUILD_GTK=ON`
- [x] 12.2 Verify default build (no `-DBUILD_GTK`) still works without GTK dependency
- [x] 12.3 Verify ImGui standalone still works after sq_app refactor
- [x] 12.4 Verify plugin builds still work after sq_app refactor
- [x] 12.5 Document `sq_app` API in `src/app/sq_app.h`
