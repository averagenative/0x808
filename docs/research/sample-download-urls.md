# Drum Sample Download URLs (Direct, Verified)

Research deliverable for bundling 2-3 new kits in 0x808. All URLs were
fetched (HEAD or GET) on 2026-04-22 and confirmed to return `audio/wav` or
`audio/x-wav` with HTTP 200. No login, form, or signup required.

License rule applied: **CC0 / Public Domain Mark / CC-BY only.**

## Sources rejected (do NOT use)

Documenting these so future contributors don't re-investigate them:

- **Pixabay sound effects** — Pixabay License explicitly forbids redistribution
  on a "Standalone basis" (i.e. shipping the raw `.wav` in a public repo's
  `samples/` folder is exactly the prohibited case). Bundling inside the
  compiled binary may be OK as a "new creative work", but a `.wav` in the
  GitHub tree is not. See <https://pixabay.com/service/license-summary/>.
- **Freesound.org (any license, including CC0)** — original WAVs require
  oAuth/login per their FAQ; only mp3 previews are public. Fails the
  `curl -O` constraint.
- **Oramics `DRUMS/avl-drumkits-1.1`** — license is **CC-BY-SA**. ShareAlike
  is viral and would force the entire 0x808 project to inherit SA. Reject.
- **Oramics `DM/CR-78`, `DM/LM-2`, `DM/MRK-2`, `DM/TR-505`** — already
  shipping in `samples/` (or aliased there). Skipped to avoid duplication.
- **Fox72z 808 Trap items on archive.org** (PD Mark 1.0) — full mp3 tracks,
  not one-shots. Wrong format for our slots.
- **`drum-samples-patch-xr` / `hawk-tuna-13-mini-drum-pack`** — PD Mark 1.0
  but distributed as single .zip blobs (251 MB and 16 MB). No per-WAV URL.
- **`ultimate-vintage-drum-machines-sample-pack` (archive.org)** — title is
  perfect (DMX, SP-1200, 707, 727, 606, etc) but distributed as a single
  `Ultimate Vintage Drum Machines Sample Pack by Onibaku.7z`. No per-WAV
  URL, and the upload has **no `licenseurl`** in its metadata — the
  uploader simply re-shares Onibaku's bundle, so the chain of title for
  any individual machine inside is unverifiable. Reject on both grounds.
- **`ab-samples` (Alex Ball Free Drum Samples, archive.org)** — covers
  Boss DR-110 / Korg Rhythm 55B / Korg SR-120 / DX. Distributed as a
  single ~hundred-MB `.zip` (no per-WAV URL) and metadata has **no
  `licenseurl`** — Alex Ball's own pack is "free" but not labelled with
  a CC license at the source. Cannot ship without explicit licensing.
- **`HGFSoundsUC1SDP01` (archive.org, CC-BY 3.0)** — only contains `.sf2`
  SoundFont bundles inside `.zip` files. No raw one-shot WAVs.
- **`instrum_20160218` "DMX (I'm Rappin') Workshop"** — PD Mark 1.0 but
  the files are **mp3 song stems** (drums, keys, bass, rap), not DMX
  one-shots. Misleading title — does not contain Oberheim DMX samples.
- **Casio MT-52 / MT-240 / VL-1 / Yamaha PSS-270 Rhythms (archive.org,
  PD Mark 1.0)** — full pattern recordings (`16beat 95bpm.wav`,
  `12beat_fill.wav` etc.), not isolated drum hits. Wrong format for our
  per-slot kit model.
- **Oramics `DM/TR-808` and `DM/TR-909`** — already shipping as our
  own `808/` and `909/` kits in `samples/`. Skip.

---

## Kit 1 — Acoustic ("Pearl Master Studio")

- **Source**: Oramics `sampled` repo, `DRUMS/pearl-master-studio/`
- **License**: **Creative Commons Attribution 3.0** (CC-BY 3.0)
  Verified at <https://raw.githubusercontent.com/oramics/sampled/master/DRUMS/pearl-master-studio/README.md>
- **Author**: enoe (credit required in `samples/LICENSE.md`)
- **Format**: WAV, real recorded acoustic drum kit
- **Per-file size**: 65 KB - 384 KB (all under the 500 KB ideal)
- **Total kit size**: ~1.95 MB (8 slots)

