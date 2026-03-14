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
- [x] REC button with red highlight
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
- [x] Right-click popup: velocity/pitch editor with copy/paste
- [x] Velocity number + pitch offset displayed in cells
- [x] Cell glow effects (rounded corners, inner glow)
- [x] Beat-aligned shading (alternating 8-step groups)
- [x] Separator line before synth tracks ("SYNTH")
- [x] Add Track buttons (+ Sampler, + Synth)
- [ ] Kit selector dropdown
- [x] Multi-step drag editing (drag to paint/erase steps)
- [x] Vertical scroll for many tracks
- [x] Hover highlight on cells

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
- [x] Oscillator mix + detune knobs
- [x] Unison voices + spread controls
- [x] Filter type selector (LP/HP/BP)
- [x] Filter cutoff/reso/env depth knobs
- [x] Filter ADSR knobs
- [x] Amp ADSR knobs + curve visualization
- [x] LFO waveform + destination selectors
- [x] LFO rate + depth knobs
- [x] BPM Sync toggle + division selector
- [x] FM algorithm selector + diagram + per-operator knobs
- [x] Wavetable position/env/lfo knobs + waveform visualizer
- [x] Dynamic rebuild on track/preset change
- [x] Interactive ADSR curve (draggable control points)
- [x] Filter response curve visualization
- [x] FM algorithm diagram (operator boxes + modulation arrows)
- [x] Wavetable waveform visualizer
- [x] 4-column layout (Subtractive mode)
- [x] Rotary knobs for all parameters
- [x] Scroll wheel on dropdowns to cycle values

## 5. Mixer / Effects Parity

- [x] Per-track volume slider (vertical)
- [x] Per-track pan knob
- [x] Mute button per track
- [x] Scrollable channel strips with scroll buttons
- [x] Track level meters (LED-style, 12 segments, green/yellow/red)
- [x] Master output meter (stereo L/R)
- [x] Effects panel with Prev/Next navigation (Master + per-track)
- [x] 3 effect slots per bus
- [x] Effect type selector (None/Filter/Delay/Reverb/Overdrive/Fuzz/Chorus)
- [x] Bypass checkbox per effect
- [x] Effect-specific parameter controls
- [x] MIXER/FX toolbar button (synced label between ImGui and GTK)
- [x] Alternating strip colors for visual separation

## 6. Sample Browser Parity

- [x] Sample list with loaded samples
- [x] Click to assign sample to selected track
- [x] Directory navigation (current path, Up, Refresh)
- [x] File size display
- [ ] Waveform preview (40px, blue line)
- [ ] Audition button (preview playback)
- [ ] Sample info (duration, sample rate)
- [x] "Load Sample" button with file dialog

## 7. Virtual Keyboard Parity

- [x] 3-octave piano rendering (white + black keys)
- [x] Click to play note
- [x] Drag for glissando
- [x] Note-off on release
- [x] Octave shift buttons (<< >>) with range display
- [x] Preset name display
- [x] C note labels on keys
- [x] Held note name display
- [x] QWERTY keyboard mapping (Z-row + Q-row + number/letter sharps)

## 8. Arrangement Parity

- [x] Panel visible in SONG/PERFORM modes
- [x] Section list with clickable buttons
- [x] Current/queued section highlighting
- [x] Add section button
- [x] Section editor (pattern selector, repeat count, remove)
- [x] Status line (playing/queued info)
- [x] Click section to jump/queue
- [x] Section color coding (8-color palette in button backgrounds)

## 9. Pattern Presets Dialog

- [x] 12 drum pattern presets with dropdown
- [x] 10 bass line presets with dropdown
- [x] Apply Drums / Apply Bass / Apply Both buttons
- [x] BPM suggestion buttons
- [x] Clear Pattern button
- [x] Tips text

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
- [x] Theme selector popup/menu
- [ ] User theme loading from JSON
- [ ] Theme-aware Cairo drawing colors (pad glow, etc.)

## 12. Missing Interactions

- [x] Right-click context menus (drum grid velocity/pitch editor)
- [x] Scroll wheel on dropdowns to cycle values
- [ ] Double-click knobs to reset
- [ ] Undo/redo visual feedback
- [x] Native file dialogs (GtkFileDialog for sample loading)

## 13. Help Dialog (both frontends)

- [ ] Help button (?) in toolbar — clickable
- [ ] Popup/panel listing keyboard shortcuts (Space, 1-9, Ctrl+Z, Ctrl+T, etc.)
- [ ] Mouse controls reference (left-click, right-click, drag, scroll wheel)
- [ ] QWERTY keyboard mapping diagram
- [ ] Brief feature descriptions
