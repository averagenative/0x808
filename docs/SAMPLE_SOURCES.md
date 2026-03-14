# Sample Sources & Expansion Guide

Reference document for finding CC0/public domain drum samples to complement
the 0x808 sequencer's built-in library.

---

## 1. Current Sample Inventory

The project ships 72 WAV one-shots across 8 directories. Format varies
(16-bit/24-bit, 44.1/48 kHz, all mono).

### Roland Drum Machine Kits

| Directory | Count | Contents |
|-----------|-------|----------|
| `808/`    | 26    | TR-808: kick (x3), snare (x3), clap (x3), closed hat (x3), open hat (x3), cowbell, hi conga, lo conga, maracas, mid conga, hi tom, mid tom, lo tom, cymbal, rimshot |
| `909/`    | 16    | TR-909: kick (x2), snare (x2), clap (x2), closed hat (x2), open hat (x2), cymbal, ride, rim, hi/mid/lo tom |
| `505/`    | 16    | TR-505: kick, snare, clap, closed hat, open hat, crash, ride, rim, hi/lo cowbell, hi/lo conga, timbale, hi/mid/lo tom |

### General / Assorted

| Directory      | Count | Contents |
|----------------|-------|----------|
| `kicks/`       | 1     | Single kick |
| `snares/`      | 1     | Single snare |
| `hihats/`      | 1     | Single hi-hat |
| `percussion/`  | 1     | Single clap |
| `mrk2/`        | 10    | Block, bongo, clave, cymbal (long + short), closed hat, open hat, kick, snare, tom |

### What We Have vs. What We Are Missing

**Well covered:**
- Kicks (many variations across 808/909/505)
- Snares (multiple across kits)
- Hi-hats (open and closed across kits)
- Claps (across 808/909/505)
- Toms (hi/mid/lo across all kits)
- Latin percussion (congas, cowbell, clave, bongo, timbale)

**Gaps:**
- No shakers, tambourines, or maracas one-shots (808 maracas is the only one)
- No long-sustaining 808 sub-bass kicks (trap-style pitched 808s)
- No acoustic/live drum recordings
- No electronic/EDM-style processed drums (distorted kicks, layered snares)
- No finger snaps
- No rim clicks (distinct from rimshots)
- No wood blocks or cajon
- No brush snares or ghost notes
- No noise/texture percussion (vinyl crackle hits, tape stop FX)
- No Linndrum / DMX / CR-78 style sounds
- Limited variety in the standalone directories (kicks/, snares/, hihats/ each have only 1 sample)

---

## 2. Recommended CC0 / Public Domain Sources

### Tier 1: Verified CC0

These sources explicitly release samples under the Creative Commons Zero (CC0)
license, meaning no attribution is required and samples can be used for any
purpose including commercial distribution.

**Freesound.org**
- URL: https://freesound.org/browse/tags/cc0/
- Filter by tag `cc0` + `drum` or specific instrument names
- Notable contributors: deadrobotmusic (drum one-shot packs with kicks,
  snares, percussion), Erokia (electronic samples)
- Download format: WAV (various bit depths/sample rates)
- License: Per-sample; filter for CC0 explicitly

**Producer Space**
- URL: https://producerspace.com/
- 2000+ samples across 18 packs, all CC0 Public Domain
- Includes percussion and drum one-shots
- No attribution required

**Internet Archive -- Mailbox Badger Public Domain Drum Samples**
- Vol 1: https://archive.org/details/HeatDish
- Vol 2: https://archive.org/details/mailboxbadgerdrumsamplesvolume2
- Acoustic and processed drum one-shots
- Public domain (attribution appreciated but not required)

**Internet Archive -- Drum Machine Collection**
- URL: https://archive.org/details/drum-machines-collection
- Various classic drum machine samples (may include Linndrum, DMX, CR-78)
- Check individual item licensing before use

**Pixabay Sound Effects**
- URL: https://pixabay.com/sound-effects/search/drum/
- CC0-equivalent Pixabay License (no attribution required)
- One-shots and short loops available

### Tier 2: Royalty-Free (check license details)

These sources offer royalty-free samples, but the exact license may not be
CC0. Read each site's license terms before bundling with a distributed project.

**99Sounds -- 99 Drum Samples (I & II)**
- URL: https://99sounds.org/drum-samples/
- 219 samples in 24-bit WAV, created from analog/digital synths and acoustic
  drums via resampling, layering, and transient shaping
- Royalty-free; check terms for redistribution

**Goldbaby Free Packs**
- URL: https://www.goldbaby.co.nz/freestuff.html
- Volca Beats through MPC60 (89 x 24-bit WAV), plus other free packs
- High quality vintage-style processing
- Royalty-free for music production; check redistribution terms

**TriSamples -- 808 Trapstep Pack**
- Vol 1: https://trisamples.com/808-trapstep-pack-vol-1/
- Vol 2: https://trisamples.com/808-trapstep-pack-vol-2/
- 808-style trap drums including sustained sub-bass kicks
- Royalty-free for music; check redistribution terms

**Clark Audio Free Kits**
- Synthwave Kit: https://clarkaudio.com/free-synthwave-drum-kit/
- Dark Trap Kit: https://clarkaudio.com/free-dark-trap-drum-kit/
- Royalty-free; check terms

