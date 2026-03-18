## Why

Modern drum machines and groove boxes (Elektron Digitakt, MPC, Maschine) ship with features like choke groups, step probability, and tap tempo as standard. Without these, 0x808 lacks fundamental behaviors that producers expect — choke groups for proper hi-hat interaction, probability for evolving patterns, and tap tempo for quick BPM setting. Adding these for v1.3.0 closes the gap on essentials, while a broader feature set (polymeter, euclidean rhythms, sample manipulation) is planned for v1.4.0.

## What Changes

### v1.3.0 (implement now)
- **Choke groups**: Assign tracks to exclusive mute groups so triggering one silences others in the same group (e.g., closed hi-hat chokes open hi-hat)
- **Step probability**: Per-step percentage (0-100%) controlling how likely a step is to fire each loop iteration
- **Tap tempo**: Tap a button/key rhythmically to set BPM from timing of taps
- **MIDI CC mapping**: Fixed CC-to-parameter mapping with factory defaults for GM2, Akai MPK Mini (CC70-77), and Novation Launchkey (CC21-28). Handles pitch bend, program change, sustain pedal.
- **MIDI learn mode**: Right-click any knob/slider to enter learn mode, wiggle a CC on the controller, binding is saved. Ported from 0xSYNTH.
- **MIDI drum pad mapping**: GM drum note map so MPC-style pad controllers trigger sampler tracks (note 36=kick, 38=snare, etc.)

### v1.4.0 (backlog)
- Note repeat / ratcheting (per-step retrigger count)
- Humanize (velocity + micro-timing randomization)
- Pattern randomize / generate
- Per-track step length (polymeter)
- Euclidean rhythm generator
- Micro-timing per step
- Sample start/end points + reverse
- MIDI output to external gear
- Groove templates (MPC swing, 808 shuffle presets)
- User sample import (load your own WAVs)
- Parameter locks (per-step automation of any parameter)
- Sample slicing / chopping

## Capabilities

### New Capabilities
- `choke-groups`: Per-track choke group assignment; triggering a track silences all other tracks in the same group
- `step-probability`: Per-step trigger probability (0-100%) evaluated each loop iteration
- `tap-tempo`: Rhythmic tap input to derive BPM from inter-tap timing
- `midi-cc-mapping`: CC-to-parameter mapping table with factory defaults, pitch bend, program change, sustain pedal
- `midi-learn`: Interactive CC-to-parameter binding via right-click learn mode
- `midi-drum-pads`: GM drum note mapping for MPC-style pad controllers to trigger sampler tracks

### Modified Capabilities
(none)

## Impact

- **Engine** (`src/engine/`): New fields on `sq_track_t` (choke group) and `sq_step_t` (probability). Choke logic in `sequencer_trigger_step()`. New `CMD_SET_PARAM` command for CC-driven parameter changes.
- **MIDI** (`src/engine/sq_midi.cpp`): Expand callback to handle CC, pitch bend, program change, sustain. Add CC map table and learn state.
- **App** (`src/app/`): Tap tempo logic, MIDI learn state, MIDI input mode (synth/drum pads).
- **GUI** (`src/gui/`, `src/gui_gtk/`): Choke group selector, probability editor, tap tempo button, MIDI learn UI (right-click context menu + visual feedback), MIDI input mode in settings. Both frontends (parity rule).
- **Plugin** (`src/plugin/`): Inherits engine CC/pitch bend handling. Plugin already handles MIDI events in `cplug_process()`.
- **Dependencies**: None new — reuses existing RtMidi + command queue infrastructure.
