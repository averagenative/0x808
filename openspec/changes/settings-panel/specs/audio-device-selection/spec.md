## ADDED Requirements

### Requirement: Audio device enumeration
The system SHALL enumerate available audio output devices using SDL2 and present them in the settings panel.

#### Scenario: List available devices
- **WHEN** the settings panel is opened
- **THEN** a dropdown lists all available audio output devices plus a "Default" option

#### Scenario: Refresh device list
- **WHEN** the settings panel is opened after a device has been plugged in or removed
- **THEN** the device list reflects the current hardware state

### Requirement: Audio device selection
The system SHALL allow users to select an audio output device from the enumerated list and apply the change.

#### Scenario: Select a specific device
- **WHEN** the user selects a device from the dropdown and applies
- **THEN** the audio subsystem restarts using the selected device

#### Scenario: Default device selection
- **WHEN** the user selects "Default" from the device dropdown
- **THEN** the system uses the operating system's default audio output device

### Requirement: Sample rate display
The settings panel SHALL display the current audio sample rate as reported by the active device.

#### Scenario: View sample rate
- **WHEN** a device is active
- **THEN** the settings panel shows the current sample rate (e.g., "44100 Hz" or "48000 Hz")

### Requirement: Audio restart on device change
The system SHALL gracefully restart the audio subsystem when the user changes the output device, without crashing or leaving the engine in an inconsistent state.

#### Scenario: Device change while playing
- **WHEN** the user changes audio device while playback is active
- **THEN** playback stops, the audio device is changed, and the user can resume playback on the new device

#### Scenario: Device change while recording
- **WHEN** the user attempts to change audio device while recording is active
- **THEN** the system stops the recording first (saving the partial file), then changes the device
