## Why

Musicians and producers lack a free, lightweight, cross-platform tool that combines drum sequencing, melodic piano roll composition, multi-type synthesis, and sample management in a single focused package — one that also works as a DAW plugin.

Existing open-source options fall into two extremes:
- **Drum-only tools** (Hydrogen) — great for beats but no melodic composition or synthesis
- **Full DAWs** (LMMS, Ardour) — overwhelming complexity when you just want to make beats and sounds
- **Commercial tools** (FL Studio, Ableton) — expensive and platform-locked

There's a gap for a focused instrument that lets you: load samples, sequence drums, write melodies, twist knobs to shape sounds, arrange full songs, and export audio — all in one lightweight C application that runs on Linux, Windows, and macOS, both standalone and inside DAWs like Reaper.

This also serves as a practical C learning project, building real-world audio software incrementally.

## Business Value Analysis

### Who Benefits and How

**1. The Bedroom Producer on Linux (primary persona)**
A musician who uses Linux as their daily driver and is frustrated that most music production tools are Windows/macOS-only or require paid licenses. They want to sketch beats, layer synth lines, and export tracks without dual-booting or paying for FL Studio. Sequencer_C gives them a native, free tool that works out of the box with bundled samples — no setup friction, no license keys, no Wine compatibility hacks.

**2. The Budget-Conscious Beatmaker (cross-platform)**
A hobbyist or student making beats who can't afford Ableton ($250-750) or FL Studio ($100-500). They've tried LMMS but found it overwhelming for what they actually need: a drum grid, some synth sounds, and a way to arrange and export a track. Sequencer_C is the "just enough" tool — focused scope, zero cost, immediate productivity.

**3. The DAW Power User Who Wants a Better Instrument Plugin**
A producer already working in Reaper (or another DAW) who wants a drum/synth instrument plugin that combines sample sequencing with built-in synthesis — without buying Battery ($400), Maschine ($600), or piecing together separate plugins for drums, bass, and leads. The VST3/CLAP plugin version drops into their existing workflow.

**4. The Developer Learning Audio Programming**
A programmer (like the project author) who wants to learn C through a real, meaningful project — not toy exercises. Building an actual music tool teaches memory management, real-time audio constraints, GUI programming, and cross-platform development in a deeply satisfying way: you hear your bugs and your fixes.

### What Problem This Solves

The core problem: **the entry point to making music with software is either too expensive, too complex, or too fragmented.**

- Free tools force you to choose between drums OR synths OR effects — never all three in one place
- Full DAWs present 200+ buttons when you need 20
- Commercial instruments cost $100-600 each, and you need several to cover drums + bass + leads
- Linux users are perpetually underserved by music software

Sequencer_C solves this by being a single, focused instrument that does drums + synth + effects + arrangement in one window, at zero cost, on any OS.

### Priority Ranking by Value Delivered

Capabilities ranked by how much user value they unlock, not by technical interest:

| Priority | Capability | Value Rationale |
|----------|-----------|-----------------|
| **P0** | `step-sequencer` + `drum-grid-gui` + `sample-engine` | The minimum useful product. Without these, nothing works. A clickable drum grid with sample playback is the atomic unit of value. |
| **P0** | `knob-controls` | Knobs make the difference between "tool" and "instrument." Real-time parameter tweaking is what makes music software feel alive. |
| **P1** | `subtractive-synth` | Unlocks bass lines, leads, and synth percussion — transforms the tool from a sample player into a sound design instrument. |
| **P1** | `audio-export` | No export = no finished songs. This is what turns a toy into a production tool. |
| **P1** | `sample-browser` + bundled samples | Out-of-box experience. If users have to hunt for samples before making sound, most will quit before starting. |
| **P2** | `song-arrangement` | Moves from "loop maker" to "song maker." Critical for anyone who wants to produce complete tracks. |
| **P2** | `piano-roll-gui` | Required for melodic content. Without it, synth tracks are limited to step-grid note entry. |
| **P2** | `effects-chain` | Reverb/delay/filter are the difference between dry, lifeless output and something that sounds like music. |
| **P3** | `fm-synth` + `wavetable-synth` | Expands the sonic palette significantly, but the subtractive synth already covers most needs. |
| **P3** | `daw-plugin` | High value for DAW users, but the standalone app serves most users first. |

### What Happens If We Don't Build This

