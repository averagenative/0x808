## ADDED Requirements

### Requirement: Per-track insert effects
Each track SHALL have an optional insert effects chain with up to 3 effect slots. Available effects SHALL include: biquad filter (LP/HP/BP), delay, and reverb.

#### Scenario: Add a filter to a track
- **WHEN** the user adds a lowpass filter effect to a drum track and sets cutoff to 500 Hz
- **THEN** the track's audio output is filtered, removing frequencies above 500 Hz

#### Scenario: Chain multiple effects
- **WHEN** a track has filter → delay → reverb in its effects chain
- **THEN** audio flows through each effect in order: filtered first, then delayed, then reverbed

### Requirement: Master bus effects
The system SHALL provide a master bus effects chain applied to the mixed output of all tracks before final output. The master bus SHALL support the same effect types as track inserts.

#### Scenario: Master reverb
- **WHEN** a reverb effect is added to the master bus with 30% wet mix
- **THEN** the entire mix output has reverb applied uniformly

### Requirement: Delay effect
The system SHALL provide a delay effect with adjustable delay time (1ms to 2 seconds), feedback (0-95%), and wet/dry mix. Delay time SHALL be optionally syncable to BPM.

#### Scenario: BPM-synced echo
- **WHEN** delay time is set to 1/4 note sync at 120 BPM with 50% feedback
- **THEN** each hit produces echoes at 500ms intervals, each 50% quieter than the last

### Requirement: Reverb effect
The system SHALL provide a reverb effect based on the Freeverb algorithm with adjustable room size, damping, and wet/dry mix.

#### Scenario: Small room reverb
- **WHEN** room size is set to 25% and damping to 50%
- **THEN** the audio has a short, tight reverb tail simulating a small acoustic space

### Requirement: Real-time effect parameter control
All effect parameters SHALL be adjustable in real time via knobs without introducing audio clicks or pops. Parameter changes SHALL be smoothed over a short time window.

#### Scenario: Sweep filter cutoff during playback
- **WHEN** the user turns the filter cutoff knob while audio is playing
- **THEN** the filter responds smoothly without zipper noise or discontinuities