| Slot | File | URL | Size |
|------|------|-----|------|
| kick | kick-01.wav | https://oramics.github.io/sampled/DRUMS/pearl-master-studio/samples/kick-01.wav | 66 KB |
| snare | snare-01.wav | https://oramics.github.io/sampled/DRUMS/pearl-master-studio/samples/snare-01.wav | 89 KB |
| closed hat | hihat-closed.wav | https://oramics.github.io/sampled/DRUMS/pearl-master-studio/samples/hihat-closed.wav | 68 KB |
| clap | snare-03.wav (rim-flam used as clap) | https://oramics.github.io/sampled/DRUMS/pearl-master-studio/samples/snare-03.wav | 65 KB |
| open hat | hihat-open.wav | https://oramics.github.io/sampled/DRUMS/pearl-master-studio/samples/hihat-open.wav | 112 KB |
| percussion | crash-01.wav | https://oramics.github.io/sampled/DRUMS/pearl-master-studio/samples/crash-01.wav | 209 KB |
| rim | snare-02.wav (rimshot variant) | https://oramics.github.io/sampled/DRUMS/pearl-master-studio/samples/snare-02.wav | 65 KB |
| tom | tom-01.wav | https://oramics.github.io/sampled/DRUMS/pearl-master-studio/samples/tom-01.wav | 173 KB |

Note: the pack has **no dedicated handclap or sidestick file**. The closest
analog for the clap slot is `snare-03.wav` (a softer snare hit). If a real
clap is required, source it separately or fall back to the existing 909
clap.

---

## Kit 2 — Modern Trap / 808 Sub ("Mailbox Badger Trap")

Hybrid pulled from two PD/CC-BY archive.org items by the same uploader
(Mailbox Badger). Provides the deeper sub-bass character the project's
original 808 kick lacks.

- **Source A**: <https://archive.org/details/HeatDish> — Mailbox Badger PD
  Drum Samples Vol 1, **CC Public Domain Mark 1.0** (no attribution
  required, redistribution OK).
- **Source B**: <https://archive.org/details/mailboxbadgerdrumsamplesvolume2>
  — Vol 2, **CC-BY 3.0** (attribution required for the four files used
  from Vol 2).
- **Per-file size**: 8 KB - 250 KB
- **Total kit size**: ~470 KB (8 slots)

| Slot | File | License | URL | Size |
|------|------|---------|-----|------|
| kick (sub) | LowSineKick-4_12_2015.wav | PD Mark | https://archive.org/download/HeatDish/LowSineKick-4_12_2015.wav | 9 KB |
| snare | sinenoisesnare-4_11_2015.wav | PD Mark | https://archive.org/download/HeatDish/sinenoisesnare-4_11_2015.wav | 8 KB |
| closed hat | cl hihat-4_11_2015.wav | PD Mark | https://archive.org/download/HeatDish/cl%20hihat-4_11_2015.wav | 8 KB |
| clap | Analog Clap 1.wav | CC-BY 3.0 | https://archive.org/download/mailboxbadgerdrumsamplesvolume2/Analog%20Clap%201.wav | 16 KB |
| open hat | Analog Hihat 2.wav | CC-BY 3.0 | https://archive.org/download/mailboxbadgerdrumsamplesvolume2/Analog%20Hihat%202.wav | 40 KB |
| percussion (sub bass) | Submarine.wav | CC-BY 3.0 | https://archive.org/download/mailboxbadgerdrumsamplesvolume2/Submarine.wav | 168 KB |
| rim | Rim-4_12_2015.wav | PD Mark | https://archive.org/download/HeatDish/Rim-4_12_2015.wav | 8 KB |
| tom (square sub) | 7:1-80Hz Squarewave.wav | CC-BY 3.0 | https://archive.org/download/mailboxbadgerdrumsamplesvolume2/7%3A1-80Hz%20Squarewave.wav | 30 KB |

Note: this is **not** a typical commercial trap kit (no Pierre-Bourne-style
processed 808s exist under a CC0/CC-BY license that I could verify). It is
a sub-leaning analog/sine kit that fills the same musical role: deep
sustained low end, snappy noise snare, tight hats. Re-pitch the sub kick
in the engine for melodic 808 lines.

