## ADDED Requirements

### Requirement: GTK application window
The GTK frontend SHALL create a GtkApplicationWindow with a dark theme, minimum size 800x500, default size 1280x720.

#### Scenario: Window launches with correct size
- **WHEN** `sequencer_gtk` is launched
- **THEN** a GTK window appears at 1280x720 with the dark theme applied

### Requirement: Toolbar
The toolbar SHALL display transport controls (play/stop), BPM slider, swing slider, volume slider, and panel toggle buttons matching the ImGui toolbar's functionality.

#### Scenario: Play/stop button toggles playback
- **WHEN** the play button is clicked
- **THEN** the engine's playback state toggles and the button label updates

#### Scenario: BPM slider changes tempo
- **WHEN** the BPM slider is adjusted
- **THEN** the engine's BPM is updated in real-time

### Requirement: Drum grid with Cairo rendering
The drum grid SHALL render track rows and step columns using GtkDrawingArea with Cairo. It SHALL support click-to-toggle steps, velocity-based opacity, and a visual playhead column.

#### Scenario: Click toggles a step
- **WHEN** a grid cell is clicked
- **THEN** the step's active state toggles and the cell redraws with the appropriate color

#### Scenario: Playhead column highlights during playback
- **WHEN** the engine is playing
- **THEN** the current step column is highlighted and redrawn each frame

### Requirement: Piano roll with Cairo rendering
The piano roll SHALL render a note grid with piano key reference using GtkDrawingArea and Cairo. It SHALL support click to place/delete notes and scrolling.

#### Scenario: Click places a note
- **WHEN** an empty cell in the piano roll is clicked
- **THEN** a note is placed at that position with default velocity

### Requirement: Synth editor
The synth editor SHALL display oscillator, filter, and ADSR parameters with rotary knob widgets. ADSR envelope visualization SHALL use Cairo drawing.

#### Scenario: Knob drag changes parameter
- **WHEN** a knob is dragged vertically
- **THEN** the corresponding synth parameter updates in real-time

### Requirement: Supporting panels
The GTK frontend SHALL implement mixer, sample browser, virtual keyboard, arrangement, pattern presets, and export dialog panels.

#### Scenario: Sample browser loads sample
- **WHEN** a sample file is selected in the browser and assigned to a track
- **THEN** the track's sampler loads the selected sample

### Requirement: Keyboard shortcuts via sq_app
The GTK frontend SHALL delegate all keyboard shortcut handling to `sq_app_handle_key()` and respond to the returned actions.

#### Scenario: Ctrl+S triggers save
- **WHEN** Ctrl+S is pressed in the GTK window
- **THEN** `sq_app_handle_key()` returns `SQ_ACTION_SAVE` and the GTK frontend shows a GtkFileChooserDialog

### Requirement: Audio via SDL2
The GTK frontend SHALL use SDL2 (audio-only init) with the existing push-based audio thread for audio output.

#### Scenario: Audio plays without GTK interference
- **WHEN** the engine is playing
- **THEN** audio is output via SDL2 push-based thread, independent of GTK main loop

### Requirement: Optional CMake build
The GTK frontend SHALL be built only when `-DBUILD_GTK=ON` is passed to CMake. It SHALL NOT affect existing ImGui builds when disabled.

#### Scenario: Default build excludes GTK
- **WHEN** CMake is configured without `-DBUILD_GTK=ON`
- **THEN** the `sequencer_gtk` target is not defined and GTK 4.0 is not required

#### Scenario: GTK build finds dependencies
- **WHEN** CMake is configured with `-DBUILD_GTK=ON`
- **THEN** pkg-config finds `gtk4` and the `sequencer_gtk` target is created
