# 0x808 Frontend Feature Set

Reference document for maintaining parity across frontends (ImGui, GTK, future).
Each section describes what a compliant frontend MUST implement.

---

## 1. Window & Chrome

- [ ] Default size: 1280x720, minimum 800x500
- [ ] Dark theme by default
- [ ] Window title: "0x808"
- [ ] Borderless frame with resize grip (standalone only)
- [ ] Window border outline (2px)

## 2. Toolbar (Fixed Top Bar, ~80px)

### 2.1 Logo
- [ ] "0x808" text, large/bold
- [ ] Animated pulse effect (sinusoidal alpha oscillation)
- [ ] Glowing box border around logo

### 2.2 Transport Controls
- [ ] PLAY/STOP toggle button (green glow when playing)
- [ ] REC button with red highlight when active
- [ ] Visual separator lines between sections

### 2.3 Parameter Knobs
- [ ] BPM rotary knob (40-300), arc display with value text
- [ ] Swing rotary knob (0-100%)
- [ ] Volume rotary knob (0-100%, default 80%)
- [ ] All knobs: vertical drag, shift+drag fine control, double-click reset

### 2.4 Mode Selector
- [ ] PAT / SONG / PERF buttons (green / blue / pink)
- [ ] Glow effect on active mode

### 2.5 Panel Toggle Buttons
- [ ] PRESETS (orange), PIANO (blue), KEYS (orange), FX (purple), BROWSE (green)
- [ ] Asterisk (*) indicator when panel is active
- [ ] Glow + highlight color on active state

### 2.6 Window Controls (standalone only)
- [ ] Minimize, Maximize, Close buttons (right-aligned)
- [ ] Close button red tint
- [ ] Toolbar drag to move window, double-click to maximize

### 2.7 Pattern Selector Row
- [ ] Numbered pattern buttons (1-N), 9 visible at a time
- [ ] Active pattern highlighted green
- [ ] Scroll buttons (< >) for paging
- [ ] Add pattern (+) button
- [ ] Status message display (auto-dismiss timer)

## 3. Drum Grid

### 3.1 Track Controls (left column, ~200px)
- [ ] Track name button (color-tinted by track color)
- [ ] Selected track highlight
- [ ] Mute (M) button — red when active
- [ ] Solo (S) button — green when active
- [ ] Volume slider with label
- [ ] Humanize slider with label
- [ ] Track type selector (Sampler / Synth / SF2) — color-coded
- [ ] Sample/preset dropdown selector

### 3.2 Step Grid
- [ ] Colored cells: velocity-based opacity, track color
- [ ] Inactive cells: dark with beat-aligned shading (every 4 steps darker)
- [ ] Active cells: concentric glow rings, inner glow, bright edge
- [ ] Velocity number displayed centered in active cells
- [ ] Pitch offset arrows (cyan up / orange down) in cells
- [ ] Playhead column highlight during playback
- [ ] Hover highlight on cells
- [ ] Cell rounding: 6px radius

### 3.3 Interactions
- [ ] Left-click drag: toggle pads on/off
- [ ] Right-click drag: continuous velocity/pitch adjustment
- [ ] Right-click release: velocity/pitch editor popup (260x180px)
- [ ] Click track name: select track

### 3.4 Additional Elements
- [ ] Separator line before synth tracks ("-- SYNTH --")
- [ ] "+ Sampler Track" and "+ Synth Track" buttons
- [ ] Kit selector dropdown at top
- [ ] 8-color track palette (red, blue, green, yellow, purple, orange, cyan, pink)

## 4. Piano Roll

### 4.1 Layout
- [ ] Piano key labels (left 50px) — C notes labeled, black/white key shading
- [ ] Note range: C2-C6 (MIDI 36-84)
- [ ] Row height: 14px per note
- [ ] Header: 30px with track info

### 4.2 Grid
- [ ] Alternating beat shading (every 8 steps)
- [ ] Black key rows darker than white key rows
- [ ] Beat grid lines: bold every 4 steps, dim every step
- [ ] Playhead: red vertical line (2px)