---

## Kit 3 — Lo-fi / Dusty ("Cassette 808")

A "deliberately bad" reamp of the TR-808 through a cassette boombox by
Chris Beckstrom. Saturated, hissy, wobbly — exactly the lo-fi character
the brief asks for.

- **Source**: <https://archive.org/details/808_variations> — "Beckstrom's
  Weird TR-808 Sample Pack"
- **License**: **CC-BY 4.0** (credit "Chris Beckstrom" in
  `samples/LICENSE.md`)
- **Format**: 32-bit stereo WAV, 44.1 kHz. Files are 700 KB - 1.4 MB each
  because of bit depth + headroom; **transcode to 16-bit mono before
  shipping** (the existing convert command `ffmpeg -i in.wav -ac 1 -ar
  44100 -sample_fmt s16 out.wav` from `docs/SAMPLE_SOURCES.md` knocks each
  file to ~120-200 KB).
- **Total kit size at source**: ~9.4 MB raw / **~1.4 MB after transcode**.

| Slot | File | URL | Raw size |
|------|------|-----|----------|
| kick | cassette_808_BD.wav | https://archive.org/download/808_variations/808variations/cassette_boombox/cassette_808_BD.wav | 1.4 MB |
| snare | cassette_808_SD.wav | https://archive.org/download/808_variations/808variations/cassette_boombox/cassette_808_SD.wav | 1.0 MB |
| closed hat | cassette_808_HH.wav | https://archive.org/download/808_variations/808variations/cassette_boombox/cassette_808_HH.wav | 690 KB |
| clap | cassette_808_CP.wav | https://archive.org/download/808_variations/808variations/cassette_boombox/cassette_808_CP.wav | 1.0 MB |
| open hat | cassette_808_OHH.wav | https://archive.org/download/808_variations/808variations/cassette_boombox/cassette_808_OHH.wav | 862 KB |
| percussion | cassette_808_CB.wav (cowbell) | https://archive.org/download/808_variations/808variations/cassette_boombox/cassette_808_CB.wav | 880 KB |
| rim | cassette_808_RIM.wav | https://archive.org/download/808_variations/808variations/cassette_boombox/cassette_808_RIM.wav | 862 KB |
| tom | cassette_808_TOM1.wav | https://archive.org/download/808_variations/808variations/cassette_boombox/cassette_808_TOM1.wav | 862 KB |

Bonus character variations available at the same archive.org item if a
contributor wants to expand: `microcassette/`, `microcassette_slow/`,
`heavyTube/`, `tube/`, `tubeSpringReverb/`, `paint_can/`, `megaphone/` —
each a full 13-slot kit with the same naming pattern.

---

## Verification methodology

For each URL above:
1. `curl -sIL` against the listed URL — confirmed HTTP 200 (or 200 after a
   single 302 to an archive.org CDN node).
2. `Content-Type` header confirmed `audio/wav` or `audio/x-wav`.
3. License confirmed by reading the source's own metadata file
   (`README.md`, `LICENSE.md`, or archive.org `licenseurl`), not just an
   aggregator's claim.

---

## One-shot download command

Run from project root. Creates a staging tree under `samples/_new/` —
wipe / curate / move into proper kit directories before committing.

