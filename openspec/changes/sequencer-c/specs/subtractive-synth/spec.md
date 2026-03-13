## ADDED Requirements

### Requirement: Wavetable-based oscillators
The system SHALL provide saw, square, triangle, and sine oscillators implemented via pre-computed wavetable lookup for efficient real-time performance. Each synth voice SHALL have at least 2 oscillators that can be mixed.

#### Scenario: Select oscillator waveform
- **WHEN** the user selects "sawtooth" as the oscillator waveform
- **THEN** the synth produces a sawtooth wave at the triggered pitch using wavetable lookup

#### Scenario: Mix two oscillators
- **WHEN** oscillator 1 is set to saw and oscillator 2 is set to square with 50/50 mix
- **THEN** the output is an equal blend of both waveforms

### Requirement: Biquad filter with cutoff and resonance
The system SHALL provide a state-variable biquad filter with selectable mode (lowpass, highpass, bandpass). Cutoff frequency SHALL range from 20 Hz to 20 kHz. Resonance SHALL range from 0 to self-oscillation.

#### Scenario: Lowpass filter sweep
- **WHEN** the user turns the cutoff knob from 20 kHz down to 200 Hz with the filter in lowpass mode
- **THEN** the output progressively loses high-frequency content, producing the classic filter sweep sound

#### Scenario: Resonance boost
- **WHEN** resonance is increased to near maximum
- **THEN** frequencies near the cutoff are boosted, producing a characteristic resonant peak

### Requirement: ADSR envelope generators
The system SHALL provide separate ADSR envelopes for amplitude and filter cutoff. Each stage (attack, decay, sustain, release) SHALL be independently adjustable. Attack and decay SHALL range from 1ms to 10 seconds. Sustain SHALL range from 0 to 1. Release SHALL range from 1ms to 10 seconds.

#### Scenario: Plucky bass sound
- **WHEN** the amp envelope has short attack (5ms), short decay (100ms), zero sustain, and short release (50ms)
- **THEN** the note starts quickly, decays to silence, producing a plucky percussive sound

#### Scenario: Pad sound with slow attack
- **WHEN** the amp envelope has long attack (2s), no decay, full sustain, and long release (3s)
- **THEN** the note fades in slowly, sustains while held, and fades out over 3 seconds after release

### Requirement: LFO modulation
The system SHALL provide at least 1 LFO per voice with selectable waveform (sine, triangle, square, sample-and-hold/random). The LFO SHALL be routable to pitch, filter cutoff, and amplitude. LFO rate SHALL be adjustable from 0.1 Hz to 50 Hz, with optional BPM sync.

#### Scenario: Vibrato via pitch LFO
- **WHEN** the LFO is routed to pitch at 5 Hz with moderate depth
- **THEN** the pitch oscillates smoothly around the base frequency, producing vibrato

#### Scenario: BPM-synced filter wobble
- **WHEN** the LFO is set to BPM sync at 1/4 note rate, routed to filter cutoff
- **THEN** the filter cutoff modulates in sync with the beat, producing a rhythmic wobble

### Requirement: Unison detuning
The system SHALL support unison mode where each voice plays multiple detuned copies of the oscillators. Detune amount SHALL be adjustable. Unison count SHALL be configurable from 1 to 7 voices.

#### Scenario: Thick unison sound
- **WHEN** unison is set to 5 voices with moderate detune
- **THEN** the output is a thick, chorused version of the sound with 5 slightly detuned copies panned across the stereo field
