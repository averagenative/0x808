## ADDED Requirements

### Requirement: Track choke group assignment
Each track SHALL have a `choke_group` field (0 = none, 1-8 = group ID). Tracks with the same non-zero choke group are mutually exclusive — triggering one silences all others in the group.

#### Scenario: Closed hi-hat chokes open hi-hat
- **WHEN** track 3 (closed hat, choke_group=1) triggers on step 5
- **THEN** track 5 (open hat, choke_group=1) SHALL immediately stop any currently playing sound

#### Scenario: No choke group assigned
- **WHEN** a track has choke_group=0
- **THEN** it SHALL NOT be affected by any other track's trigger, and its trigger SHALL NOT affect any other track

#### Scenario: Different choke groups are independent
- **WHEN** track A (choke_group=1) triggers
- **THEN** only tracks with choke_group=1 SHALL be silenced; tracks with choke_group=2 SHALL NOT be affected

### Requirement: Choke group silencing mechanism
The engine SHALL silence choked tracks appropriately based on track type.

#### Scenario: Sampler track is choked
- **WHEN** a sampler track is choked by another track in its group
- **THEN** the sample playback SHALL stop immediately (within the current audio buffer)

#### Scenario: Synth track is choked
- **WHEN** a synth track is choked by another track in its group
- **THEN** all active synth voices for that track SHALL enter the release phase of their envelope

### Requirement: Choke group UI in both frontends
Both ImGui and GTK frontends SHALL provide a choke group selector per track.

#### Scenario: User assigns choke group in ImGui
- **WHEN** user selects a choke group value (Off, 1-8) for a track in the drum grid
- **THEN** the track's choke_group field SHALL be updated immediately

#### Scenario: GTK frontend parity
- **WHEN** choke group UI exists in ImGui
- **THEN** an equivalent choke group selector SHALL exist in the GTK frontend with identical behavior

### Requirement: Default choke group is none
New tracks and existing project files SHALL default to choke_group=0 (no choke group).

#### Scenario: Loading an old project file
- **WHEN** a project file saved before choke groups were implemented is loaded
- **THEN** all tracks SHALL have choke_group=0 and behave identically to before
