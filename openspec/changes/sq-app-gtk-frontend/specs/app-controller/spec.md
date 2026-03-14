## ADDED Requirements

### Requirement: App state initialization
The `sq_app_init()` function SHALL initialize an `sq_app_t` struct with default panel visibility (all panels hidden except drum grid), pattern mode active, and no status message.

#### Scenario: Default state after init
- **WHEN** `sq_app_init()` is called
- **THEN** all panel flags are false, mode is `SQ_MODE_PATTERN`, selected track is -1, visual step is 0

### Requirement: Keyboard shortcut dispatch
The `sq_app_handle_key()` function SHALL map keycodes and modifiers to actions and update app state accordingly. It SHALL return an action enum indicating what the host must do.

#### Scenario: Space toggles playback
- **WHEN** space key is pressed
- **THEN** engine play/stop is toggled and `SQ_ACTION_NONE` is returned

#### Scenario: Escape returns quit action
- **WHEN** escape key is pressed
- **THEN** `SQ_ACTION_QUIT` is returned (host decides whether to quit)

#### Scenario: Number keys select patterns
- **WHEN** keys 1-9 are pressed without modifiers
- **THEN** the engine's current pattern is set to the corresponding index (0-8)

#### Scenario: Ctrl+S returns save action
- **WHEN** Ctrl+S is pressed
- **THEN** `SQ_ACTION_SAVE` is returned (host handles file dialog)

#### Scenario: Ctrl+O returns load action
- **WHEN** Ctrl+O is pressed
- **THEN** `SQ_ACTION_LOAD` is returned (host handles file dialog)

#### Scenario: Ctrl+C copies pattern
- **WHEN** Ctrl+C is pressed
- **THEN** the current pattern is copied to the app's clipboard

#### Scenario: Ctrl+V pastes pattern
- **WHEN** Ctrl+V is pressed
- **THEN** the clipboard pattern is pasted to the current pattern

#### Scenario: Ctrl+Z triggers undo
- **WHEN** Ctrl+Z is pressed without Shift
- **THEN** undo is performed on the engine and `SQ_ACTION_NONE` is returned

#### Scenario: Ctrl+Shift+Z triggers redo
- **WHEN** Ctrl+Shift+Z is pressed
- **THEN** redo is performed on the engine and `SQ_ACTION_NONE` is returned

#### Scenario: Ctrl+T returns theme toggle action
- **WHEN** Ctrl+T is pressed
- **THEN** `SQ_ACTION_TOGGLE_THEME` is returned

### Requirement: Panel visibility management
The `sq_app_t` struct SHALL contain boolean flags for each panel's visibility. The `sq_app_toggle_panel()` function SHALL toggle a panel by ID.

#### Scenario: Toggle browser panel
- **WHEN** `sq_app_toggle_panel(app, SQ_PANEL_BROWSER)` is called
- **THEN** the browser visibility flag is inverted

#### Scenario: Exclusive panels
- **WHEN** a panel that is mutually exclusive with another is toggled on
- **THEN** the conflicting panel is toggled off (e.g., mixer and browser share space)

### Requirement: Visual playhead computation
The `sq_app_update_playhead()` function SHALL compute the current visual step from wall-clock time, BPM, and swing settings when the engine is playing.

#### Scenario: Playhead advances during playback
- **WHEN** the engine is playing and `sq_app_update_playhead()` is called with current ticks
- **THEN** `app->visual_step` is updated to the correct step based on elapsed time and BPM

#### Scenario: Playhead resets on stop
- **WHEN** the engine transitions from playing to stopped
- **THEN** `app->visual_step` is set to the engine's current step position

### Requirement: Status message management
The `sq_app_set_status()` function SHALL set a status message with an auto-dismiss timer.

#### Scenario: Status message with timeout
- **WHEN** `sq_app_set_status(app, "Saved!", 3000)` is called
- **THEN** the status message is "Saved!" and it clears after 3000ms

### Requirement: QWERTY piano key dispatch
The `sq_app_handle_piano_key()` function SHALL map QWERTY keys to MIDI note numbers for virtual keyboard input.

#### Scenario: Piano key triggers note
- **WHEN** a mapped QWERTY key (e.g., 'A' = C4) is pressed and the keyboard panel is visible
- **THEN** the corresponding note number and velocity are returned for the host to play