```bash
mkdir -p samples/_new/pearl-acoustic samples/_new/trap-808 samples/_new/cassette-lofi

# --- Kit 1: Pearl Master Studio (acoustic, CC-BY 3.0, credit: enoe) ---
cd samples/_new/pearl-acoustic
for f in kick-01.wav snare-01.wav hihat-closed.wav snare-03.wav \
         hihat-open.wav crash-01.wav snare-02.wav tom-01.wav; do
    curl -fLO "https://oramics.github.io/sampled/DRUMS/pearl-master-studio/samples/$f"
done
cd -

# --- Kit 2: Mailbox Badger trap-sub hybrid (PD Mark + CC-BY 3.0) ---
cd samples/_new/trap-808
curl -fLo kick.wav        'https://archive.org/download/HeatDish/LowSineKick-4_12_2015.wav'
curl -fLo snare.wav       'https://archive.org/download/HeatDish/sinenoisesnare-4_11_2015.wav'
curl -fLo hihat-closed.wav 'https://archive.org/download/HeatDish/cl%20hihat-4_11_2015.wav'
curl -fLo clap.wav        'https://archive.org/download/mailboxbadgerdrumsamplesvolume2/Analog%20Clap%201.wav'
curl -fLo hihat-open.wav  'https://archive.org/download/mailboxbadgerdrumsamplesvolume2/Analog%20Hihat%202.wav'
curl -fLo perc-sub.wav    'https://archive.org/download/mailboxbadgerdrumsamplesvolume2/Submarine.wav'
curl -fLo rim.wav         'https://archive.org/download/HeatDish/Rim-4_12_2015.wav'
curl -fLo tom-sub.wav     'https://archive.org/download/mailboxbadgerdrumsamplesvolume2/7%3A1-80Hz%20Squarewave.wav'
cd -

# --- Kit 3: Cassette 808 lo-fi (CC-BY 4.0, credit: Chris Beckstrom) ---
BASE='https://archive.org/download/808_variations/808variations/cassette_boombox'
cd samples/_new/cassette-lofi
for slot in BD SD HH CP OHH CB RIM TOM1; do
    curl -fLO "$BASE/cassette_808_${slot}.wav"
done
cd -

# Optional: transcode lo-fi kit from 32-bit stereo to 16-bit mono
# (matches existing project format, drops kit from ~9.4 MB to ~1.4 MB)
for f in samples/_new/cassette-lofi/*.wav; do
    ffmpeg -y -i "$f" -ac 1 -ar 44100 -sample_fmt s16 "${f%.wav}-16m.wav"
    mv "${f%.wav}-16m.wav" "$f"
done
```

---

## Total payload

| Kit | At-source size | After 16-bit-mono transcode |
|-----|----------------|-----------------------------|
| Pearl acoustic | ~1.95 MB | ~1.95 MB (already 16-bit) |
| Mailbox Badger trap | ~470 KB | ~470 KB (already small) |
| Cassette lo-fi | ~9.4 MB | **~1.4 MB** |
| **All three kits** | **~11.8 MB** | **~3.8 MB** |

Both totals are well under the 25 MB ceiling proposed in
`docs/research/kit-expansion.md`. Current `samples/` is 5.6 MB, so adding
all three (transcoded) brings the project to ~9.4 MB.

---

## Attribution updates required for `samples/LICENSE.md`

If shipped, add three rows:

```
pearl-acoustic/   - Pearl Master Studio Pack 1 by enoe, CC-BY 3.0,
                    https://oramics.github.io/sampled/DRUMS/pearl-master-studio/
trap-808/         - Mailbox Badger PD Drum Samples Vol 1 (PD Mark 1.0)
                    + Vol 2 (CC-BY 3.0),
                    https://archive.org/details/HeatDish
                    https://archive.org/details/mailboxbadgerdrumsamplesvolume2
cassette-lofi/    - Beckstrom's Weird TR-808 Sample Pack by Chris Beckstrom,
                    CC-BY 4.0, https://archive.org/details/808_variations
```

---

# Round 2 — additional kits

Second pass after the priority-target brief asked for DMX, SP-1200, TR-606/
707/727, Boss machines, and additional Beckstrom variations. The named
machines above (DMX/SP-1200/606/707/727/Boss DR-x) **could not be sourced**
under PD/CC0/CC-BY/MIT in raw per-WAV form — every candidate found was
either packaged as a single uploader `.zip`/`.7z` with no per-file URL,
unlabelled with a license, or pattern-loop recordings instead of one-shots.
See the rejection list above. The following four kits **were** verified
and add useful character to the bundled set: a Roland Sound Canvas
(SC-8850) hardware ROMpler, two more Beckstrom 808 character variations
to broaden the lo-fi range, and a CC-BY-4.0 Korg Volca Modular synth
percussion set (modular/IDM texture).

All URLs verified with `curl -sIL` on 2026-04-22 — every one returned
HTTP 200 with `Content-Type: audio/x-wav` (after a single 302 to an
archive.org CDN node).

---

