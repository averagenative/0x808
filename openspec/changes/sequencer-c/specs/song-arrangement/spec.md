## ADDED Requirements

### Requirement: Section-based song structure
The system SHALL support arranging patterns into sections. Each section SHALL reference a pattern and have a configurable repeat count (1-99). Up to 32 sections SHALL be supported in a single arrangement.

#### Scenario: Build a verse-chorus structure
- **WHEN** the user creates sections: Intro(1x) → Verse(4x) → Chorus(2x) → Verse(4x) → Outro(1x)
- **THEN** the arrangement contains 5 sections with their respective patterns and repeat counts

### Requirement: Song mode (linear playback)
In song mode, the system SHALL play through the arrangement linearly from the first section to the last, respecting repeat counts. Playback SHALL stop after the last section completes unless loop-all is enabled.

#### Scenario: Play full song
- **WHEN** the user presses play in song mode
- **THEN** the system plays each section in order, repeating each for its configured count, then stops

### Requirement: Perform mode (live section triggering)
In perform mode, the system SHALL allow the user to queue a section for playback. The queued section SHALL begin playing at the next bar boundary (end of current pattern loop). The current section SHALL loop until a new section is queued.

#### Scenario: Queue next section during playback
- **WHEN** the user clicks section 3 while section 1 is playing at step 10 of 16
- **THEN** section 1 completes its current loop (steps 11-16), then section 3 begins playing

#### Scenario: No queued section
- **WHEN** the current section finishes and no new section is queued
- **THEN** the current section loops indefinitely

### Requirement: Pattern mode
In pattern mode, the system SHALL loop the currently selected pattern indefinitely, ignoring the arrangement. This is the default editing mode.

#### Scenario: Edit while looping
- **WHEN** the user is in pattern mode editing a pattern
- **THEN** the pattern loops continuously, and edits are heard on the next loop iteration
