## ADDED Requirements

### Requirement: Offline render to WAV
The system SHALL render the current pattern or full song arrangement to a WAV file (16-bit or 24-bit, 44100 Hz or 48000 Hz). Rendering SHALL run faster than real time by processing the engine without audio device output.

#### Scenario: Export a pattern to WAV
- **WHEN** the user selects "Export Pattern" and chooses WAV format at 44100 Hz 16-bit
- **THEN** the system renders the current pattern (one full loop) to a WAV file at the specified settings

#### Scenario: Export full song arrangement
- **WHEN** the user selects "Export Song" in song mode
- **THEN** the system renders the entire arrangement (all sections with repeats) to a single WAV file

### Requirement: MP3 export
The system SHALL export audio to MP3 format at configurable bitrate (128, 192, 256, 320 kbps).

#### Scenario: Export to MP3
- **WHEN** the user selects MP3 format at 320 kbps
- **THEN** the system renders the audio and encodes it as an MP3 file

### Requirement: Real-time recording
The system SHALL capture the master output during live playback to an audio buffer, which can then be saved to disk as WAV or MP3.

#### Scenario: Record a live performance
- **WHEN** the user clicks "Record" and performs live section triggering for 3 minutes, then clicks "Stop"
- **THEN** the system saves the captured 3-minute audio to a file in the user's chosen format

### Requirement: Export progress feedback
During export, the system SHALL display a progress indicator showing estimated completion percentage.

#### Scenario: Long song export
- **WHEN** the user exports a 5-minute song arrangement
- **THEN** a progress bar advances from 0% to 100% during rendering, and the export completes without blocking the GUI