- **Linux musicians** continue choosing between Hydrogen (drums only, no synth, aging Qt4 UI) and LMMS (full DAW complexity they don't need). Many give up and dual-boot Windows.
- **Budget producers** remain locked into free tiers of commercial tools with crippled export, or pirate software they can't afford.
- **The "focused instrument" gap** stays unfilled. Nobody ships the thing that sits between "simple drum machine" and "full DAW" — the sweet spot where most beat-making actually happens.
- **The developer** misses a high-quality learning vehicle for C, real-time systems, and audio programming — skills with strong transferability to embedded systems, game audio, and DSP engineering.

Nothing catastrophic happens. People keep muddling through with existing tools. But the opportunity cost is real: a lightweight, free, cross-platform instrument would serve a community that's been chronically underserved.

### Success Metrics

| Metric | Target | How to Measure |
|--------|--------|----------------|
| **Time to first sound** | < 30 seconds after launch | User opens app → hears a drum pattern playing with bundled samples. No file hunting, no configuration. |
| **Time to first custom beat** | < 5 minutes | User can click a grid, assign samples, and have a unique pattern looping. Measured via user testing. |
| **Export completion rate** | > 80% of sessions produce an exported file | If people finish tracks and export them, the tool is delivering value. Track via opt-in telemetry or user surveys. |
| **Cross-platform build success** | 100% on Linux, Windows, macOS | CI passes on all three platforms on every commit. |
| **Audio quality** | No audible glitches at 256-sample buffer | Measure via automated test: render 60 seconds, check for discontinuities in the output waveform. |
| **Plugin loads in Reaper** | VST3 and CLAP scan and load without errors | Automated test: Reaper CLI plugin scan returns success. |
| **Community adoption signals** | GitHub stars, forks, issues filed | If people file feature requests, they care enough to want more — that's success. |
| **Knob satisfaction** | Qualitative: "this feels good to use" | User testing feedback on real-time parameter control. The knob experience is a core differentiator — it must feel responsive and musical. |

## What Changes

This is a greenfield project — building a complete application from scratch. The system consists of:

1. **A pure C DSP engine** (Layer 1) — sequencer, sampler, synthesizer, mixer, and effects processing that operates on float audio buffers with zero platform dependencies
2. **A Nuklear-based GUI** (Layer 2) — drum grid, piano roll, knob controls, arrangement view, sample browser
3. **Host wrappers** (Layer 3) — standalone app via miniaudio, and DAW plugin via CPLUG (VST3/CLAP)

The architecture cleanly separates audio processing from GUI and platform code, allowing the same engine to power both the standalone app and DAW plugins.

**Technology stack**: C99 with single-header libraries (miniaudio, dr_libs, Nuklear, TinySoundFont, CPLUG) — no massive frameworks, minimal dependencies, public domain or MIT licensed.

## Capabilities

### New Capabilities
- `sample-engine`: Load WAV/MP3/FLAC samples, play back with 16-voice polyphony, pitch-shift via resampling with Hermite interpolation
- `step-sequencer`: Pattern-based drum sequencing with configurable step count (4-64), per-step velocity and pitch offset, BPM-synced transport
- `drum-grid-gui`: Visual step grid (tracks × steps) for percussion programming — click to toggle, right-click for velocity/pitch, playback position highlighting
- `piano-roll-gui`: Melodic note editor (pitch × time) with variable-length notes, velocity coloring — for synth and pitched sample tracks
- `subtractive-synth`: Wavetable-based oscillators (saw/square/tri/sine), biquad filter (LP/HP/BP), ADSR envelopes, LFO modulation, unison detuning
- `fm-synth`: 4-operator FM synthesis with configurable algorithms, per-operator envelopes and frequency ratios
- `wavetable-synth`: Wavetable scanning synthesis with morphing between waveforms, modulatable table position
- `effects-chain`: Per-track and master bus effects — biquad filter, delay, reverb (Freeverb algorithm)
- `song-arrangement`: Section-based song structure with song mode (linear playback) and perform mode (live section triggering queued to bar boundaries)
- `audio-export`: Offline render to WAV and MP3, real-time recording of master output
- `sample-browser`: File browser with waveform preview, SF2 soundfont support via TinySoundFont
- `knob-controls`: Custom rotary knob widgets for real-time parameter tweaking — satisfying tactile sound design
- `daw-plugin`: VST3 and CLAP plugin formats via CPLUG, with parameter automation and host transport sync

### Modified Capabilities
<!-- N/A — greenfield project -->

## Impact

- **New codebase**: ~30+ source files across engine, GUI, and host wrapper layers
- **Dependencies** (all vendored, single-header): miniaudio, dr_wav, dr_mp3, dr_flac, TinySoundFont, Nuklear, CPLUG, cJSON
- **Build system**: CMake with targets for standalone executable, VST3/CLAP plugins, and tests
- **Platform dependencies**: SDL2 (windowing/input), OpenGL (rendering) — both widely available via package managers
- **Bundled assets**: ~50-100 CC0/public domain drum samples, basic wavetable data
- **Target platforms**: Linux (gcc/ALSA/PulseAudio), Windows (MSVC/WASAPI), macOS (clang/CoreAudio)
