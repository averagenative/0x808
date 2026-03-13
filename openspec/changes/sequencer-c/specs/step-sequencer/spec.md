## ADDED Requirements

### Requirement: Pattern-based step sequencing
The system SHALL support patterns consisting of up to 16 tracks and up to 64 steps per track. Each step SHALL have velocity (0-127, where 0 = off), pitch offset (-24 to +24 semitones), and up to 4 automation parameters.

#### Scenario: Play a 16-step drum pattern
- **WHEN** a pattern with kick on steps 1,5,9,13 and snare on steps 5,13 is playing at 120 BPM
- **THEN** the system triggers the correct samples at the correct times with sample-accurate timing

#### Scenario: Variable pattern length
- **WHEN** a pattern has track A set to 16 steps and track B set to 12 steps
- **THEN** both tracks loop independently at their own lengths (polymetric playback)

### Requirement: BPM-synced transport
The system SHALL maintain a transport clock that converts absolute sample position to beat and step positions. The transport SHALL support play, stop, and pause operations. BPM SHALL be adjustable from 20 to 300.

#### Scenario: Accurate step timing at 120 BPM
- **WHEN** the transport is running at 120 BPM with 16th-note steps at 44100 Hz sample rate
- **THEN** each step lasts exactly 5512.5 samples (sample_rate * 60 / bpm / 4) with sub-sample accuracy via fractional tracking

#### Scenario: BPM change during playback
- **WHEN** the BPM is changed from 120 to 140 while a pattern is playing
- **THEN** the step timing adjusts immediately without resetting the current position

### Requirement: Per-step velocity and parameter control
The system SHALL apply velocity to sample/synth amplitude on a per-step basis. Velocity 127 SHALL produce full amplitude, velocity 1 SHALL produce the minimum audible amplitude, and velocity 0 SHALL mean no trigger.

#### Scenario: Velocity-sensitive playback
- **WHEN** a step has velocity set to 64 (half of 127)
- **THEN** the triggered voice plays at approximately half amplitude compared to velocity 127

### Requirement: Track types
The system SHALL support two track types: sampler tracks (triggering loaded audio samples) and synth tracks (triggering the built-in synthesizer). Both types SHALL be sequenceable in the same pattern.

#### Scenario: Mixed pattern with sampler and synth tracks
- **WHEN** a pattern contains 2 sampler tracks (kick, snare) and 1 synth track (bass)
- **THEN** all three tracks play simultaneously, each using their respective sound source