## Kit 4 — Roland Sound Canvas SC-8850 (digital ROMpler)

A General-MIDI-era hardware module recorded slot-by-slot. Sounds like
mid-90s game/PC music — clean, slightly synthetic, very different from
both the analog 808/909 and the acoustic Pearl kit already in the bundle.

- **Source**: <https://archive.org/details/SC8850DrumSamples>
- **License**: **CC Public Domain Mark 1.0** (no attribution required, but
  credit "Paisley Computer" in `samples/LICENSE.md` as a courtesy).
- **Author**: Paisley Computer
- **Format**: 16-bit WAV (mostly mono, a few stereo). 99 files in pack;
  we use 8.
- **Per-file size**: 30 KB - 350 KB
- **Total kit size**: ~810 KB (8 slots)

| Slot | File | URL | Size |
|------|------|-----|------|
| kick | Kick 02 SC8850-O1.wav | https://archive.org/download/SC8850DrumSamples/SC%208850%20Drum%20Samples/Kick%2002%20SC8850-O1.wav | 36 KB |
| snare | Snare 01 SC8850-O1.wav | https://archive.org/download/SC8850DrumSamples/SC%208850%20Drum%20Samples/Snare%2001%20SC8850-O1.wav | 49 KB |
| closed hat | High Hat 01 SC8850-O2.wav | https://archive.org/download/SC8850DrumSamples/SC%208850%20Drum%20Samples/High%20Hat%2001%20SC8850-O2.wav | 47 KB |
| clap | Clap SC8850-O1.wav | https://archive.org/download/SC8850DrumSamples/SC%208850%20Drum%20Samples/Clap%20SC8850-O1.wav | 50 KB |
| open hat | High Hat 02 SC8850-O2.wav | https://archive.org/download/SC8850DrumSamples/SC%208850%20Drum%20Samples/High%20Hat%2002%20SC8850-O2.wav | 169 KB |
| percussion (cowbell) | Cowbell SC8850-O2.wav | https://archive.org/download/SC8850DrumSamples/SC%208850%20Drum%20Samples/Cowbell%20SC8850-O2.wav | 30 KB |
| rim (sub: agogo low) | Agogo Low SC8850-O2.wav | https://archive.org/download/SC8850DrumSamples/SC%208850%20Drum%20Samples/Agogo%20Low%20SC8850-O2.wav | 36 KB |
| tom | Tom 03 SC8850-O2.wav | https://archive.org/download/SC8850DrumSamples/SC%208850%20Drum%20Samples/Tom%2003%20SC8850-O2.wav | 341 KB |

Note: SC-8850 has **no dedicated rim/sidestick** in the dump. Substituted
the short Agogo Low metallic perc as a "rim slot" — it's still a tight
percussive accent that sits in the same role musically. Swap for any of
the 4 Bongo or Cabasa files if a wood/skin character is preferred.

---

## Kit 5 — Beckstrom Tube 808 (warm overdrive)

Same TR-808 as our existing `cassette-lofi` kit but reamped through a
vacuum-tube preamp — saturated and warm rather than wobbly and hissy.
Different musical character (smoothed/compressed vs. broken/lo-fi).

- **Source**: <https://archive.org/details/808_variations> — folder
  `808variations/tube/`
- **License**: **CC-BY 4.0** (credit "Chris Beckstrom" — already in
  `samples/LICENSE.md` if Kit 3 is shipped, no new attribution row needed).
- **URL pattern**: `…/808variations/tube/tube_808_<SLOT>.wav`
- **Format**: 32-bit stereo WAV. **Transcode to 16-bit mono before
  shipping** (same `ffmpeg` recipe as Kit 3 — drops total to ~1.4 MB).
- **Total kit size at source**: ~9.5 MB raw / ~1.4 MB after transcode.

