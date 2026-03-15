## ADDED Requirements

### Requirement: Settings panel toggle
The system SHALL provide a settings panel accessible via a gear/settings button in the toolbar, using the same toggle pattern as existing panels (browser, mixer, piano roll, keyboard).

#### Scenario: Open settings from toolbar
- **WHEN** user clicks the settings button in the toolbar
- **THEN** the settings panel opens showing audio and recording configuration sections

#### Scenario: Close settings
- **WHEN** user clicks the settings button again while settings panel is open
- **THEN** the settings panel closes

### Requirement: Recording output directory picker
The settings panel SHALL provide a way to view and change the recording output directory.

#### Scenario: View current directory
- **WHEN** the settings panel is open
- **THEN** the current recording output directory path is displayed

#### Scenario: Change directory via folder picker
- **WHEN** the user activates the folder picker control
- **THEN** a folder selection mechanism is presented and the chosen folder becomes the new recording output directory

### Requirement: Recording filename prefix
The settings panel SHALL provide an editable text field for the recording filename prefix.

#### Scenario: Edit prefix
- **WHEN** the user changes the prefix text from "recording" to "jam"
- **THEN** subsequent recordings use filenames like `jam_001.wav`

### Requirement: Recording bit depth selector
The settings panel SHALL provide a dropdown to select recording bit depth (16, 24, or 32-bit).

#### Scenario: Change bit depth
- **WHEN** the user selects 24-bit from the dropdown
- **THEN** subsequent recordings are captured at 24-bit depth

### Requirement: Frontend parity
Both ImGui and GTK frontends SHALL implement identical settings panel functionality.

#### Scenario: ImGui settings
- **WHEN** using the ImGui frontend
- **THEN** all settings controls are available and functional

#### Scenario: GTK settings
- **WHEN** using the GTK frontend
- **THEN** all settings controls are available and functionally identical to ImGui
