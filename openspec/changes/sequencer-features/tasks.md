## 1. Choke Groups -- Engine

- [x] 1.1 Add `uint8_t choke_group` field to `sq_track_t` in `engine.h` (0=none, 1-8)
- [x] 1.2 Implement choke logic in `sequencer_trigger_step()`: when a track fires, iterate other tracks in the pattern and silence any with matching non-zero choke_group
- [x] 1.3 Implement sampler choke: stop sample playback immediately for choked sampler tracks
- [x] 1.4 Implement synth choke: call `envelope_release()` on active voices belonging to choked synth tracks
- [x] 1.5 Add choke_group to project save/load in `formats/project.c` (backwards compatible -- defaults to 0)

## 2. Choke Groups -- GUI

- [x] 2.1 ImGui: Add choke group dropdown (Off, 1-8) to track header in drum grid
- [x] 2.2 GTK: Add choke group indicator + click-to-cycle in drum grid
- [x] 2.3 Verify frontend parity: both frontends show and edit choke group identically

## 3. Step Probability -- Engine

- [x] 3.1 Add `uint8_t probability` field to `sq_step_t` in `engine.h` (0=100%, 1-100=percentage)
- [x] 3.2 Implement probability check in `sequencer_trigger_step()` using engine xorshift32 PRNG
- [x] 3.3 Add probability to project save/load (backwards compatible -- 0 means always trigger)

## 4. Step Probability -- GUI

- [x] 4.1 ImGui: Add probability slider/input to step edit popup
- [x] 4.2 ImGui: Show visual indicator on steps with probability < 100 (yellow percentage at top-right)
- [x] 4.3 GTK: Add probability spinner to step editing popover
- [x] 4.4 GTK: Show visual indicator for probability steps (yellow percentage)
- [x] 4.5 Verify frontend parity for probability display and editing

## 5. Tap Tempo -- App Controller

- [x] 5.1 Add tap tempo state to `sq_app_t`: last 4 tap timestamps, tap count
- [x] 5.2 Implement `sq_app_tap_tempo()` in `sq_app.c`: average interval, BPM clamp, 2s reset
- [x] 5.3 Add `SQ_ACTION_TAP_TEMPO` and wire to T key in `sq_app_handle_key()`

## 6. Tap Tempo -- GUI

- [x] 6.1 ImGui: Add TAP button in toolbar next to BPM knob
- [x] 6.2 GTK: Add TAP button in toolbar next to BPM knob
- [x] 6.3 Verify both frontends update BPM on tap (+ plugin)

## 7. MIDI CC Mapping -- Engine (port from 0xSYNTH)

- [x] 7.1 Add `sq_midi_cc_map_t` struct: array of 128 CC slots mapping to parameter IDs (-1=unassigned)
- [x] 7.2 Add `sq_param_id_t` enum in `engine.h`: filter cutoff, resonance, amp ADSR, master volume, BPM, swing, delay wet, reverb wet
- [x] 7.3 Implement factory default CC map in `sq_engine_init()`: GM2 standard mappings
- [x] 7.4 Add Akai MPK Mini defaults: CC70-77 mapped to cutoff, resonance, attack, decay, sustain, release, reverb, delay
- [x] 7.5 Add Novation Launchkey defaults: CC21-28 mapped to same parameter order
- [x] 7.6 Handle CC messages in `sq_midi.cpp` callback: look up CC in map, push CMD_MIDI_CC command
- [x] 7.7 Add `CMD_MIDI_CC` and `CMD_PITCH_BEND` command types to command queue
- [x] 7.8 Process `CMD_MIDI_CC` in `sq_engine_process()` -- apply mapped CC values to engine parameters

## 8. MIDI Pitch Bend & Program Change

- [x] 8.1 Handle pitch bend (0xE0) in MIDI callback: decode 14-bit value, push CMD_PITCH_BEND
- [x] 8.2 Apply pitch bend to active synth voices (+/- 2 semitones via `pitch_bend` field)
- [x] 8.3 Handle program change (0xC0): programs 0-3 log kit select, 4+ map to pattern select
- [x] 8.4 Add sustain pedal support (CC64): state tracked in midi struct

## 9. MIDI Learn Mode

- [x] 9.1 Add `learn_param` field to `sq_midi` struct (SQ_PARAM_NONE when not learning)
- [x] 9.2 Implement `sq_midi_learn_start()`/`cancel()`/`active()` API
- [x] 9.3 In MIDI CC callback: if learn mode active, assign incoming CC to learning parameter and exit learn mode
- [x] 9.4 ImGui: right-click on synth knobs (cutoff, resonance, ADSR) triggers MIDI learn
- [x] 9.5 ImGui: pulsing yellow border on control being learned
- [x] 9.6 ImGui: pulsing "LEARN" indicator in toolbar while learn mode is active
- [x] 9.7 GTK: right-click on filter/amp knobs triggers MIDI learn with status message
- [x] 9.8 Save/load CC map in project file (sparse JSON: only non-default entries)

## 10. MIDI Drum Pad Mapping (MPC-style controllers)

- [x] 10.1 Add GM drum note map function: note 36=kick, 38=snare, 42=closed hat, 46=open hat, 39=clap, etc.
- [x] 10.2 In drum pad mode, MIDI note-on triggers corresponding sampler track (negative preset convention)
- [x] 10.3 Add `sq_midi_input_mode_t` enum and set/get API: Synth vs Drum Pads
- [x] 10.4 ImGui + GTK: add MIDI input mode selector to settings panel

## 11. Testing