**Bedroom Producers Blog -- Free Drum Kits Roundup**
- URL: https://bedroomproducersblog.com/2021/07/02/free-drum-kits/
- Curated list of 11 free drum kits with quality commentary
- Good starting point for discovering packs

---

## 3. Samples to Complement Specific Genres

The 0x808 includes pattern presets for Trap, as well as visual themes
(Vaporwave, Neon, Midnight) that pair well with synthwave/darkwave production.
The following sample types would fill gaps for these genres.

### Trap / Hip-Hop

| Sample Type | Description | Where to Find |
|-------------|-------------|---------------|
| 808 sub kick | Long-sustaining, pitched sub-bass kick (the defining trap sound) | TriSamples 808 Trapstep, Clark Audio Dark Trap Kit |
| Distorted 808 | Saturated/clipped 808 kick variants | Freesound CC0 tag search: "808 distorted" |
| Trap snare | Tight, snappy snare with short decay | Clark Audio Dark Trap Kit |
| Trap hat rolls | Rapid hi-hat patterns (single hits for programming rolls) | Any 808/909 closed hat works; add velocity variation |
| Finger snap | Clean snap, often layered with clap | Freesound CC0 |
| Vocal chant hit | "Hey", "Yeah" one-shots | Freesound CC0 tag search |

### Darkwave / Synthwave / Retrowave

| Sample Type | Description | Where to Find |
|-------------|-------------|---------------|
| Gated reverb snare | Big 80s-style reverb snare (Phil Collins / Linn style) | Clark Audio Synthwave Kit, 99Sounds |
| Simmons-style tom | Electronic tom with pitch sweep | Freesound CC0, 99Sounds |
| LinnDrum kick/snare | Classic 80s digital drum machine | Archive.org drum machine collection |
| CR-78 sounds | Early Roland analog; simple, warm | Archive.org drum machine collection |
| Brush/side-stick | Subtle acoustic percussion for slower tempos | Freesound CC0 |
| Tambourine | 16th-note tambourine is a synthwave staple | Freesound CC0 |

### EDM / Techno / House

| Sample Type | Description | Where to Find |
|-------------|-------------|---------------|
| Processed kick | Layered, compressed, punchy kick | 99Sounds, Goldbaby |
| Noise snare | White noise burst snare | Freesound CC0 |
| Shaker | 16th-note shaker loop or one-shot | Freesound CC0 |
| Ride/bell | Clean ride cymbal and bell hit | Already have ride in 909/505; could add more variety |
| Clav/wood hit | Short percussive accent | Freesound CC0 |

---

## 4. Adding Your Own Samples

### Format Requirements

The 0x808 engine uses dr_wav for sample loading. Supported formats:

- **Format:** WAV (RIFF), PCM
- **Bit depth:** 16-bit or 24-bit (32-bit float also supported by dr_wav)
- **Channels:** Mono preferred (stereo will work but mono saves memory)
- **Sample rate:** 44100 Hz recommended (48000 Hz also works; engine resamples)
- **Length:** One-shot, typically under 2 seconds for drums (sub 808s can be longer)

### Directory Convention

Place samples in the `samples/` directory, organized by kit or category:

```
samples/
  808/            # Roland TR-808
  909/            # Roland TR-909
  505/            # Roland TR-505
  mrk2/           # MRK2 kit
  kicks/          # Assorted kicks
  snares/         # Assorted snares
  hihats/         # Assorted hi-hats
  percussion/     # Assorted percussion
  my-trap-kit/    # <-- your custom kit directory
```

The sample browser (see docs/FRONTEND_FEATURES.md, section 7) lets users
navigate to any directory and load WAV files at runtime, so custom samples
do not need to follow any specific naming convention.

### Quick Start: Adding Samples from Freesound

1. Go to https://freesound.org
2. Search for the sound you want (e.g., "tambourine one-shot")
3. On the left sidebar, filter by License: "Creative Commons 0"
4. Download the WAV file
5. (Optional) Convert to mono 44100 Hz 16-bit with ffmpeg:
   ```
   ffmpeg -i input.wav -ac 1 -ar 44100 -sample_fmt s16 output.wav
   ```
6. Place in the appropriate `samples/` subdirectory
7. Use the sample browser in 0x808 to load it onto a track

### Quick Start: Bulk Download from Archive.org

1. Visit the collection page (e.g., the Mailbox Badger link above)
2. Click "Download Options" and select the ZIP or individual WAV files
3. Extract and convert as needed
4. Place in `samples/` under a descriptive directory name

---

## 5. License Considerations for Distribution

If adding samples to the 0x808 repository (as opposed to personal use):

- **CC0 only:** Samples bundled with the project must be CC0 or public domain
  to avoid attribution/license complexity for downstream users.
- **Royalty-free is not CC0:** "Royalty-free" means you can use them in your
  music without per-use fees, but redistribution (shipping them in a software
  project) may not be permitted. Always check the specific license.
- **Document provenance:** Update `samples/LICENSE.md` when adding new kits,
  noting the source URL and license for each directory.
