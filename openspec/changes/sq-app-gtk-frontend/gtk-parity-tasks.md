# GTK Frontend Parity Tasks

Gap analysis: what the GTK frontend is missing vs the ImGui frontend.
Reference: `docs/FRONTEND_FEATURES.md`

## Status Key
- [x] = implemented
- [ ] = missing / needs work

---

## 1. Toolbar Parity

- [x] Play/Stop button
- [x] BPM slider
- [x] Swing slider
- [x] Volume slider
- [ ] Replace sliders with rotary knobs (arc + value display, drag interaction)
- [ ] Logo with animated pulse effect
- [ ] REC button with red highlight
- [x] Mode selector (PAT/SONG/PERF) buttons with active highlight
- [x] Pattern selector row (numbered buttons, add pattern)
- [x] Panel buttons: active CSS class when panel is open
- [x] Status message in toolbar
- [x] EXPORT button wired to export dialog
- [ ] Window controls (minimize/maximize/close) for standalone
- [ ] Toolbar drag to move window

## 2. Drum Grid Parity

- [x] Track rows with step columns
- [x] Click-to-toggle steps
- [x] Velocity-based opacity
- [x] Track color palette
- [x] Playhead column highlight + line
- [x] Track labels (sample/synth name with preset names)
- [x] Mute/Solo buttons (clickable)
- [x] Selected track highlight
- [x] Track controls column (200px): color bar, type badge, volume bar
- [ ] Track type selector button (Sampler/Synth/SF2 — currently display-only)
- [ ] Sample/preset dropdown in track controls
- [ ] Right-click drag: velocity/pitch adjustment
- [ ] Right-click popup: velocity/pitch editor
- [x] Velocity number + pitch offset displayed in cells
- [x] Cell glow effects (rounded corners, inner glow)
- [x] Beat-aligned shading (alternating 8-step groups)
- [x] Separator line before synth tracks ("SYNTH")
- [ ] Add Track buttons (+ Sampler, + Synth)
- [ ] Kit selector dropdown
- [x] Multi-step drag editing (drag to paint/erase steps)
- [x] Vertical scroll for many tracks
- [ ] Hover highlight on cells

## 3. Piano Roll Parity

- [x] Note grid with Cairo rendering
- [x] Piano key labels (left column, C notes labeled)
- [x] Click to place/delete notes
- [x] Playhead line (red, 2px)
- [x] Velocity-colored note bars (height varies by velocity)
- [x] Velocity text on wide notes
- [x] Scroll wheel for pitch scrolling
- [x] Alternating beat shading (black/white key rows)
- [x] Bold grid lines every 4 steps
- [x] Rounded note corners
- [ ] Left-click + drag to extend note length
- [ ] Right-click + drag to erase

## 4. Synth Editor Parity

- [x] Preset selector dropdown
- [x] Mode selector dropdown (Subtractive/FM/Wavetable)
- [x] Osc1/Osc2 waveform selectors
- [x] Oscillator mix + detune sliders
- [x] Unison voices + spread controls
- [x] Filter type selector (LP/HP/BP)
- [x] Filter cutoff/reso/env depth sliders
- [x] Filter ADSR sliders
- [x] Amp ADSR sliders + curve visualization
- [x] LFO waveform + destination selectors
- [x] LFO rate + depth sliders
- [x] BPM Sync toggle + division selector
- [x] FM algorithm selector + per-operator ratio/level/feedback/ADSR
- [x] Wavetable position/env/lfo sliders (when WT mode)
- [x] Dynamic rebuild on track/preset change
- [ ] Interactive ADSR curve (draggable control points)
- [ ] Filter response curve visualization
- [ ] FM algorithm diagram (operator boxes + modulation arrows)
- [ ] Wavetable bank selector + waveform visualizer
- [ ] 4-column layout (Subtractive mode)
- [ ] Replace sliders with rotary knobs where appropriate

## 5. Mixer / Effects Parity

- [x] Per-track volume slider (vertical)
- [x] Per-track pan slider
- [x] Mute button per track
- [x] Scrollable channel strips
- [ ] Track level meters (LED-style, 12 segments, green/yellow/red)
- [ ] Master output meter (stereo L/R)
- [ ] Effects panel with Prev/Next navigation (Master + per-track)
- [ ] 3 effect slots per bus
- [ ] Effect type selector (None/Filter/Delay/Reverb/Overdrive/Fuzz/Chorus)
- [ ] Bypass checkbox per effect
- [ ] Effect-specific parameter controls

