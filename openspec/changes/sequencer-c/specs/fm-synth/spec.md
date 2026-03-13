## ADDED Requirements

### Requirement: 4-operator FM synthesis
The system SHALL provide 4 sine-wave operators that can modulate each other's frequency. Each operator SHALL have adjustable frequency ratio (0.5x to 16x of the base frequency), output level, and an individual ADSR envelope.

#### Scenario: Simple FM bell sound
- **WHEN** operator 2 modulates operator 1 with a frequency ratio of 3.0 and moderate modulation depth
- **THEN** the output produces metallic, bell-like harmonics characteristic of FM synthesis

#### Scenario: Operator self-feedback
- **WHEN** an operator's output is fed back into its own frequency input
- **THEN** the waveform becomes increasingly complex, ranging from sine to noise depending on feedback amount

### Requirement: Configurable FM algorithms
The system SHALL provide at least 8 predefined operator routing configurations (algorithms), defining which operators modulate which. Algorithms SHALL include: serial chain (1→2→3→4), parallel (all carriers), and common DX7-style configurations.

#### Scenario: Select algorithm
- **WHEN** the user selects algorithm 3 (2→1, 4→3, outputs 1+3)
- **THEN** operators are routed accordingly: op2 modulates op1, op4 modulates op3, and ops 1 and 3 mix to the output

### Requirement: FM parameter control via knobs
Each operator's level, frequency ratio, and envelope parameters SHALL be controllable via GUI knobs for real-time sound design.

#### Scenario: Tweak modulator depth in real time
- **WHEN** the user turns the level knob on a modulator operator while a note is playing
- **THEN** the timbral complexity of the output changes smoothly in real time without clicks or pops