### 4.3 Notes
- [ ] Velocity-colored bars with intensity scaling
- [ ] Height varies by velocity (70-100% of row)
- [ ] Rounded corners (2px)
- [ ] Velocity text on notes wider than 20px

### 4.4 Interactions
- [ ] Left-click empty: place note (vel=100, length=1)
- [ ] Left-click + drag: extend note length
- [ ] Right-click + drag: erase notes
- [ ] Scroll wheel: vertical pitch scrolling (±2 notes per tick)

## 5. Synth Editor

### 5.1 Header
- [ ] Preset selector dropdown (scroll wheel cycling)
- [ ] Mode selector: Subtractive / FM / Wavetable
- [ ] Preset name display

### 5.2 Subtractive Mode (4-column layout)
- [ ] **Oscillators:** Osc1/Osc2 waveform selectors, Mix, Detune, Unison voices, Spread
- [ ] **Filter:** Type selector, Cutoff (20-20kHz), Resonance, Env depth, Filter ADSR sliders, Filter response curve visualization, Filter ADSR curve
- [ ] **Amp Envelope:** ADSR sliders, interactive ADSR curve visualization with draggable control points
- [ ] **LFO:** Waveform, Destination, Rate, Depth, BPM Sync toggle, Sync division selector

### 5.3 FM Mode
- [ ] Algorithm selector (8 algorithms)
- [ ] FM algorithm diagram with operator boxes and modulation arrows
- [ ] 4 operator columns: Frequency ratio, Level, Feedback, per-operator ADSR
- [ ] Carrier (green) vs Modulator (blue) color coding

### 5.4 Wavetable Mode (3-column layout)
- [ ] Bank selector, Position slider, Env/LFO modulation sliders
- [ ] Waveform visualizer (single-cycle display)
- [ ] Amp envelope ADSR + curve
- [ ] Position envelope ADSR + curve

### 5.5 ADSR Curve Visualization
- [ ] Dark background, blue curve line (1.5px)
- [ ] Draggable control points (white circles, yellow when active)
- [ ] Phase labels: A, D, S, R

### 5.6 Filter Response Curve
- [ ] Orange curve line showing frequency response
- [ ] Cutoff frequency indicator line

## 6. Mixer / Effects Panel

### 6.1 Track Level Meters
- [ ] Vertical LED-style meters per track (12 segments)
- [ ] Color zones: green (<0.6), yellow (0.6-0.85), red (>0.85)
- [ ] Track number labels below

### 6.2 Effects Panel
- [ ] Browse bar with Prev/Next navigation (Master Bus + per-track)
- [ ] 3 effect slots per bus
- [ ] Effect type selector: None, Filter, Delay, Reverb, Overdrive, Fuzz, Chorus
- [ ] Bypass checkbox per slot
- [ ] Type-specific controls (cutoff/reso, time/feedback/wet, room/damp, drive/tone, rate/depth, etc.)
- [ ] BPM Sync for Delay with division selector

### 6.3 Master Output Meter
- [ ] Stereo L/R meters with LED styling
- [ ] Same color zones as track meters

## 7. Sample Browser

- [ ] Current path display with "Up (..)", "Refresh", "Close (X)" buttons
- [ ] File list: Name, Size, Action columns
- [ ] Directory navigation (double-click or Open button)
- [ ] "Load Sample" button (enabled when file selected)
- [ ] Sample count display
- [ ] Waveform preview (40px height, blue line, center reference)
- [ ] "Audition" button for preview playback
- [ ] Sample info: duration, sample rate

## 8. Virtual Keyboard

- [ ] 3-octave piano (C3-B5 default), white and black keys
- [ ] Octave shift buttons (<< >>), range -2 to +4
- [ ] Octave range label ("C3-C6")
- [ ] Preset name display
- [ ] White keys: light gray, blue when pressed
- [ ] Black keys: dark, darker blue when pressed
- [ ] C note labels on white keys
- [ ] Held note name display below keyboard

