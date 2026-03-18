## ADDED Requirements

### Requirement: Tap tempo BPM detection
The app controller SHALL compute BPM from rhythmic taps. It SHALL track the last 4 tap timestamps, compute the average inter-tap interval, and convert to BPM.

#### Scenario: User taps four times at 120 BPM
- **WHEN** user taps four times with 500ms intervals
- **THEN** the BPM SHALL be set to approximately 120

#### Scenario: User taps twice
- **WHEN** user taps only twice with a 400ms interval
- **THEN** the BPM SHALL be set to approximately 150 (derived from the single interval)

### Requirement: Tap tempo reset on long gap
If more than 2 seconds elapse between taps, the tap history SHALL be reset and the next tap starts a new measurement.

#### Scenario: User pauses and re-taps
- **WHEN** user taps twice at 120 BPM, waits 3 seconds, then taps twice at 140 BPM
- **THEN** the final BPM SHALL be approximately 140 (the earlier taps are discarded)

### Requirement: Tap tempo BPM clamping
The computed BPM SHALL be clamped to the engine's valid range (20-300 BPM).

#### Scenario: Very fast taps
- **WHEN** user taps so fast that computed BPM exceeds 300
- **THEN** the BPM SHALL be clamped to 300

#### Scenario: Very slow taps
- **WHEN** user taps so slowly that computed BPM is below 20
- **THEN** the BPM SHALL be clamped to 20

### Requirement: Tap tempo keyboard shortcut and toolbar button
Tap tempo SHALL be accessible via a keyboard shortcut (T key, unmodified) and a clickable button in the toolbar of both frontends.

#### Scenario: User presses T key repeatedly
- **WHEN** user presses the T key four times rhythmically
- **THEN** the BPM SHALL update based on the tap timing

#### Scenario: User clicks tap tempo button in toolbar
- **WHEN** user clicks the tap tempo button in the toolbar repeatedly
- **THEN** the BPM SHALL update based on the click timing

#### Scenario: GTK frontend parity
- **WHEN** tap tempo button exists in ImGui toolbar
- **THEN** an equivalent tap tempo button SHALL exist in the GTK toolbar
