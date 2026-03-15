## Context

Both the ImGui and GTK frontends use SDL2 for audio output with push-based threading. Audio is initialized in `main_gui.c` (ImGui) and `main_gtk.c` (GTK) by calling `SDL_OpenAudioDevice(NULL, 0, &want, &have, ...)` — NULL means "use default device". There is no settings UI. The recording config (`sq_rec_config_t`) exists in `sq_app` but has no UI to edit it. The toolbar has panel toggle buttons but no settings button.

## Goals / Non-Goals

**Goals:**
- Settings panel with audio device + recording config sections
- SDL2 device enumeration and selection
- Audio restart when device changes (stop thread, close device, reopen, restart thread)
- Folder picker for recording output directory
- Both ImGui and GTK frontends

**Non-Goals:**
- Audio input/recording from microphone (capture devices) — future
- ASIO or JACK backend selection — SDL2 handles backend internally
- Settings for plugin builds (host manages audio)
- Persistent settings file on disk — use sensible defaults, settings live in memory per session for now

## Decisions

### 1. Settings panel as a toggleable panel (not a modal dialog)

Use the same pattern as the existing mixer/browser panels — a toggleable panel that appears in the UI. This keeps the interaction model consistent and avoids modal blocking. Add `SQ_PANEL_SETTINGS` to the panel enum.

**Why over modal:** Consistent with existing UI patterns. User can keep settings open while testing audio device changes.

### 2. Audio device config in sq_app

Add `sq_audio_config_t` to `sq_app_t` with `device_name[128]`, `sample_rate`, and `device_index`. Both frontends read this config. When the user changes device, the frontend-specific code handles the actual SDL2 restart.

**Why in sq_app:** Same pattern as `sq_rec_config_t`. Keeps config centralized. Frontends own the SDL2 lifecycle but share the preference.

### 3. Audio restart via callback function pointer

Each frontend registers a restart callback (`sq_app_set_audio_restart_fn`) that sq_app calls when config changes. This keeps sq_app platform-agnostic while allowing each frontend to handle SDL2 restart differently.

**Alternative considered:** Direct SDL2 calls in sq_app. Rejected because sq_app is Layer 2 (no platform deps) and the audio init code differs between frontends.

### 4. Folder picker: native dialogs where available, text input as fallback

- GTK: Use `GtkFileDialog` with `select_folder` (already available in the codebase)
- ImGui on Windows: Use `SHBrowseForFolderA` from shell32
- ImGui on Linux: Text input field with the current path (most Linux ImGui apps do this)

### 5. SDL2 device enumeration at panel open time

Call `SDL_GetNumAudioDevices(0)` and `SDL_GetAudioDeviceName(i, 0)` when the settings panel opens, not on startup. This handles devices being plugged in/out. Store the list in a static array refreshed each time the panel is shown.

## Risks / Trade-offs

- **Audio glitch on device change** → Mitigated by stopping the push thread, closing device, reopening, and restarting. Brief silence is acceptable.
- **Device disappears mid-session** → SDL2 handles this internally (returns silence). User can reopen settings and pick a different device.
- **Sample rate change** → Engine must be re-initialized with new sample rate if it differs. This resets transport state. Acceptable for a settings change.