### 8.1 Interactions
- [ ] Click to play note
- [ ] Drag across keys for glissando
- [ ] Release for note-off
- [ ] QWERTY mapping: Z-row lower octave, Q-row upper octave, number/letter row for sharps

## 9. Arrangement (Song/Perform Mode)

- [ ] Mode selector: PATTERN (green), SONG (blue), PERFORM (pink)
- [ ] Section list with 8-color palette
- [ ] Current section: full brightness
- [ ] Queued section: 50% brightness + yellow border
- [ ] Add section (+) button
- [ ] Section editor: pattern selector, repeat count (1-16), Remove button
- [ ] Status line showing current playback position
- [ ] Click section: jump (PATTERN/SONG) or queue (PERFORM)

## 10. Pattern Presets Dialog

- [ ] 12 drum pattern presets (House, Boom Bap, Trap, DnB, Reggaeton, Disco, Techno, Breakbeat, Lo-fi, Rock, Afrobeat, Bossa Nova)
- [ ] 10 bass line presets (Octave, Root-Fifth, Walking, Sub, 808 Trap, Reese, Acid 303, Disco Funk, Dembow, Minimal)
- [ ] BPM suggestion button per preset
- [ ] "Apply Drums", "Apply Bass", "Apply Both" buttons
- [ ] "Clear Pattern" button (red)
- [ ] Tips text
- [ ] Toggle behavior: toolbar button opens/closes dialog (click again to dismiss)
- [ ] Dialog closeable via X button or toolbar toggle for piano roll usage

## 11. Export Dialog

- [ ] Filename text input (default "output.wav")
- [ ] Format selector: WAV 16/24/32-float, MP3 128k/192k/256k/320k
- [ ] Bar count slider (1-32)
- [ ] Duration display ("N bars at BPM")
- [ ] Export + Close buttons
- [ ] Status: green success or red error message
- [ ] Toggle behavior: toolbar button opens/closes dialog (click again to dismiss)
- [ ] Dialog closeable via X button or toolbar toggle

## 12. Theme System

### 12.1 Built-in Themes (7)
- [ ] Dark (blue accents, gray backgrounds)
- [ ] Light (warm gray, blue accents)
- [ ] Hacker (green-on-black CRT, cyan accents)
- [ ] Midnight (purple/blue, cool mood)
- [ ] Amber (warm CRT, amber accents)
- [ ] Vaporwave (pink/cyan/purple synthwave)
- [ ] Neon (hot pink/magenta, cyan accents)

### 12.2 User Themes
- [ ] Load from JSON files in {base_dir}/themes/
- [ ] ~60 configurable color keys + frame_border
- [ ] Theme selector popup with all built-in + user themes

### 12.3 Theme-Aware Elements
- [ ] Pad glow colors change per theme
- [ ] Clear color (background) per theme
- [ ] All UI elements respect theme colors

## 13. Keyboard Shortcuts

- [ ] Space: Play/Stop toggle
- [ ] Escape: Quit
- [ ] 1-9: Pattern select (always active)
- [ ] Ctrl+S: Save project
- [ ] Ctrl+O: Open project
- [ ] Ctrl+T: Toggle theme
- [ ] Ctrl+C: Copy pattern
- [ ] Ctrl+V: Paste pattern
- [ ] Ctrl+Z: Undo
- [ ] Ctrl+Shift+Z: Redo
- [ ] = / +: Add new pattern
- [ ] QWERTY piano (when keyboard panel visible)

## 14. Rotary Knob Widget

- [ ] 270-degree arc sweep
- [ ] Dark background circle
- [ ] Track arc (dim) + Value arc (accent color)
- [ ] Indicator line + dot at current value
- [ ] Value text display
- [ ] Variants: standard, mini, inline
- [ ] Interactions: vertical drag, shift+fine, double-click reset