## 6. Sample Browser Parity

- [x] Sample list with loaded samples
- [x] Click to assign sample to selected track
- [ ] Directory navigation (current path, Up, Refresh)
- [ ] File size display
- [ ] Waveform preview (40px, blue line)
- [ ] Audition button (preview playback)
- [ ] Sample info (duration, sample rate)
- [ ] "Load Sample" button for adding new samples

## 7. Virtual Keyboard Parity

- [x] 3-octave piano rendering (white + black keys)
- [x] Click to play note
- [x] Drag for glissando
- [x] Note-off on release
- [x] Octave shift buttons (<< >>) with range display
- [x] Preset name display
- [x] C note labels on keys
- [x] Held note name display
- [ ] QWERTY keyboard mapping (Z-row + Q-row + number/letter sharps)

## 8. Arrangement Parity

- [x] Panel visible in SONG/PERFORM modes
- [x] Section list with clickable buttons
- [x] Current/queued section highlighting
- [x] Add section button
- [x] Section editor (pattern selector, repeat count, remove)
- [x] Status line (playing/queued info)
- [x] Click section to jump/queue
- [ ] Section color coding (8-color palette in button backgrounds)

## 9. Pattern Presets Dialog

- [ ] 12 drum pattern presets with dropdown
- [ ] 10 bass line presets with dropdown
- [ ] Apply Drums / Apply Bass / Apply Both buttons
- [ ] BPM suggestion buttons
- [ ] Clear Pattern button
- [ ] Tips text

## 10. Export Dialog

- [x] Filename text input (default "output.wav")
- [x] Format selector (WAV 16/24/32, MP3 128-320k)
- [x] Bar count slider (1-32)
- [x] Duration display
- [x] Export + Close buttons
- [x] Status message (success/error)
- [x] EXPORT button in toolbar to open dialog

## 11. Theme System

- [x] Dark CSS theme (matching ImGui Dark)
- [x] Light CSS theme (matching ImGui Light)
- [x] Hacker theme (green CRT)
- [x] Midnight theme (purple/blue)
- [x] Amber theme (warm CRT)
- [x] Ctrl+T cycles themes with status message
- [x] Theme provider properly removes old CSS on switch
- [ ] Theme selector popup/menu
- [ ] User theme loading from JSON
- [ ] Theme-aware Cairo drawing colors (pad glow, etc.)

## 12. Missing Interactions

- [ ] Right-click context menus
- [ ] Scroll wheel on dropdowns to cycle values
- [ ] Double-click knobs to reset
- [ ] Undo/redo visual feedback
- [ ] Native file dialogs (GtkFileDialog for save/load)

## 13. Testing (after implementation)

- [ ] T1: Launch GTK frontend, verify dark theme, toolbar visible, drum grid with 10 tracks
- [ ] T2: Click PLAY — verify playhead animates across grid, audio plays
- [ ] T3: Click steps in drum grid — verify toggle on/off, velocity coloring
- [ ] T4: Click synth track row — verify piano roll + synth editor appear
- [ ] T5: Change synth preset via dropdown — verify controls rebuild
- [ ] T6: Drag synth editor sliders (cutoff, ADSR) — verify audio changes in real-time
- [ ] T7: Click KEYS — verify keyboard appears, click keys plays notes, drag for glissando
- [ ] T8: Click BROWSE — verify sample list appears, click sample assigns to selected track
- [ ] T9: Click MIXER — verify channel strips with volume/pan per track
- [ ] T10: Test keyboard shortcuts: Space (play/stop), 1-9 (patterns), Ctrl+Z (undo), Escape (quit)
- [ ] T11: Verify pattern selector: click numbered buttons, add new pattern with +
- [ ] T12: Change mode to SONG — verify arrangement panel appears with section controls
- [ ] T13: Test export dialog: enter filename, select format, export WAV
- [ ] T14: Ctrl+T — verify theme cycles (dark/hacker/midnight/amber/light)
- [ ] T15: Resize window — verify all panels scale correctly, min size 800x500 enforced
- [ ] T16: Verify default build (no -DBUILD_GTK) still works without GTK dependency
- [ ] T17: Verify ImGui standalone + plugins still build and pass all tests after changes
