## ADDED Requirements

### Requirement: Wavetable scanning synthesis
The system SHALL support wavetable synthesis where a table of single-cycle waveforms is stored and the playback position within the table is continuously adjustable. The system SHALL interpolate between adjacent waveforms for smooth morphing.

#### Scenario: Morph between waveforms
- **WHEN** the wavetable position knob is swept from 0% to 100% during playback
- **THEN** the timbre smoothly morphs through all waveforms in the table without audible stepping

#### Scenario: Static wavetable position
- **WHEN** the wavetable position is set to 50% on a table with 32 waveforms
- **THEN** the output plays waveform 16, interpolated with its neighbors

### Requirement: Wavetable loading
The system SHALL support loading custom wavetables from WAV files where each cycle is a fixed number of samples (e.g., 2048 samples per cycle). The system SHALL also bundle a set of basic wavetables.

#### Scenario: Load custom wavetable
- **WHEN** the user loads a WAV file containing 32 cycles of 2048 samples each
- **THEN** the system parses it into 32 single-cycle waveforms usable by the wavetable oscillator

#### Scenario: Use bundled wavetable
- **WHEN** the user selects a bundled wavetable preset (e.g., "Basic Analog")
- **THEN** the wavetable oscillator uses the selected preset immediately

### Requirement: Wavetable position modulation
The wavetable position parameter SHALL be modulatable by the LFO and by an envelope, allowing automated timbral movement.

#### Scenario: LFO-modulated wavetable position
- **WHEN** the LFO is routed to wavetable position at a slow rate
- **THEN** the timbre evolves rhythmically as the playback scans through different waveforms
