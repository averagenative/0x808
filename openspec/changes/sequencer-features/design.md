## Context

0x808 is a 4-layer drum machine (Engine → App → GUI → Host). The sequencer triggers samples and synth voices per-step with velocity, pitch, pan, and note length. Steps are stored in `sq_step_t` structs within `sq_track_t` arrays. The sequencer advances in `sequencer_trigger_step()` called from the audio thread.

Both ImGui and GTK frontends must implement identical UI for any new feature (frontend parity rule). The engine is pure C99 with no allocations in the audio path.

## Goals / Non-Goals

**Goals:**
- Add choke groups, step probability, and tap tempo for v1.3.0
- Keep all new logic real-time safe (no malloc in audio path)
- Maintain frontend parity between ImGui and GTK
- Minimal struct changes to avoid breaking project file compatibility

**Non-Goals:**
- v1.4.0 backlog features (polymeter, euclidean, parameter locks, etc.)
- MIDI-triggered choke groups (only sequencer-triggered for now)
- Per-step probability display in the drum grid (just edit via right-click or detail panel)

## Decisions

### 1. Choke groups: uint8 field on sq_track_t

Add `uint8_t choke_group` to `sq_track_t` (0 = no group, 1-8 = group ID). When `sequencer_trigger_step()` fires a track, iterate other tracks in the same pattern — if any share the same non-zero choke group, silence their currently playing sample or release their synth voice.

**Why not per-step?** Choke groups are a track-level property in every real drum machine. Per-step would add complexity with no musical benefit.

**Silencing mechanism:** For samplers, set the sample playback position to end (already stops on next process). For synths, call `envelope_release()` on matching voices.

### 2. Step probability: uint8 field on sq_step_t

Add `uint8_t probability` to `sq_step_t` (0 = use default 100%, 1-100 = percentage). Evaluated in `sequencer_trigger_step()` using the engine's existing xorshift32 PRNG (`engine->rng_state`). If `(rng % 100) >= probability`, skip the step.

**Why uint8 not float?** Keeps `sq_step_t` compact. 1% granularity is sufficient.

**Default 0 means 100%:** Avoids breaking existing patterns where probability is uninitialized (zero-filled).

### 3. Tap tempo: app controller logic, not engine

Tap tempo is a UI concern — it computes BPM from inter-tap timing and sends a `CMD_SET_BPM` command. Lives in `sq_app.c` with state (last tap time, tap count, running average). Requires a new key binding and toolbar button.

**Algorithm:** Track last 4 taps, compute average interval, convert to BPM. Reset if gap > 2 seconds. Clamp to 20-300 BPM range.

### 4. GUI: choke group dropdown per track, probability in step edit

- **Choke group**: Small dropdown/number selector in the track header area (drum grid left column). Values: Off, 1-8.
- **Probability**: Shown when editing a step (right-click or detail view). Slider or number input 0-100%.
- **Tap tempo**: Button in toolbar next to BPM display + keyboard shortcut (T key).

## Risks / Trade-offs

- **[Choke group iteration cost]** → Iterating all tracks per trigger is O(num_tracks). With max 16 tracks this is negligible in the audio path.
- **[Probability PRNG determinism]** → Using the engine's shared PRNG means probability sequences aren't reproducible across renders. Acceptable for a creative tool — producers expect probability to vary. → If deterministic export is needed later, seed the PRNG at pattern start.
- **[sq_step_t size increase]** → Adding 1 byte (probability) to sq_step_t. With 64 patterns × 32 tracks × 64 steps, this adds ~128KB. Negligible.
- **[Project file compat]** → New fields will be zero in old project files, which maps to "no choke group" and "100% probability" — backwards compatible.
