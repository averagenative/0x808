## ADDED Requirements

### Requirement: Per-step trigger probability
Each step SHALL have a `probability` field (0-100). A value of 0 SHALL be treated as 100% (always trigger). On each loop iteration, the sequencer SHALL evaluate the probability and skip the step if the random check fails.

#### Scenario: Step with 50% probability
- **WHEN** a step has probability=50 and the sequencer reaches it
- **THEN** the step SHALL trigger approximately 50% of the time across multiple loop iterations

#### Scenario: Step with 100% probability
- **WHEN** a step has probability=100 (or probability=0, the default)
- **THEN** the step SHALL always trigger, identical to current behavior

#### Scenario: Step with 1% probability
- **WHEN** a step has probability=1
- **THEN** the step SHALL trigger approximately 1% of the time

### Requirement: Probability uses engine PRNG
The probability check SHALL use the engine's existing xorshift32 PRNG (`engine->rng_state`) to generate random values. This ensures no additional state or allocation in the audio path.

#### Scenario: PRNG evaluation in audio thread
- **WHEN** the sequencer evaluates step probability
- **THEN** it SHALL use only the engine's existing PRNG with no heap allocation

### Requirement: Probability UI in both frontends
Both ImGui and GTK frontends SHALL allow editing step probability.

#### Scenario: User sets probability in ImGui
- **WHEN** user edits a step's probability value (0-100) in the drum grid or step detail view
- **THEN** the step's probability field SHALL be updated

#### Scenario: GTK frontend parity
- **WHEN** probability editing exists in ImGui
- **THEN** an equivalent probability editor SHALL exist in the GTK frontend

### Requirement: Probability does not affect export determinism
Offline export SHALL use the same PRNG path as live playback. Probability creates variation by design — repeated exports of the same pattern MAY produce different results.

#### Scenario: Exporting a pattern with probability steps
- **WHEN** a pattern containing steps with probability < 100 is exported
- **THEN** the export SHALL render with probability evaluation active (not bypassed)
