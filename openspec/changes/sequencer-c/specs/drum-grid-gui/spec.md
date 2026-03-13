## ADDED Requirements

### Requirement: Visual step grid
The system SHALL display a grid with rows representing tracks and columns representing steps. Each cell SHALL be clickable to toggle the step on/off. Active steps SHALL be visually distinct from inactive steps.

#### Scenario: Toggle a step on
- **WHEN** the user left-clicks an inactive cell in the drum grid
- **THEN** the cell becomes active with default velocity (100) and is visually highlighted

#### Scenario: Toggle a step off
- **WHEN** the user left-clicks an active cell in the drum grid
- **THEN** the cell becomes inactive (velocity 0) and returns to the default appearance

### Requirement: Playback position indicator
The system SHALL highlight the currently playing step column during playback, advancing in time with the transport.

#### Scenario: Visual playback feedback
- **WHEN** the sequencer is playing a 16-step pattern at 120 BPM
- **THEN** a highlight moves across the grid columns in sync with the audio, completing one pass every 2 seconds

### Requirement: Per-step velocity and pitch editing
The system SHALL allow right-click or secondary interaction on a step to open a popup for editing velocity (1-127) and pitch offset (-24 to +24 semitones).

#### Scenario: Edit step velocity
- **WHEN** the user right-clicks an active step and adjusts the velocity slider to 50
- **THEN** the step's velocity is updated to 50 and the cell's visual brightness reflects the lower velocity

### Requirement: Per-track controls
Each track row SHALL display the track name, a volume knob, a pan knob, a mute button, and a solo button.

#### Scenario: Mute a track
- **WHEN** the user clicks the mute button on a track
- **THEN** that track produces no audio output but continues to show step activity visually

#### Scenario: Solo a track
- **WHEN** the user clicks the solo button on a track
- **THEN** only that track (and any other soloed tracks) produce audio output
