## ADDED Requirements

### Requirement: Rotary knob widget
The system SHALL provide a custom rotary knob GUI widget that responds to vertical mouse drag to change value. Dragging up SHALL increase the value, dragging down SHALL decrease it. The knob SHALL display its current value and parameter name.

#### Scenario: Adjust a knob
- **WHEN** the user clicks and drags upward on the BPM knob
- **THEN** the BPM value increases proportionally to the drag distance, and the knob visual rotates clockwise

#### Scenario: Fine adjustment
- **WHEN** the user holds Shift while dragging a knob
- **THEN** the value changes at 1/10th the normal rate for precise adjustments

### Requirement: Knob-to-parameter mapping
Every adjustable parameter in the engine (BPM, volume, pan, filter cutoff, resonance, envelope times, LFO rate, effect parameters) SHALL be controllable via a knob widget. Parameter changes from knobs SHALL take effect in real time.

#### Scenario: Real-time knob interaction
- **WHEN** the user turns the filter cutoff knob while the sequencer is playing
- **THEN** the filter cutoff changes immediately and the audio output reflects the change without latency perceptible to the user

### Requirement: Visual feedback
Knobs SHALL provide visual feedback: a rotational indicator showing the current position within the parameter range, and a numeric readout of the current value with appropriate units (Hz, ms, dB, %).

#### Scenario: Knob display
- **WHEN** a filter cutoff knob is set to 1200 Hz
- **THEN** the knob shows a rotational position at approximately 30% and displays "1200 Hz" as text

### Requirement: Double-click reset
The system SHALL reset a knob to its default value when the user double-clicks it.

#### Scenario: Reset to default
- **WHEN** the user double-clicks the pan knob
- **THEN** the pan value resets to center (0.0) and the knob visual returns to the center position
