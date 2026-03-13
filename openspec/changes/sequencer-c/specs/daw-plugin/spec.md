## ADDED Requirements

### Requirement: VST3 plugin format
The system SHALL build as a VST3 plugin loadable by any VST3-compatible DAW (Reaper, Ableton, FL Studio, etc.). The plugin SHALL present as a virtual instrument (VSTi) that generates audio.

#### Scenario: Load in Reaper as VST3
- **WHEN** the user scans for plugins in Reaper and selects the sequencer VST3
- **THEN** the plugin loads on an instrument track and is ready to produce audio

### Requirement: CLAP plugin format
The system SHALL build as a CLAP plugin loadable by CLAP-compatible DAWs (Reaper, Bitwig, etc.).

#### Scenario: Load in Reaper as CLAP
- **WHEN** the user scans for CLAP plugins in Reaper and selects the sequencer
- **THEN** the plugin loads and functions identically to the VST3 version

### Requirement: Embedded GUI in host
The plugin SHALL display its full GUI (drum grid, piano roll, knobs, arrangement view) within the DAW's plugin window. The GUI SHALL be resizable.

#### Scenario: Open plugin GUI
- **WHEN** the user opens the plugin editor in the DAW
- **THEN** the full sequencer GUI appears embedded in the DAW's plugin window with all interactive controls functional

### Requirement: Host transport sync
The plugin SHALL sync its transport (play/stop/BPM) with the host DAW's transport. When the host plays, the sequencer plays. When the host changes BPM, the sequencer follows.

#### Scenario: Sync with host playback
- **WHEN** the user presses play in Reaper
- **THEN** the sequencer starts playing in sync with Reaper's transport position and BPM

### Requirement: Parameter automation
The plugin SHALL expose key parameters (BPM override, pattern selection, track volumes, synth parameters) as automatable parameters that the host DAW can record and play back.

#### Scenario: Automate filter cutoff
- **WHEN** the user records automation of the filter cutoff parameter in Reaper
- **THEN** the filter cutoff follows the automation curve during playback, and the knob in the plugin GUI moves to reflect the automated value
