# 0x808 Synthesizer Guide

A beginner-friendly guide to understanding synthesis and getting the most out of
the 0x808 built-in synthesizer. No prior knowledge required.

---

## Table of Contents

1. [What is a Synthesizer?](#1-what-is-a-synthesizer)
2. [Oscillators (Sound Source)](#2-oscillators-sound-source)
3. [Filter (Tone Shaping)](#3-filter-tone-shaping)
4. [Envelopes (ADSR)](#4-envelopes-adsr)
5. [LFO (Movement)](#5-lfo-movement)
6. [FM Synthesis](#6-fm-synthesis)
7. [Wavetable Synthesis](#7-wavetable-synthesis)
8. [Quick Recipes](#8-quick-recipes)
9. [Preset Reference](#9-preset-reference)

---

## 1. What is a Synthesizer?

A **synthesizer** generates sound from scratch using math and electronics (or in
our case, code). This is the opposite of a **sampler**, which plays back
recordings of real instruments.

Think of it this way:
- **Sampler** = a photo of a landscape
- **Synthesizer** = a painting of a landscape -- you control every brushstroke

### The Signal Flow

Every sound in the 0x808 synth flows through this chain:

```
                                                  You hear this
                                                       |
  Oscillator(s) ----> Filter ----> Amplifier ----> Output
    "raw sound"      "tone knob"   "volume shape"
        |                |              |
    What note?      Bright/dark?    Fade in/out?
```

The oscillator creates the raw tone. The filter shapes which frequencies you
hear. The amplifier (controlled by an envelope) shapes how the volume changes
over time. Every synth sound is some variation of this chain.

---

## 2. Oscillators (Sound Source)

The oscillator is where sound begins. It generates a repeating waveform --
a shape that cycles hundreds of times per second to create a pitch. The **shape**
of the wave determines the **character** of the sound.

### The Four Waveforms

The 0x808 synth has four basic waveforms:

#### Sawtooth (Saw)
```
    /|  /|  /|  /|
   / | / | / | / |
  /  |/  |/  |/  |
```
**Sounds like:** Buzzy, bright, aggressive. The richest waveform -- it contains
all harmonics (overtones). Think of a violin bow scraping a string, or a
distorted guitar. Great for bass lines, leads, and pads. This is the most
versatile waveform and you will see it in most presets.

#### Square
```
  ___     ___     ___
 |   |   |   |   |   |
 |   |   |   |   |   |
 |   |___|   |___|   |___
```
**Sounds like:** Hollow, woody, like a clarinet or old-school video game music.
Contains only odd harmonics, giving it that distinctive "hollow" quality. Pair it
with a saw wave for a thicker sound.

#### Triangle
```
   /\    /\    /\
  /  \  /  \  /  \
 /    \/    \/    \
```
**Sounds like:** Soft, mellow, flute-like. Similar to a sine wave but with a
touch more character. Contains only odd harmonics at much lower levels than the
square. Good for gentle bass lines and sub-bass.

#### Sine
```
    .---.       .---.
   /     \     /     \
  /       \   /       \
 /         '-'         '-
```
**Sounds like:** Pure, clean, like a tuning fork or a whistle. This is the
simplest waveform -- just one frequency with no overtones at all. Used for
sub-bass, test tones, and as a building block in FM synthesis.

### Osc Mix (Blending Two Oscillators)

The 0x808 has **two oscillators** per voice. The `osc_mix` knob blends between
them:

```
  osc_mix = 0.0          osc_mix = 0.5          osc_mix = 1.0
  All Oscillator 1       Equal blend            All Oscillator 2

  Osc1: ||||||||||||     Osc1: ||||||           Osc1:
  Osc2:                  Osc2: ||||||           Osc2: ||||||||||||
```

Why use two oscillators? Different waveform combinations create richer sounds.
A saw + square blend, for example, gives you the brightness of the saw with the
body of the square.

### Osc2 Detune

The `osc2_detune` parameter shifts the pitch of Oscillator 2 in **semitones**
(the distance between two adjacent piano keys). Range: -24 to +24 semitones.

- **Small detune** (0.05 - 0.15 semitones): Creates a thick, chorused sound as
  the two oscillators slowly drift in and out of phase. This is the secret
  behind classic analog poly-synth sounds.
- **Large detune** (-12 or +12): Shifts by a full octave, adding depth.
- **Musical intervals** (7 = a fifth, 5 = a major third): Creates harmony
  within a single note.

### Unison (Stacking Voices)

Unison takes each note and plays it with **multiple slightly-detuned copies**
(1 to 7 voices), spread across the stereo field.

```
  Unison = 1             Unison = 5
  (single voice)         (5 detuned copies)

     |                  | | | | |
     |                  | | | | |
  center             spread L to R
```

- `unison_voices`: How many copies (1-7). More = thicker but heavier on CPU.
- `unison_detune`: How far apart the copies are in **cents** (1/100th of a
  semitone). Range: 0-50 cents.
  - 5-10 cents: Subtle thickening, like a chorus pedal
  - 15-25 cents: Rich, wide, classic "supersaw" territory
  - 30-50 cents: Extreme, almost dissonant -- used for aggressive EDM sounds

The "SuperSaw" preset uses 5 unison voices at 20 cents of detune. The "JP-8 Pad"
preset maxes out at 7 voices with 16 cents -- about as lush as it gets.

---

## 3. Filter (Tone Shaping)

If the oscillator is the raw paint, the filter is the brush technique. A filter
**removes certain frequencies** from the sound. Think of it like the tone knob
on a guitar -- all the way up is bright and full, rolled back is dark and muffled.

### Filter Types

#### Low Pass (LP) -- the most common
Lets **low frequencies through**, removes high frequencies. This is the filter
you will use 90% of the time.

```
  Volume
  |
  |______
  |      \
  |       \
  |        \___________
  |________________________
         Cutoff        Frequency -->

  "Everything below the cutoff passes through"
```

Turn the cutoff **down** = darker, muddier.
Turn the cutoff **up** = brighter, more present.

The "wah" sound you hear when a bass note hits? That is a low-pass filter
with its cutoff sweeping from high to low via an envelope.

#### High Pass (HP)
The opposite of low pass. Lets **high frequencies through**, removes the bass.

```
  Volume
  |
  |            ________
  |           /
  |          /
  |_________/
  |________________________
         Cutoff        Frequency -->

  "Everything above the cutoff passes through"
```

Useful for thinning out sounds, creating risers, or making room for a separate
bass instrument. The "MS-20 Growl" preset uses a high-pass filter for its
aggressive character.

#### Band Pass (BP)
Removes **both** the highs and the lows, leaving only a band in the middle.

```
  Volume
  |
  |        __
  |       /  \
  |      /    \
  |_____/      \_____
  |________________________
         Cutoff        Frequency -->

  "Only frequencies near the cutoff pass through"
```

Creates thin, nasal, "telephone" or "radio" sounds. The "Stab" preset uses
band pass for its punchy, narrow character.

### Cutoff (20 Hz - 20,000 Hz)

The cutoff frequency is **where the filter starts working**. In a low-pass
filter:

- Cutoff at 20,000 Hz = filter is wide open, you hear everything
- Cutoff at 5,000 Hz = upper harmonics are softened
- Cutoff at 500 Hz = very dark and muffled
- Cutoff at 100 Hz = almost nothing left but a low rumble

Most interesting filter sounds happen when the cutoff is **moving** -- sweeping
up or down via an envelope or LFO.

### Resonance (Q: 0.5 - 20.0)

Resonance **emphasizes the frequencies right at the cutoff point**, creating a
peak. Think of it as a volume boost at exactly the cutoff frequency.

```
  Low resonance:          High resonance:

  |______                 |   /\
  |      \                |  /  \
  |       \               | /    \
  |        \___           |/      \___
  |_______________        |_______________
       Cutoff                  Cutoff
```

- **Low Q (0.5 - 2.0):** Gentle, smooth filtering. Good for pads and subtle tone
  shaping.
- **Medium Q (3.0 - 6.0):** Noticeable "squelch" or "quack." The sweet spot for
  bass lines.
- **High Q (8.0 - 15.0):** Very squelchy, almost whistling. Think acid house
  ("303 Acid" preset uses Q of 12.0).
- **Very High Q (15.0 - 20.0):** Near self-oscillation, where the filter starts
  to ring and produce its own tone. Used in the "MS-20 Growl" preset (Q = 15.0).

### Filter Envelope Depth

This controls **how much the filter envelope affects the cutoff**. The envelope
(explained in the next section) temporarily pushes the cutoff up or down when
a note is played.

- **Positive depth** (e.g., 3000.0): The cutoff sweeps UP when a note triggers,
  then falls back. This creates the classic "bow" or "wah" attack on bass and
  lead sounds.
- **Negative depth** (e.g., -1500.0): The cutoff sweeps DOWN, creating a
  "closing" sound. Used in the "SH Noise" preset.
- **Zero depth**: The filter stays static -- good for pads or when you want a
  consistent tone.

Higher values = more dramatic sweep. The "303 Acid" preset has a filter envelope
depth of 8000 Hz, which is what gives it that extreme squelchy sweep.

---

## 4. Envelopes (ADSR)

An envelope is a **shape that changes a value over time** when a note is played.
Imagine drawing a line that goes up, then down, then holds, then fades -- that
line is an envelope.

Every note you play goes through four stages. This is called **ADSR**:

```
  Level
  1.0 |     /\
      |    /  \
      |   /    \--------\
      |  /   D  | S     | \
      | / A     |       |  \ R
  0.0 |/________|_______|____\____
      |  note   |       | note
      |  ON     |       | OFF
      |         |       |
      Attack  Decay   Sustain  Release
```

### Attack (0.001 - 10.0 seconds)

How fast the sound **rises from silence to full volume** when you trigger a note.

- **Fast attack** (0.001 - 0.01s): Instant. The sound hits immediately, like
  tapping a key on a piano or plucking a string. Used for percussive sounds,
  plucks, and bass.
- **Medium attack** (0.05 - 0.3s): A slight fade-in, like bowing a violin.
- **Slow attack** (0.5 - 2.0s): Gradual swell, like a distant pad or choir
  fading in. This is what makes pad sounds feel "floaty."

### Decay (0.001 - 10.0 seconds)

How fast the sound **drops from its peak down to the sustain level**.

After the attack hits full volume, the decay stage brings it back down to the
sustain level. Think of hitting a piano key -- the initial "ping" is louder
than the sustained note. The decay controls how quickly that initial brightness
or loudness fades.

- **Short decay** (0.05 - 0.2s): Quick drop. Good for plucks and percussion.
- **Medium decay** (0.3 - 0.8s): Natural-sounding for keys and leads.
- **Long decay** (1.0 - 3.0s): Slow fade from the peak, good for bells.

### Sustain (0.0 - 1.0, this is a level, not a time)

The **volume level held while the note is still active**. This is the only ADSR
parameter that is a level (not a time).

- **Sustain = 0.0:** Sound dies completely after the decay, even if the note is
  still held. This makes pluck and percussion sounds -- the note rings out and
  then is gone. (See "Bass," "Pluck," and "FM Bell" presets.)
- **Sustain = 0.5 - 0.7:** Sound drops but keeps playing at a moderate level.
  Good for leads and keys.
- **Sustain = 1.0:** No decay at all -- the sound stays at full volume the
  entire time. Good for organs.

### Release (0.001 - 10.0 seconds)

How fast the sound **fades to silence after the note ends**.

- **Short release** (0.01 - 0.05s): Sound stops almost instantly. Tight and
  controlled, good for staccato bass and rhythmic parts.
- **Medium release** (0.1 - 0.5s): Natural decay, like lifting your foot off a
  piano's sustain pedal.
- **Long release** (0.8 - 2.0s): Sound lingers and fades slowly, creating
  trails and atmosphere. Essential for pads.

### Two Envelopes: Amp vs. Filter

The 0x808 synth has **two separate envelopes**:

1. **Amp Envelope** (`amp_env`): Controls the **volume** of the note over time.
   This determines the basic shape of the sound -- is it a short blip or a long
   sustained note?

2. **Filter Envelope** (`filter_env`): Controls the **filter cutoff** over time.
   This determines the tonal shape -- does the sound start bright and get dark,
   or does it open up?

These two envelopes work together. For example, on a bass sound:
- **Amp envelope:** Fast attack, short decay, zero sustain = note hits hard and
  dies quickly.
- **Filter envelope:** Fast attack, short decay, zero sustain = filter sweeps
  open on the attack, then closes. This creates the "bwow" of the initial
  transient.

### ADSR Examples: Common Sound Shapes

**Piano / Pluck** -- Fast attack, medium decay, low sustain, medium release:
```
  A=0.001  D=0.15  S=0.0  R=0.01

  |  /\
  | /  \
  |/    \
  |______\____
```
The sound hits instantly, decays, and is gone. The "Pluck" preset uses this.

**Pad** -- Slow attack, medium decay, high sustain, long release:
```
  A=0.5  D=0.5  S=0.8  R=1.0

  |      /------\
  |     / |     | \
  |    /  |     |  \
  |   /   |     |   \
  |__/____|_____|____\__
```
The sound fades in gently, holds, and trails off. The "Pad" preset uses this.

**Organ** -- Fast attack, no decay, full sustain, fast release:
```
  A=0.001  D=0.01  S=1.0  R=0.05

  |  /----------\
  | /            |
  |/             |
  |______________|__
```
Instant on, instant off. The "DX Organ" preset uses this.

**Bell** -- Fast attack, long decay, no sustain, long release:
```
  A=0.001  D=2.0  S=0.0  R=1.0

  |/\
  |  \
  |   \
  |    \
  |     \________
```
A bright strike that slowly fades away. The "FM Bell" preset uses this.

---

## 5. LFO (Movement)

**LFO** stands for **Low Frequency Oscillator**. It is just like the oscillators
that make sound, but it runs **much slower** -- too slow to hear as a pitch.
Instead, it wobbles other parameters up and down to add movement.

Think of an LFO like this:
- **Vibrato** on a guitar = LFO modulating pitch
- **Tremolo** on a guitar amp = LFO modulating volume
- **Wah-wah pedal** moving on its own = LFO modulating filter cutoff

### LFO Destinations

The 0x808 LFO can modulate three things:

| Destination        | What it does                          | Sounds like           |
|--------------------|---------------------------------------|-----------------------|
| `LFO_DEST_PITCH`  | Wobbles the pitch up and down         | Vibrato, siren        |
| `LFO_DEST_FILTER` | Wobbles the filter cutoff up and down | Wah-wah, auto-filter  |
| `LFO_DEST_AMP`    | Wobbles the volume up and down        | Tremolo, helicopter    |
| `LFO_DEST_NONE`   | LFO is off                            | No movement            |

### LFO Parameters

**Rate** (0.1 - 50.0 Hz): How fast the wobble happens.
- 0.1 - 0.5 Hz: Very slow, gentle drift (good for pads)
- 1.0 - 4.0 Hz: Noticeable movement (classic vibrato or filter wobble)
- 5.0 - 8.0 Hz: Fast vibrato territory (like a singer's vibrato)
- 10.0+ Hz: Gets into "buzzy" territory, starts to sound like it is
  changing the tone itself

**Depth** (0.0 - 1.0): How extreme the wobble is.
- 0.0: No effect
- 0.05 - 0.1: Subtle movement, adds life without being obvious
- 0.2 - 0.5: Clearly audible modulation
- 0.8 - 1.0: Extreme, dramatic wobble

**Waveform**: The LFO uses the same waveforms as the main oscillators.
- **Sine**: Smooth, even wobble (most common for vibrato/filter)
- **Triangle**: Similar to sine but slightly sharper transitions
- **Square**: Jumps between two values with no smoothing -- creates a trilling
  or stuttering effect
- **Saw**: Rises gradually, drops suddenly -- or the reverse

### BPM Sync

When `lfo_bpm_sync` is enabled, the LFO rate locks to your project tempo instead
of running at a free rate in Hz. The `lfo_sync_division` setting determines the
speed:

| Division | Meaning    | At 120 BPM         |
|----------|------------|---------------------|
| 0        | 1 bar      | 0.5 Hz (very slow)  |
| 1        | 1/2 note   | 1 Hz                |
| 2        | 1/4 note   | 2 Hz (one per beat) |
| 3        | 1/8 note   | 4 Hz                |
| 4        | 1/16 note  | 8 Hz                |
| 5        | 1/32 note  | 16 Hz (fast)        |

BPM sync ensures the wobble always stays in time with your music, even if you
change tempo.

---

## 6. FM Synthesis

FM stands for **Frequency Modulation**. Instead of using a filter to shape
the tone (like subtractive synthesis), FM synthesis uses **one oscillator to
rapidly change the pitch of another**, creating complex timbres.

### The Basic Idea

Imagine you are singing a steady note. Now imagine someone rapidly shaking
your body -- your voice would warble and create overtones that were not there
before. That is FM synthesis.

```
  Subtractive:  Oscillator ---> Filter ---> Output
                "Start rich, remove stuff"

  FM:           Modulator ---> Carrier ---> Output
                "Start simple, add complexity"
```

### Operators: Carriers and Modulators

In the 0x808 FM synth, there are **4 operators** (numbered 0-3). Each operator
is a sine-wave oscillator.

- **Carrier**: An operator whose output you actually **hear**. It produces the
  audible tone.
- **Modulator**: An operator whose output **changes the frequency** of another
  operator. You do not hear it directly -- it shapes the sound.

Each operator has:
- `freq_ratio` (0.5 - 16.0): Its frequency relative to the note you play. A
  ratio of 1.0 means it plays the same note. A ratio of 2.0 means double the
  frequency (one octave up). Non-integer ratios (like 3.5 or 1.41) create
  inharmonic, metallic, bell-like tones.
- `level` (0.0 - 1.0): How loud the operator is (or how much it modulates).
- `feedback` (0.0 - 1.0): Self-feedback, where an operator modulates itself.
  Adds grit and noise. A little goes a long way.
- `env` (ADSR): Each operator has its own envelope, so modulators can fade out
  over time, changing the timbre as a note sustains.

### Algorithms (Routing Patterns)

An **algorithm** defines how the 4 operators are connected. The 0x808 has 8
algorithms:

```
  Algorithm 0: Serial chain
  [4] --> [3] --> [2] --> [1] --> Output
  Maximum complexity. Each operator modulates the next.

  Algorithm 1: Two modulators into one carrier
  [3] --> [2] --> [1] --> Output
                  [4] -----^
  Rich harmonics from two modulation sources.

  Algorithm 2: Two parallel pairs
  [4] --> [3] --> Output
  [2] --> [1] --> Output
  Two independent FM sounds mixed together.

  Algorithm 3: Three-chain plus free carrier
  [4] --> [3] --> [2] --> Output
                  [1] --> Output
  One complex FM voice plus one clean sine tone.

  Algorithm 4: Pair plus two carriers
  [2] --> [1] --> Output
          [3] --> Output
          [4] --> Output
  One FM pair mixed with two clean tones.

  Algorithm 5: Pair plus two carriers (alternate)
  [4] --> [3] --> Output
          [2] --> Output
          [1] --> Output
  Similar to 4 but different routing.

  Algorithm 6: All carriers (additive)
  [4] --> Output
  [3] --> Output
  [2] --> Output
  [1] --> Output
  No FM at all -- just 4 sine waves mixed. This is additive synthesis.
  Good for organs and clean tones.

  Algorithm 7: One modulator to two carriers
  [4] --> [2] --> Output
    \---> [3] --> Output
          [1] --> Output
  One modulator shapes two voices simultaneously.
```

### What FM is Good For

FM excels at sounds that are hard to make with subtractive synthesis:

- **Bells and chimes**: Use inharmonic frequency ratios (like 3.5, 7.0)
- **Electric pianos**: The classic DX7 Rhodes sound (see "FM EPiano" preset)
- **Metallic percussion**: High ratios with short envelopes (see "FM Metal")
- **Glassy pads**: Multiple carriers with slow envelopes (see "FM Pad")
- **Organs**: Algorithm 6 with integer ratios (see "DX Organ")
- **Plucked strings**: Modulators that fade quickly (see "DX Koto")

**Tip:** FM can be unpredictable. Small changes in operator levels can cause big
changes in timbre. Start with a preset you like and make small adjustments.

---

## 7. Wavetable Synthesis

Wavetable synthesis stores a **sequence of different waveforms** (called frames)
in a table. Instead of picking one fixed waveform (like saw or square), you can
**smoothly scan through the table**, morphing between shapes in real time.

### How It Works

```
  Frame 0       Frame 8       Frame 16      Frame 24      Frame 31
  (Saw-ish)     (Squarish)    (Triangle)    (Sine-like)   (Simple)

  /|            ___           /\             .---.          |
  / |           |   |         /  \           /     \        |
  /  |           |   |___     /    \         /       '-     |
  Position: 0.0          0.25          0.5          0.75         1.0

  <--- wt_position scans across --->
```

### Wavetable Banks

The 0x808 includes 4 built-in wavetable banks:

| Bank | Name       | Description                                        |
|------|------------|----------------------------------------------------|
| 0    | Analog     | Morphs from saw to square to triangle to sine       |
| 1    | Harmonics  | Adds more and more overtones across the table        |
| 2    | PWM        | Pulse width from 50% (square) down to 5% (thin)    |
| 3    | Formant    | Vowel-like shapes (A, E, I, O, U resonances)        |

### Wavetable Parameters

- `wt_position` (0.0 - 1.0): Where in the table you are. At 0.0 you hear the
  first waveform; at 1.0 you hear the last. Values in between smoothly blend
  adjacent frames.

- `wt_env_depth` (-1.0 to 1.0): How much the **filter envelope** sweeps the
  position. Positive values sweep forward through the table as a note plays;
  negative values sweep backward. This creates evolving timbres that change
  over the life of each note.

- `wt_lfo_depth` (0.0 - 1.0): How much the **LFO** wobbles the position back
  and forth. Creates continuously shifting textures.

### What Wavetable is Good For

- **Evolving textures**: Sounds that change character over time
- **Modern bass**: Morphing waveforms create complex harmonics
- **PWM strings**: Classic pulse-width-modulated string sounds
- **Vocal-like sounds**: The Formant bank creates vowel sweeps

---

## 8. Quick Recipes

Step-by-step settings for common sounds using the 0x808 synth parameters.

### Simple Bass

A solid, punchy bass for electronic music.

| Parameter        | Value             | Why                                   |
|------------------|-------------------|---------------------------------------|
| Synth Mode       | Subtractive       |                                       |
| Osc1 Wave        | Saw               | Rich harmonics for the filter to shape|
| Osc Mix          | 0.0               | Single oscillator keeps it focused    |
| Filter Type      | Low Pass          | Remove brightness for warmth          |
| Filter Cutoff    | 800 Hz            | Dark but not muffled                  |
| Filter Resonance | 2.0               | Slight emphasis at cutoff             |
| Filter Env Depth | 2000              | Cutoff sweeps up on attack            |
| Amp Envelope     | A=0.005 D=0.3 S=0.0 R=0.05 | Fast hit, dies away           |
| Filter Envelope  | A=0.005 D=0.2 S=0.0 R=0.05 | Brief brightness on attack   |

This matches the built-in **"Bass"** preset.

### Warm Pad

A lush, atmospheric background sound.

| Parameter        | Value             | Why                                   |
|------------------|-------------------|---------------------------------------|
| Synth Mode       | Subtractive       |                                       |
| Osc1 Wave        | Triangle          | Soft starting point                   |
| Osc2 Wave        | Sine              | Adds gentle body                      |
| Osc Mix          | 0.5               | Equal blend of both                   |
| Osc2 Detune      | 0.05 semitones    | Very slight detuning for width        |
| Unison Voices    | 3-7               | Stacking for thickness                |
| Unison Detune    | 12-16 cents       | Chorus-like spread                    |
| Filter Type      | Low Pass          |                                       |
| Filter Cutoff    | 2000-3000 Hz      | Warm but not dark                     |
| Filter Resonance | 1.0-1.5           | Low, smooth                           |
| Amp Envelope     | A=0.5 D=0.5 S=0.8 R=1.0  | Slow fade in, holds, trails off|
| Filter Envelope  | A=0.3 D=0.5 S=0.5 R=0.8  | Gentle brightness swell       |
| LFO Dest         | Filter            | Slow filter movement                  |
| LFO Rate         | 0.2-0.3 Hz        | Very slow drift                       |
| LFO Depth        | 0.15-0.2          | Subtle                                |

See the **"Pad"**, **"Juno Pad"**, or **"JP-8 Pad"** presets.

### Cutting Lead

A bright, forward synth lead that sits on top of a mix.

| Parameter        | Value             | Why                                   |
|------------------|-------------------|---------------------------------------|
| Synth Mode       | Subtractive       |                                       |
| Osc1 Wave        | Square            | Hollow body                           |
| Osc2 Wave        | Saw               | Brightness                            |
| Osc Mix          | 0.4-0.5           | Blend both                            |
| Osc2 Detune      | 0.1 semitones     | Slight thickening                     |
| Filter Type      | Low Pass          |                                       |
| Filter Cutoff    | 1500-2000 Hz      |                                       |
| Filter Resonance | 3.0-5.0           | Prominent, present                    |
| Filter Env Depth | 4000-5000         | Big filter sweep on attack            |
| Amp Envelope     | A=0.01 D=0.1 S=0.7 R=0.3  | Quick attack, sustains       |
| Filter Envelope  | A=0.01 D=0.3 S=0.3 R=0.2  | Opens bright, settles down   |
| LFO Dest         | Pitch             | Vibrato                               |
| LFO Rate         | 5.0-6.0 Hz        | Natural vibrato speed                 |
| LFO Depth        | 0.05-0.1          | Subtle pitch wobble                   |

See the **"Lead"** or **"ARP Lead"** presets.

### Pluck / Key Hit

A short, percussive synth note for arpeggios or stabs.

| Parameter        | Value             | Why                                   |
|------------------|-------------------|---------------------------------------|
| Synth Mode       | Subtractive       |                                       |
| Osc1 Wave        | Saw               | Bright attack                         |
| Osc2 Wave        | Square            | Body                                  |
| Osc Mix          | 0.2               | Mostly saw                            |
| Filter Type      | Low Pass          |                                       |
| Filter Cutoff    | 1000 Hz           | Starting dark                         |
| Filter Resonance | 4.0               | Squelchy pluck character               |
| Filter Env Depth | 6000              | Big sweep for "ping"                  |
| Amp Envelope     | A=0.001 D=0.15 S=0.0 R=0.01 | Instant hit, dies fast       |
| Filter Envelope  | A=0.001 D=0.1 S=0.0 R=0.01  | Brief brightness spike       |
| LFO              | Off               | No movement needed                    |

See the **"Pluck"** or **"Stab"** presets.

### FM Bell

A clear, ringing bell using FM synthesis.

| Parameter        | Value             | Why                                   |
|------------------|-------------------|---------------------------------------|
| Synth Mode       | FM                |                                       |
| Algorithm        | 0 (serial chain)  | Maximum harmonic complexity           |
| Op 0 Ratio       | 1.0               | Fundamental tone (carrier)            |
| Op 1 Ratio       | 3.5               | Inharmonic = bell character           |
| Op 2 Ratio       | 7.0               | Upper shimmer                         |
| Op 3 Ratio       | 11.0              | High sparkle                          |
| Op Levels         | 1.0, 0.8, 0.4, 0.2 | Modulators decrease in level        |
| Op Envelopes     | All have long decay (1-3s), no sustain | Ring and fade        |
| Amp Envelope     | A=0.001 D=2.0 S=0.0 R=1.0  | Instant strike, long ring    |

The key to bell sounds is **inharmonic ratios** (non-integer numbers like 3.5).
Integer ratios (1.0, 2.0, 3.0) sound musical and tonal. Non-integer ratios
sound metallic and bell-like.

See the **"FM Bell"** preset.

### Wobble Bass (Dubstep-style)

A bass that rhythmically opens and closes.

| Parameter        | Value             | Why                                   |
|------------------|-------------------|---------------------------------------|
| Synth Mode       | Subtractive       |                                       |
| Osc1 Wave        | Saw               | Rich harmonics to filter              |
| Osc2 Wave        | Saw               |                                       |
| Osc Mix          | 0.5               | Full blend                            |
| Osc2 Detune      | 0.1               | Slight thickness                      |
| Unison Voices    | 3                 | Width                                 |
| Unison Detune    | 10 cents          | Moderate spread                       |
| Filter Type      | Low Pass          |                                       |
| Filter Cutoff    | 600 Hz            | Starts dark                           |
| Filter Resonance | 3.0               | Noticeable squelch on the wobble      |
| Filter Env Depth | 2000              | Moderate sweep                        |
| Amp Envelope     | A=0.01 D=0.5 S=0.6 R=0.3   | Sustains while held          |
| Filter Envelope  | A=0.01 D=0.8 S=0.2 R=0.3   | Slow filter movement         |
| LFO Dest         | Filter            | The wobble                            |
| LFO Rate         | 0.3 Hz (or use BPM sync) | Slow rhythmic movement        |
| LFO Depth        | 0.15              | Audible but not extreme               |

For a wavetable version, see the **"WT Wobble"** preset which uses the Analog
bank with LFO scanning the wavetable position.

See the **"Reese Bass"** preset for the subtractive version.

---

## 9. Preset Reference

The 0x808 ships with 50 built-in presets across all three synthesis modes.

### Subtractive Presets

| #  | Name           | Character                                            |
|----|----------------|------------------------------------------------------|
| 0  | Bass           | Simple punchy saw bass                               |
| 1  | Lead           | Square + saw lead with vibrato                       |
| 2  | Pad            | Gentle triangle/sine pad with unison                 |
| 3  | Pluck          | Short percussive pluck                               |
| 4  | SuperSaw       | Wide 5-voice unison saw, EDM supersaw                |
| 14 | Moog Bass      | Thick Minimoog-style bass, saw + square octave down  |
| 15 | 303 Acid       | TB-303 acid squelch, high resonance                  |
| 16 | Juno Pad       | Roland Juno warm chorus pad, 5-voice unison          |
| 17 | OB Strings     | Oberheim OB-X string ensemble, 7-voice unison        |
| 18 | Prophet Brass  | Sequential Circuits brass stab                       |
| 19 | ARP Lead       | ARP 2600 piercing lead, square + saw fifth           |
| 20 | SH Noise       | SH-101 noise percussion, heavy detune + high pass    |
| 21 | Reese Bass     | DnB-style detuned reese bass with filter LFO         |
| 24 | Hoover         | Classic rave hoover, wide unison with pitch LFO      |
| 25 | Stab           | House/techno chord stab, band pass filter             |
| 26 | CS-80 Brass    | Yamaha CS-80 Blade Runner brass                      |
| 27 | MS-20 Growl    | Korg MS-20 aggressive, near-self-oscillation filter  |
| 28 | Poly-6 Keys    | Korg Polysix classic poly keys                       |
| 29 | JP-8 Pad       | Roland Jupiter-8 lush evolving pad, 7-voice unison   |
| 30 | Sub 37         | Moog Sub 37 mono lead with sub oscillator            |
| 40 | Sync Lead      | Hard sync-style aggressive lead                      |
| 41 | Whistle        | Pure sine lead with vibrato                          |
| 42 | Tape Strings   | Mellotron-like slow strings, 5-voice unison          |

### FM Presets

| #  | Name           | Character                                            |
|----|----------------|------------------------------------------------------|
| 5  | FM Bell        | Classic DX7 bell, inharmonic ratios                  |
| 6  | FM EPiano      | Rhodes-like electric piano                           |
| 7  | FM Metal       | Metallic percussion hit                              |
| 8  | FM Bass        | Deep FM bass                                         |
| 9  | FM Pad         | Evolving additive pad (algorithm 6)                  |
| 22 | DX Piano       | Brighter FM electric piano                           |
| 23 | DX Vibes       | FM vibraphone                                        |
| 31 | DX Organ       | Hammond-style additive organ                         |
| 32 | DX Marimba     | FM marimba / xylophone                               |
| 33 | DX Clav        | FM clavinet                                          |
| 34 | DX Strings     | FM string ensemble                                   |
| 35 | DX Koto        | FM plucked string                                    |
| 36 | DX Flute       | FM breathy flute                                     |
| 37 | DX Harm        | FM harmonica / accordion                             |

### Wavetable Presets

| #  | Name           | Character                                            |
|----|----------------|------------------------------------------------------|
| 10 | WT Sweep       | Analog bank with envelope position sweep             |
| 11 | WT Harmonic    | Harmonics bank with LFO, pad-like                    |
| 12 | WT PWM         | Pulse width modulation via LFO                       |
| 13 | WT Vocal       | Formant bank, vowel-like sweep                       |
| 38 | WT Glass       | Harmonics bank, glassy bell-like                     |
| 39 | WT Wobble      | Analog bank with heavy LFO wobble                    |

---

## Glossary

| Term           | Plain English                                              |
|----------------|------------------------------------------------------------|
| Oscillator     | The part that generates the raw tone                       |
| Waveform       | The shape of the repeating wave (saw, square, etc.)        |
| Filter         | Removes certain frequencies to shape the tone              |
| Cutoff         | The frequency where the filter starts cutting              |
| Resonance (Q)  | Boost at the cutoff frequency -- makes it "squelchy"       |
| Envelope       | A shape that changes a value over time (volume, filter)    |
| ADSR           | Attack, Decay, Sustain, Release -- the four envelope stages|
| LFO            | A slow oscillator that wobbles other parameters            |
| Unison         | Stacking multiple detuned copies of a sound                |
| Detune         | Shifting pitch slightly to create thickness                |
| Carrier        | An FM operator you hear directly                           |
| Modulator      | An FM operator that shapes another operator's sound        |
| Algorithm      | The wiring diagram between FM operators                    |
| Wavetable      | A sequence of waveforms you can scan through               |
| Harmonics      | Overtones above the fundamental frequency                  |
| Semitone       | One piano key up or down (1/12 of an octave)               |
| Cent           | 1/100th of a semitone -- used for fine detuning            |
| BPM Sync       | Locking LFO speed to the project tempo                     |
