## ADDED Requirements

### Requirement: Melodic note grid
The system SHALL display a piano roll with vertical axis representing MIDI pitch (C1-C7) and horizontal axis representing time in steps. Notes SHALL be drawn as horizontal rectangles whose length represents note duration.

#### Scenario: Place a note
- **WHEN** the user clicks and drags on the piano roll grid
- **THEN** a note is created at the clicked pitch with length proportional to the drag distance, minimum 1 step

#### Scenario: Delete a note
- **WHEN** the user right-clicks an existing note
- **THEN** the note is removed from the pattern

### Requirement: Variable note length
Notes in the piano roll SHALL have configurable length from 1 step to the full pattern length. Note length SHALL be editable by dragging the right edge of the note rectangle.

#### Scenario: Resize a note
- **WHEN** the user drags the right edge of a note from 2 steps to 4 steps
- **THEN** the note's duration changes to 4 steps, and the synth/sample sustains for the new duration

### Requirement: Velocity display
Note velocity SHALL be indicated visually through color brightness or opacity. Higher velocity notes SHALL appear brighter/more opaque.

#### Scenario: Visual velocity feedback
- **WHEN** a note has velocity 127 and another has velocity 40
- **THEN** the high-velocity note appears visibly brighter than the low-velocity note

### Requirement: Piano key labels
The system SHALL display piano key labels (C, C#, D, etc.) along the left edge of the piano roll. Octave boundaries (C notes) SHALL be visually emphasized.

#### Scenario: Note identification
- **WHEN** the user views the piano roll
- **THEN** each pitch row is labeled with its note name and octave number, with C rows having a distinct background color

### Requirement: View switching between drum grid and piano roll
The system SHALL allow switching between drum grid view and piano roll view on a per-track basis. Sampler tracks SHALL default to drum grid view. Synth tracks SHALL default to piano roll view.

#### Scenario: Switch a synth track to drum grid
- **WHEN** the user selects drum grid view for a synth track
- **THEN** the track displays as a single row in the drum grid with step toggles
