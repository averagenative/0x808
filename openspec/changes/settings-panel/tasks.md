## 1. App-Level Config (sq_app)

- [x] 1.1 Add `SQ_PANEL_SETTINGS` to panel enum in sq_app.h, update `SQ_PANEL_COUNT`
- [x] 1.2 Add `sq_audio_config_t` struct to sq_app.h — device_name[128], sample_rate, device_index
- [x] 1.3 Add audio_config to sq_app_t, initialize with defaults (empty name = "Default", 44100 Hz)
- [x] 1.4 Add audio restart callback function pointer to sq_app_t — `void (*audio_restart_fn)(void *userdata)` + `void *audio_restart_userdata`

## 2. Toolbar Settings Button

- [x] 2.1 Add settings gear button to ImGui toolbar (after panel toggles), toggles SQ_PANEL_SETTINGS
- [x] 2.2 Add settings button to GTK toolbar, toggles SQ_PANEL_SETTINGS

## 3. ImGui Settings Panel

- [x] 3.1 Create `src/gui/settings_panel.cpp` and `src/gui/settings_panel.h`
- [x] 3.2 Audio section: device dropdown using SDL_GetNumAudioDevices/SDL_GetAudioDeviceName, "Apply" button that calls audio restart callback
- [x] 3.3 Audio section: display current sample rate from engine
- [x] 3.4 Recording section: output directory text input with native folder picker on Windows (SHBrowseForFolder)
- [x] 3.5 Recording section: filename prefix text input bound to rec_config.prefix
- [x] 3.6 Recording section: bit depth combo (16/24/32) bound to rec_config.bit_depth
- [x] 3.7 Add settings_panel to gui.cpp layout — show when SQ_PANEL_SETTINGS is active
- [x] 3.8 Add settings_panel.cpp to CMakeLists.txt

## 4. GTK Settings Panel

- [x] 4.1 Create `src/gui_gtk/gtk_settings.c` with settings panel widget
- [x] 4.2 Audio section: GtkDropDown with SDL2 device names, "Apply" button
- [x] 4.3 Audio section: sample rate label
- [x] 4.4 Recording section: directory entry with GtkFileDialog folder picker button
- [x] 4.5 Recording section: prefix entry and bit depth dropdown
- [x] 4.6 Add settings panel to GTK window layout, show when SQ_PANEL_SETTINGS is active
- [x] 4.7 Add gtk_settings.c to CMakeLists.txt

## 5. Audio Device Restart (ImGui)

- [x] 5.1 Refactor audio init in main_gui.c into `audio_open_device(const char *device_name)` function
- [x] 5.2 Implement audio restart: stop push thread → close device → reopen with new device → restart thread
- [x] 5.3 Register restart callback with sq_app on startup
- [x] 5.4 Handle device change while playing (stop transport first) and while recording (stop recording first)

## 6. Audio Device Restart (GTK)

- [x] 6.1 Refactor audio init in main_gtk.c into `audio_open_device(const char *device_name)` function
- [x] 6.2 Implement audio restart: stop push thread → close device → reopen → restart thread
- [x] 6.3 Register restart callback with sq_app on startup
- [x] 6.4 Handle device change while playing/recording

## 7. Integration

- [x] 7.1 Verify settings panel works in ImGui standalone
- [x] 7.2 Verify settings panel works in GTK frontend
- [x] 7.3 Verify audio device switching works end-to-end
- [x] 7.4 Verify recording config changes take effect on next recording
