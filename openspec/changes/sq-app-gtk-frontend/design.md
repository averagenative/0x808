## Context

The sequencer has a 3-layer architecture: `sq_engine` (C99 DSP), `sq_gui` (ImGui component library), and host wrappers (standalone `gui.cpp` + plugin `plugin_gui.cpp`). Both host wrappers duplicate ~300 lines of app logic: keyboard shortcuts, panel state, playhead computation, and layout management. A GTK 4.0 frontend would need this same logic a third time.

The `sq_gui` library is ImGui-specific (DrawList calls, ImGui widgets) and cannot be reused by GTK. What CAN be shared is the non-rendering app logic that sits between the engine and any frontend.

## Goals / Non-Goals

**Goals:**
- Extract shared app logic into `sq_app` library — single source of truth for shortcuts, panel state, playhead, undo coordination
- Reduce duplication between standalone and plugin hosts
- Build GTK 4.0 frontend as alternate Linux-native frontend
- Both ImGui and GTK frontends buildable from the same source tree

**Non-Goals:**
- Abstracting rendering primitives (each frontend owns its rendering)
- GTK plugin GUI (plugins stay ImGui — GTK can't embed in DAW host windows)
- Windows GTK support (GTK 4.0 frontend is Linux-only)
- Replacing ImGui — it remains the primary frontend

## Decisions

### 1. `sq_app` as a C99 static library in `src/app/`

The app controller is a struct (`sq_app_t`) with init/update/shutdown functions. It owns:
- Panel visibility flags (browser, mixer, piano roll, keyboard, etc.)
- Visual playhead state (wall-clock computation)
- Mode state (pattern/song/perform)
- Status message + timer

It does NOT own:
- Window management (SDL, GTK, or other)
- Rendering (ImGui, Cairo, or other)
- Audio (each host manages its own audio backend)
- ImGui context (remains host responsibility)

**Rationale:** Pure C99, matching `sq_engine_t` style. This keeps the GTK frontend as pure C (no C++ anywhere) and avoids extern "C" friction. The ImGui frontend wraps the C API naturally since C++ can call C directly. No vtables, no inheritance — just a data struct that frontends read/write.

### 2. Event dispatch via `sq_app_handle_key()`

Rather than duplicating switch statements, both hosts call:
```c
sq_app_action_t sq_app_handle_key(sq_app_t *app, sq_engine_t *engine,
                                   int keycode, int mod, int down);
```

Returns an action enum (`SQ_ACTION_NONE`, `SQ_ACTION_QUIT`, `SQ_ACTION_SAVE`, `SQ_ACTION_LOAD`, etc.) so the host can handle platform-specific responses (e.g., standalone shows file dialog, plugin shows "use DAW to save").

**Alternative considered:** Callback-based dispatch. Rejected because it adds indirection without benefit — the host needs to know what happened anyway.

### 3. GTK 4.0 frontend structure

```
src/gui_gtk/
  main_gtk.c          — Entry point, GtkApplication, audio thread
  gtk_window.c        — Main window, toolbar (GtkHeaderBar), panel layout
  gtk_drum_grid.c     — GtkDrawingArea + Cairo rendering
  gtk_piano_roll.c    — GtkDrawingArea + Cairo rendering
  gtk_synth_editor.c  — Knobs as custom GtkWidget, ADSR as GtkDrawingArea
  gtk_mixer.c         — Channel strips with GtkScale widgets
  gtk_browser.c       — GtkListView for sample browsing
  gtk_keyboard.c      — GtkDrawingArea piano widget
  gtk_theme.c         — CSS provider for dark/light themes
```

**Rationale:** Each panel maps to a GTK widget or custom drawing area. Cairo replaces ImGui DrawList for custom rendering (drum grid cells, piano roll notes, knobs, envelopes). Native GTK widgets used where appropriate (sliders, buttons, dropdowns).

### 4. GTK audio via SDL2 (not GStreamer)

The GTK frontend still uses SDL2 for audio output with the existing push-based audio thread. GTK handles only the UI.

**Rationale:** The push-based SDL audio thread works reliably and is already battle-tested. GStreamer adds complexity without benefit for a simple audio push loop. SDL2 can init without video when GTK owns the window.

### 5. Build integration

```cmake
option(BUILD_GTK "Build GTK 4.0 frontend" OFF)

add_library(sq_app STATIC src/app/sq_app.c)
target_link_libraries(sq_app PUBLIC sq_engine)

# ImGui targets link sq_app
target_link_libraries(sequencer_gui PRIVATE sq_gui sq_app ...)
target_link_libraries(sequencer_vst3 PRIVATE sq_gui sq_app ...)

if(BUILD_GTK)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK4 REQUIRED gtk4)
  add_executable(sequencer_gtk src/gui_gtk/main_gtk.c ...)
  target_link_libraries(sequencer_gtk PRIVATE sq_app sq_engine ${GTK4_LIBRARIES} ${SDL2_LIBRARIES})
endif()
```

## Risks / Trade-offs

- **[Risk] GTK 4.0 availability on WSL2** → GTK 4.0 rendering may not work under WSLg. Mitigation: Test early, fall back to X11 forwarding if needed.
- **[Risk] Cairo performance for drum grid** → Immediate-mode Cairo redraw every frame could be slower than ImGui's GPU-accelerated rendering. Mitigation: Use `gtk_widget_queue_draw()` only on state changes (retained-mode), not every frame.
- **[Trade-off] Two rendering codebases** → Each panel is implemented twice (ImGui + GTK). Accepted because the rendering approaches are fundamentally different and abstracting them would add more complexity than maintaining two implementations.
- **[Trade-off] Two language boundaries** → ImGui frontend (C++) calls into sq_app (C99) and sq_engine (C99). This is natural since C++ calls C directly. GTK frontend is pure C throughout.