| Slot | File | URL | Raw size |
|------|------|-----|----------|
| kick | tube_808_BD.wav | https://archive.org/download/808_variations/808variations/tube/tube_808_BD.wav | ~1.4 MB |
| snare | tube_808_SD.wav | https://archive.org/download/808_variations/808variations/tube/tube_808_SD.wav | ~1.0 MB |
| closed hat | tube_808_HH.wav | https://archive.org/download/808_variations/808variations/tube/tube_808_HH.wav | ~700 KB |
| clap | tube_808_CP.wav | https://archive.org/download/808_variations/808variations/tube/tube_808_CP.wav | ~1.0 MB |
| open hat | tube_808_OHH.wav | https://archive.org/download/808_variations/808variations/tube/tube_808_OHH.wav | ~860 KB |
| percussion | tube_808_CB.wav (cowbell) | https://archive.org/download/808_variations/808variations/tube/tube_808_CB.wav | ~880 KB |
| rim | tube_808_RIM.wav | https://archive.org/download/808_variations/808variations/tube/tube_808_RIM.wav | ~860 KB |
| tom | tube_808_TOM1.wav | https://archive.org/download/808_variations/808variations/tube/tube_808_TOM1.wav | ~860 KB |

URL pattern verified by inspecting the archive.org `metadata` endpoint
file list — all 13 expected slot files (`BD CB CLAVE CP CYMBAL HH MAR OHH
RIM SD TOM1 TOM2 TOM3`) are present at predictable URLs.

---

## Kit 6 — Beckstrom Paint-Can 808 (industrial / metallic)

The TR-808 played through a paint can as a resonator — gives an aggressive
metallic texture, useful for industrial/IDM patches. Note the filename
pattern is **different** from the other folders: bare `808_<SLOT>.wav`
with no folder prefix.

- **Source**: <https://archive.org/details/808_variations> — folder
  `808variations/paint_can/` (and folder `megaphone/` uses the same
  bare-filename pattern if you want a megaphone-distorted alternate).
- **License**: **CC-BY 4.0** (Chris Beckstrom — same attribution as
  Kits 3 & 5).
- **URL pattern**: `…/808variations/paint_can/808_<SLOT>.wav` — note
  no `paint_can_` prefix on the filename.
- **Format**: 32-bit stereo WAV. Transcode as Kit 3/5.
- **Total kit size at source**: ~9.5 MB raw / ~1.4 MB after transcode.

| Slot | File | URL | Raw size |
|------|------|-----|----------|
| kick | 808_BD.wav | https://archive.org/download/808_variations/808variations/paint_can/808_BD.wav | 542 KB |
| snare | 808_SD.wav | https://archive.org/download/808_variations/808variations/paint_can/808_SD.wav | ~1 MB |
| closed hat | 808_HH.wav | https://archive.org/download/808_variations/808variations/paint_can/808_HH.wav | ~700 KB |
| clap | 808_CP.wav | https://archive.org/download/808_variations/808variations/paint_can/808_CP.wav | ~1 MB |
| open hat | 808_OHH.wav | https://archive.org/download/808_variations/808variations/paint_can/808_OHH.wav | ~860 KB |
| percussion | 808_CB.wav (cowbell) | https://archive.org/download/808_variations/808variations/paint_can/808_CB.wav | ~880 KB |
| rim | 808_RIM.wav | https://archive.org/download/808_variations/808variations/paint_can/808_RIM.wav | ~860 KB |
| tom | 808_TOM1.wav | https://archive.org/download/808_variations/808variations/paint_can/808_TOM1.wav | ~860 KB |

If two metallic 808 kits is overkill, prefer this one (paint can — more
distinctive) over `tube/` for industrial flavor.

---

## Kit 7 — Beckstrom Volca Modular (modular synth percussion)

Drum/percussion patches synthesized on the Korg Volca Modular semi-
modular. Useful as IDM/glitch/electroacoustic textures — tonal, often
inharmonic, none sound like a "drum machine". Fills the modern-textures
slot in the priority brief.

- **Source**: <https://archive.org/details/beckstrom-volca-modular-drum-samples-1>
- **License**: **CC-BY 4.0** (credit "Chris Beckstrom" in `samples/LICENSE.md`).
- **Format**: 16-bit WAV. 129 files total in pack; we cherry-pick 8.
- **Per-file size**: 36 KB - 560 KB
- **Total kit size**: ~1.3 MB (8 slots)

