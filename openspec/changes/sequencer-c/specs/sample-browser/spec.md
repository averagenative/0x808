## ADDED Requirements

### Requirement: File system browser
The system SHALL provide a file browser for navigating the local filesystem to find audio files. The browser SHALL filter to show only supported formats (WAV, MP3, FLAC, SF2).

#### Scenario: Browse for samples
- **WHEN** the user opens the sample browser and navigates to a directory containing WAV files
- **THEN** the browser displays all WAV files in the directory with their names and file sizes

#### Scenario: Filter unsupported files
- **WHEN** a directory contains WAV, MP3, PNG, and TXT files
- **THEN** only the WAV and MP3 files are shown in the browser

### Requirement: Waveform preview
The system SHALL display a waveform visualization of the selected sample before loading it. The user SHALL be able to audition (preview-play) the sample from the browser.

#### Scenario: Preview a sample
- **WHEN** the user selects a WAV file in the browser
- **THEN** the waveform is displayed and the user can click a play button to hear it

### Requirement: Assign samples to tracks
The system SHALL allow assigning a browsed/loaded sample to any sampler track via dropdown selection or drag interaction.

#### Scenario: Assign sample to track
- **WHEN** the user selects a loaded kick sample and assigns it to track 1
- **THEN** track 1 uses the selected kick sample for all its triggered steps

### Requirement: SF2 SoundFont support
The system SHALL load SF2 SoundFont files and expose their presets as assignable instruments. SoundFont playback SHALL support velocity layers and key ranges as defined in the SF2 file.

#### Scenario: Load a GM SoundFont
- **WHEN** the user loads a General MIDI SF2 file
- **THEN** the system lists all available presets (e.g., "Acoustic Grand Piano", "Electric Bass") and any preset can be assigned to a track

### Requirement: Bundled sample library
The system SHALL include a bundled collection of CC0/public domain drum samples organized by category (kicks, snares, hi-hats, percussion). These SHALL be immediately available without requiring any downloads.

#### Scenario: Use bundled samples on first launch
- **WHEN** the user launches the application for the first time
- **THEN** the sample browser shows the bundled drum kit samples and a default drum pattern is loaded using them
