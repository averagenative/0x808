## ADDED Requirements

### Requirement: Load audio samples from disk
The system SHALL load audio files in WAV, MP3, and FLAC formats and decode them into interleaved float PCM buffers. The system SHALL support mono and stereo samples at any sample rate, resampling to the engine's output rate as needed.

#### Scenario: Load a WAV file
- **WHEN** the user provides a path to a valid WAV file
- **THEN** the system decodes it into a float buffer and stores it as an `sq_sample_t` with correct frame count, channel count, and sample rate

#### Scenario: Load an unsupported format
- **WHEN** the user provides a path to an unsupported or corrupt audio file
- **THEN** the system returns an error code without crashing and logs the failure reason

### Requirement: Polyphonic voice allocation
The system SHALL support at least 16 simultaneous voices for sample playback. When all voices are in use and a new note is triggered, the system SHALL steal the oldest voice.

#### Scenario: Trigger multiple samples simultaneously
- **WHEN** 4 different samples are triggered on the same step
- **THEN** 4 voices are allocated and all 4 samples play simultaneously without clipping the voice allocator

#### Scenario: Voice stealing under full polyphony
- **WHEN** all 16 voices are active and a new note is triggered
- **THEN** the oldest voice is stopped and reassigned to the new note

### Requirement: Pitch-shifted sample playback
The system SHALL pitch-shift samples by resampling, using the formula `rate = pow(2.0, semitones / 12.0)` to adjust playback speed. The system SHALL use Hermite interpolation for high-quality pitch shifting.

#### Scenario: Play a sample pitched up 12 semitones
- **WHEN** a sample is triggered with pitch offset +12
- **THEN** the sample plays at double speed (one octave higher) with smooth interpolation artifacts

#### Scenario: Play a sample at original pitch
- **WHEN** a sample is triggered with pitch offset 0
- **THEN** the sample plays at its original speed and pitch with no interpolation artifacts

### Requirement: One-shot and looping playback modes
The system SHALL support one-shot playback (play to end, then stop) and looped playback (loop between configurable start/end points).

#### Scenario: One-shot drum hit
- **WHEN** a drum sample is triggered in one-shot mode
- **THEN** the sample plays from start to end and the voice is released

#### Scenario: Looped pad sample
- **WHEN** a looping sample reaches its loop end point
- **THEN** playback jumps back to the loop start point and continues