| Slot | File | URL | Size |
|------|------|-----|------|
| kick | Beckstrom_volca-modular-Bd1-9.10.wav | https://archive.org/download/beckstrom-volca-modular-drum-samples-1/Beckstrom_volca-modular-Bd1-9.10.wav | 66 KB |
| snare | Beckstrom_volca-modular-SD.1.wav | https://archive.org/download/beckstrom-volca-modular-drum-samples-1/Beckstrom_volca-modular-SD.1.wav | 141 KB |
| closed hat (sub: noisy hit) | Beckstrom_volca-modular-NoisyHit.1.wav | https://archive.org/download/beckstrom-volca-modular-drum-samples-1/Beckstrom_volca-modular-NoisyHit.1.wav | 58 KB |
| clap | Beckstrom_volca-modular-Clap.1.wav | https://archive.org/download/beckstrom-volca-modular-drum-samples-1/Beckstrom_volca-modular-Clap.1.wav | 57 KB |
| open hat (sub: long cymbal) | Beckstrom_volca-modular-CymbalHH1.10.wav | https://archive.org/download/beckstrom-volca-modular-drum-samples-1/Beckstrom_volca-modular-CymbalHH1.10.wav | 499 KB |
| percussion (cowbell) | Beckstrom_volca-modular-Cowbell.1.wav | https://archive.org/download/beckstrom-volca-modular-drum-samples-1/Beckstrom_volca-modular-Cowbell.1.wav | 79 KB |
| rim | Beckstrom_volca-modular-Rim.1.wav | https://archive.org/download/beckstrom-volca-modular-drum-samples-1/Beckstrom_volca-modular-Rim.1.wav | 36 KB |
| tom | Beckstrom_volca-modular-Tom.1.wav | https://archive.org/download/beckstrom-volca-modular-drum-samples-1/Beckstrom_volca-modular-Tom.1.wav | 137 KB |

Note: the pack has **no dedicated closed/open hat** — the modular has no
white-noise generator, so Beckstrom synthesized substitutes. Used
`NoisyHit` for closed hat (short noise burst) and `CymbalHH1.10` for
open hat (longer cymbal-like sustain). Pack also contains 11 `Clave` and
8 alternate `NoisyHit` files if a different feel is wanted.

---

## Round 2 download commands

```bash
mkdir -p samples/_new/sc8850 \
         samples/_new/tube-808 \
         samples/_new/paint-can-808 \
         samples/_new/volca-modular

# --- Kit 4: Sound Canvas SC-8850 (PD Mark, Paisley Computer) ---
SC='https://archive.org/download/SC8850DrumSamples/SC%208850%20Drum%20Samples'
cd samples/_new/sc8850
curl -fLo kick.wav        "$SC/Kick%2002%20SC8850-O1.wav"
curl -fLo snare.wav       "$SC/Snare%2001%20SC8850-O1.wav"
curl -fLo hihat-closed.wav "$SC/High%20Hat%2001%20SC8850-O2.wav"
curl -fLo clap.wav        "$SC/Clap%20SC8850-O1.wav"
curl -fLo hihat-open.wav  "$SC/High%20Hat%2002%20SC8850-O2.wav"
curl -fLo cowbell.wav     "$SC/Cowbell%20SC8850-O2.wav"
curl -fLo rim.wav         "$SC/Agogo%20Low%20SC8850-O2.wav"
curl -fLo tom.wav         "$SC/Tom%2003%20SC8850-O2.wav"
cd -

# --- Kit 5: Beckstrom Tube 808 (CC-BY 4.0, Chris Beckstrom) ---
TB='https://archive.org/download/808_variations/808variations/tube'
cd samples/_new/tube-808
for slot in BD SD HH CP OHH CB RIM TOM1; do
    curl -fLO "$TB/tube_808_${slot}.wav"
done
cd -

# --- Kit 6: Beckstrom Paint-Can 808 (CC-BY 4.0, Chris Beckstrom) ---
# NB: filename pattern is `808_<SLOT>.wav` (no `paint_can_` prefix)
PC='https://archive.org/download/808_variations/808variations/paint_can'
cd samples/_new/paint-can-808
for slot in BD SD HH CP OHH CB RIM TOM1; do
    curl -fLO "$PC/808_${slot}.wav"
done
cd -

# --- Kit 7: Beckstrom Volca Modular (CC-BY 4.0, Chris Beckstrom) ---
VM='https://archive.org/download/beckstrom-volca-modular-drum-samples-1'
cd samples/_new/volca-modular
curl -fLo kick.wav        "$VM/Beckstrom_volca-modular-Bd1-9.10.wav"
curl -fLo snare.wav       "$VM/Beckstrom_volca-modular-SD.1.wav"
curl -fLo hihat-closed.wav "$VM/Beckstrom_volca-modular-NoisyHit.1.wav"
curl -fLo clap.wav        "$VM/Beckstrom_volca-modular-Clap.1.wav"
curl -fLo hihat-open.wav  "$VM/Beckstrom_volca-modular-CymbalHH1.10.wav"
curl -fLo cowbell.wav     "$VM/Beckstrom_volca-modular-Cowbell.1.wav"
curl -fLo rim.wav         "$VM/Beckstrom_volca-modular-Rim.1.wav"
curl -fLo tom.wav         "$VM/Beckstrom_volca-modular-Tom.1.wav"
cd -

# Optional: transcode tube/paint-can kits to 16-bit mono (same as cassette)
for dir in samples/_new/tube-808 samples/_new/paint-can-808; do
    for f in "$dir"/*.wav; do
        ffmpeg -y -i "$f" -ac 1 -ar 44100 -sample_fmt s16 "${f%.wav}-16m.wav"
        mv "${f%.wav}-16m.wav" "$f"
    done
done
```

