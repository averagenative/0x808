## Why

There is no settings UI in 0x808. Audio always uses the system default device with no way to choose an interface. The new streaming recorder saves to `~/Music/0x808/` by default, but users have no way to change the output directory, filename prefix, or bit depth without editing code. Users with multiple audio interfaces (e.g., USB audio + built-in) need device selection to route output correctly.

## What Changes

- **Settings panel**: New panel accessible from a toolbar gear/settings button, housing all configurable preferences in one place
- **Audio device selection**: Enumerate available SDL2 output devices, let the user pick one, and restart the audio subsystem when changed. Display current sample rate.
- **Recording settings UI**: Folder picker for recording output directory, editable filename prefix, bit depth dropdown (16/24/32)
- **Audio device state in sq_app**: Store selected device name and sample rate preference so frontends share config

## Capabilities

### New Capabilities
- `settings-ui`: Settings panel UI with tabs/sections for audio device and recording configuration, accessible from toolbar in both ImGui and GTK frontends
- `audio-device-selection`: SDL2 device enumeration and selection, audio subsystem restart on device change, device name persistence

### Modified Capabilities

## Impact

- **sq_app** (`src/app/sq_app.h/c`): Add audio device config (device name, sample rate) alongside existing rec_config
- **ImGui frontend** (`src/gui/`): New `settings_panel.cpp` with ImGui widgets, toolbar button to open it
- **GTK frontend** (`src/gui_gtk/`): New `gtk_settings.c` with GTK widgets, toolbar button
- **Audio init** (`src/standalone/main_gui.c`, `src/gui_gtk/main_gtk.c`): Refactor device open into a function that accepts device name, callable for restart
- **Toolbar** (`src/gui/toolbar.cpp`): Add settings gear button
- **Plugin**: Settings panel not applicable (host manages audio device)