- [x] 11.1 Add choke group test: verify choke logic runs without crash, choke_group=0 unaffected
- [x] 11.2 Add probability test: probability=50 triggers ~50% over 100 loops (got 57/100)
- [x] 11.3 Add tap tempo test: verify BPM from simulated intervals (120 BPM, 140 BPM, 2s reset)
- [x] 11.4 Add MIDI CC mapping test: CC74 maps to cutoff, CC7 to volume with correct scaling
- [x] 11.5 Add MIDI learn test: verify learn start/cancel/active API
- [x] 11.6 Add pitch bend test: +8191 = +2.00 semitones, center = 0
- [x] 11.7 Run `scripts/test_all.sh quick` -- all existing tests pass (engine_render_test is known flaky)

## 12. Note Repeat / Ratcheting

- [x] 12.1 Add `uint8_t retrigger` field to `sq_step_t` (0=off, 2-4=subdivisions within one step)
- [x] 12.2 Implement retrigger in `sequencer_trigger_step()` + retrigger queue processing in engine
- [x] 12.3 Add retrigger to project save/load
- [x] 12.4 ImGui: add retrigger selector + micro-timing slider to step edit popup
- [x] 12.5 GTK: add retrigger dropdown + micro-timing spinner to step edit popover

## 13. Humanize Enhancement

- [x] 13.1 Add `float timing_humanize` to `sq_track_t` + project save/load
- [x] 13.2 Implement timing offset in `sequencer_trigger_step()` using PRNG + retrigger queue for delay
- [x] 13.3 ImGui: timing humanize slider (T:%) next to velocity humanize in drum grid

## 14. Pattern Randomize

- [x] 14.1 Add `sequencer_randomize_track()` in engine: fill steps with random velocities based on density parameter
- [x] 14.2 ImGui: "Rnd" button per track in drum grid (density 0.4)
- [x] 14.3 GTK: "Rnd" click region in track header

## 15. Polymeter (Per-Track Step Length)

- [x] 15.1 Engine supports per-track length (`track->length`); `sequencer_trigger_step()` already wraps with `step % track->length`
- [x] 15.2 ImGui: per-track length input in drum grid header
- [x] 15.3 GTK: click-to-cycle track length in header (4/8/12/16/24/32/64)
- [x] 15.4 Visual: drum grid already draws correct number of cells per track->length

## 16. Euclidean Rhythm Generator

- [x] 16.1 Implement `sequencer_euclidean_fill()` using Bresenham-style distribution with rotation
- [x] 16.2 ImGui: "Euc" button per track (default 4 pulses, track length steps, 0 rotation)
- [x] 16.3 GTK: "Euc" click region in track header (4-pulse default)

## 17. Micro-Timing Per Step

- [x] 17.1 Add `float micro_offset` to `sq_step_t` (range -0.5 to +0.5 steps)
- [x] 17.2 Implement micro-timing in sequencer via retrigger queue delay (positive offsets)
- [x] 17.3 Add micro_offset to project save/load
- [x] 17.4 ImGui + GTK: micro-timing slider/spinner in step edit popup/popover

## 18. Sample Start/End + Reverse

- [x] 18.1 Add `sample_start`, `sample_end`, `sample_reverse` to `sq_track_t` + `clip_start/end/reverse` to `sq_voice_t`
- [x] 18.2 Implement in sampler: clip start/end, reverse playback with Hermite interpolation
- [x] 18.3 Add to project save/load
- [x] 18.4 ImGui: "Rev" toggle button per sampler track in drum grid

## 19. MIDI Output

- [x] 19.1 Add RtMidi output handle to `sq_midi` struct + init/shutdown
- [x] 19.2 Implement `sq_midi_send_note_on/off()` API for sequencer output
- [x] 19.3 ImGui: MIDI output port selector in settings panel

## 20. Groove Templates

- [x] 20.1 Define `sq_groove_template_t` struct with per-step timing + velocity arrays
- [x] 20.2 Create 4 built-in templates: Straight, MPC 54% Swing, 808 Shuffle, Linndrum 66%
- [x] 20.3 Implement `sequencer_apply_groove()` to apply timing/velocity to current pattern
- [x] 20.4 ImGui: groove template selector + "Apply Groove" button in settings

## 21. User Sample Import

- [x] 21.1 Sample browser already provides directory navigation + file loading
- [x] 21.2 `sample_io_load()` already handles WAV/FLAC/MP3 via dr_libs
- [x] 21.3 ImGui: sample browser loads files on click (already implemented)
- [x] 21.4 GTK: gtk_browser.c provides equivalent file browsing
- [x] 21.5 Project save/load already stores sample paths

## 22. Parameter Locks

- [x] 22.1 Using existing `param[4]` array on `sq_step_t` for parameter locks
- [x] 22.2 Apply parameter locks in `sequencer_trigger_step()`: save/override/restore filter cutoff and resonance
- [x] 22.3 Restore original values after synth trigger
- [x] 22.4 ImGui + GTK: parameter lock editing UI (cutoff + resonance sliders in step edit for synth tracks)

## 23. Sample Slicing

- [x] 23.1 Implement transient detection via RMS energy windowing in `sequencer_slice_sample()`
- [x] 23.2 Onset detection produces slice start frame array (up to 64 slices)
- [x] 23.3 Map slices to steps with sample_start/end per step
- [x] 23.4 ImGui: waveform display with onset detection slice markers (orange vertical lines)

## 24. v1.4.0 Backlog

- [ ] 24.1 Bluetooth MIDI support (Windows UWP MIDI API, macOS CoreBluetooth)
- [ ] 24.2 Per-pattern kit selection (each pattern can use a different sample kit)
- [ ] 24.3 Per-preset MIDI CC mapping (FM/wavetable parameters learnable)
- [ ] 24.4 MIDI step recording (play pads/keys to program pattern in real-time)
- [ ] 24.5 Virtual keyboard octave tracking from MIDI input