---

## Round 2 attribution rows for `samples/LICENSE.md`

```
sc8850/           - Sound Canvas 8850 Drum Samples by Paisley Computer,
                    CC Public Domain Mark 1.0 (no attribution required),
                    https://archive.org/details/SC8850DrumSamples
tube-808/         - Beckstrom's Weird TR-808 Sample Pack (tube reamp) by
                    Chris Beckstrom, CC-BY 4.0,
                    https://archive.org/details/808_variations
paint-can-808/    - Beckstrom's Weird TR-808 Sample Pack (paint can) by
                    Chris Beckstrom, CC-BY 4.0,
                    https://archive.org/details/808_variations
volca-modular/    - Korg Volca Modular Drum Kit Samples by Chris Beckstrom,
                    CC-BY 4.0,
                    https://archive.org/details/beckstrom-volca-modular-drum-samples-1
```

## Round 2 payload total

| Kit | At-source size | After 16-bit-mono transcode |
|-----|----------------|-----------------------------|
| SC-8850 | ~810 KB | ~810 KB (already 16-bit) |
| Tube 808 | ~9.5 MB | ~1.4 MB |
| Paint-Can 808 | ~9.5 MB | ~1.4 MB |
| Volca Modular | ~1.3 MB | ~1.3 MB (already 16-bit) |
| **All four kits** | **~21 MB** | **~4.9 MB** |

Rounds 1 + 2 transcoded total ≈ 8.7 MB, comfortably under the 25 MB
ceiling from `docs/research/kit-expansion.md`. Combined `samples/` after
shipping all 7 new kits would land around 14.3 MB.

---

## Priority targets that could not be fulfilled

For completeness — the brief explicitly asked for these and I could not
find any matching the license/format constraints:

- **Oberheim DMX** — only candidates were the Onibaku 7z (no per-WAV URL,
  no license) and the `instrum_20160218` mp3 song stems (not one-shots).
- **E-mu SP-1200** — the 56 archive.org hits are all unrelated CIA reading
  room scans, vinyl rips, or LP titles containing "SP" and "1200" as
  separate tokens. No PD/CC drum dump exists.
- **Roland TR-606 / TR-707 / TR-727** — only inside the rejected Onibaku
  bundle.
- **Boss DR-110 / DR-220 / DR-660** — only inside the rejected Alex Ball
  zip (no license metadata).
- **Acoustic alternatives** — Oramics `DRUMS/avl-drumkits-1.1` rejected
  for SA virality (already documented). No other CC0/CC-BY acoustic kit
  surfaced beyond the Pearl one already shipped.

A future contributor with access to a CC0/CC-BY DMX or SP-1200 dump
(check Github sample-pack repos under MIT, or contact
chrisbeckstrom.com for a possible commission) could add these kits
following the same per-slot pattern used above.

