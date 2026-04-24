# Kit Expansion Research

Research deliverable for expanding 0x808's built-in drum kit library beyond
the current six (808, 909, 505, MRK-2, CR-78, LM-2). All sources below were
verified by web search in April 2026; sample-pack URLs come and go, so
re-verify before bundling.

Current samples directory is **5.6 MB** on disk. Project ships an ~80 MB
AppImage. Existing license file at `samples/LICENSE.md` permits royalty-free
redistribution of recordings of classic Roland-style drum machines.

---

## 1. Recommended kits to add

Priority order. The first four are the most "iconic machines not already
covered" and have verified public-domain sources. Items 5-8 fill genre gaps
(trap, lo-fi, acoustic, breaks) rather than emulating specific hardware.

### 1. LinnDrum LM-2 (proper, replacing the LM-2 alias)

Why it matters: defined the sound of 80s pop (Prince, Michael Jackson, Phil
Collins). The current "LM-2" kit in `kits.c` is an *alias* that re-uses 808
samples (rows 119-131); shipping the actual LinnDrum samples would be a
correctness win.

Source: **Oramics `sampled` repo, `DM/LM-2/`**, license: **Public Domain**,
WAV format, 30 samples (kick x2, snare x3, hihat x4, tom x5, conga x6,
crash, ride, clap, cowbell, cabasa, tambourine, sticks x3).
- Browser: https://oramics.github.io/sampled/DM/LM-2/
- Repo: https://github.com/oramics/sampled (MIT for code, samples per-instrument PD)

### 2. Oberheim DMX

Why it matters: the other defining 80s machine (New Order's "Blue Monday",
early Run-DMC). Punchier and more aggressive than the LinnDrum.

Sources to evaluate (none of these are CC0; all require a license check):
- Angelspit free DMX pack (Dropbox-hosted, "free vintage drum machine samples"):
  https://www.angelspit.net/oberheim-dmx/
- Internet Archive "Oberheim DMX Drums - Stock DMX Sounds":
  https://archive.org/details/dmx-stock-dmx-sounds
- Electrongate publishes the actual DMX EPROM binary images plus WAV previews
  ("for personal use"): http://electrongate.com/dmxfiles/eproms.html
  **Flag:** "personal use" wording rules this out for redistribution.
- Waxadisc factory-single-hits pack: https://www.waxadisc.com/post/free-oberheim-dmx-drum-machine-how-to-produce-blue-monday-by-new-order

Best bet: contact Angelspit / Waxadisc to confirm CC0/permissive terms, or
re-record from a hardware unit. Otherwise prefer the SP-1200 below.

### 3. E-mu SP-1200

Why it matters: the foundational golden-age hip-hop sampler (Pete Rock,
DJ Premier). Crunchy 12-bit/26.04 kHz character.

Verified free sources:
- Internet Archive "Turbo Kit": https://archive.org/details/sp_1200_drums_turbokit
- Internet Archive "Percotron": https://archive.org/details/sp_1200_drums_percotron
- Reverb x Nick Hook free pack (25 one-shots, account required):
  https://bedroomproducersblog.com/2021/01/15/nick-hook-sp-1200-sample-pack/
- "Free SP From Mars" (samplesfrommars.com) - email required, free tier of a
  paid pack. **Flag:** mailing-list signup is friction; license is "free for
  use in your music" not "free to redistribute".

Use Internet Archive items where the uploader explicitly marks public domain.

### 4. Roland CR-78 (real samples, replacing the alias)

Why it matters: same situation as LM-2 - the current "CR-78" entry in
`kits.c` is an alias on 808 samples (rows 105-117). The CR-78 was the first
microprocessor-based drum machine and is the heartbeat of "In the Air
Tonight" and countless ambient records.

Source: **Oramics `sampled` repo, `DM/CR-78/`**, **Public Domain**, 17 WAVs
(kick + accent, snare + accent, hihat + accent + metal, bongo h/l, conga-l,
cowbell, cymbal, guiro x2, rim, tamb x2).
- https://oramics.github.io/sampled/DM/CR-78/

### 5. Yamaha RX-5 / Boss DR-660 (digital 80s/90s pair)

Why it matters: 16-bit PCM digital machines; tighter, cleaner sound than the
analog Rolands. RX-5 is on countless 80s pop records; DR-660 is a budget
classic for 90s home studios.

Verified free sources:
- Boss DR-660: all 255 samples, dry, on Internet Archive:
  https://archive.org/details/DR660Samples
  Also mirrored at: https://sounds.martinjanus.com/free-samples/boss-dr-660-sample-pack/
- Yamaha RX-5: paid sources are easy (Noiiz, SamplePhonics); no clean
  CC0 source surfaced. Recommend skipping unless someone donates a
  recording.

### 6. Modern trap 808 sub-bass kit

Why it matters: the current 808 kick is the original short-decay TR-808
hit. Modern trap uses pitched, long-sustaining "808s" as melodic bass. This
is the #1 sound 0x808 users will *expect* given the project name.

Verified free-ish sources (none cleanly CC0 - all are "royalty-free for
music use"; redistribution-in-software needs a per-source check):
- TriSamples 808 Trapstep Vol 1: https://trisamples.com/808-trapstep-pack-vol-1/
- Patchbanks Free 808 Bass Samples (130 one-shots, varied decay):
  https://www.patchbanks.com/urban/free-808-bass-samples/
- Pixabay 808 bass sound effects (Pixabay Content License, no attribution):
  https://pixabay.com/sound-effects/search/808%20bass/

The Pixabay pool is the safest redistribution-wise.

### 7. Lo-fi / dusty kit

Why it matters: pairs with the project's existing visual themes
(Vaporwave/Midnight). Tape-saturated, vinyl-crackle drums for chillhop.

Verified sources:
- SoundPacks.com "Lo-Fi Hip-Hop Drum Kit" (free, royalty-free):
  https://soundpacks.com/free-sound-packs/lo-fi-hip-hop-drum-kit/
- Freesound CC0 packs by `deadrobotmusic`:
  https://freesound.org/people/deadrobotmusic/packs/32405/
- Producer Space (2000+ CC0 samples): https://producerspace.com/

### 8. Acoustic / breakbeat kit

Why it matters: every "real drums" use case currently requires the user to
import samples. A single curated acoustic kit covers it.

Verified sources:
- Mailbox Badger PD Drum Samples Vol 1 & 2 (acoustic + processed,
  public domain, attribution appreciated):
  - https://archive.org/details/HeatDish
  - https://archive.org/details/mailboxbadgerdrumsamplesvolume2
- Freesound CC0 acoustic packs (browse + curate): https://freesound.org/browse/tags/cc0
- For a separate "Amen break" loop slot: Pixabay amen-break SFX (Pixabay
  License): https://pixabay.com/sound-effects/search/amen-break/

---

## 2. Licensing audit guidance

`samples/LICENSE.md` currently makes a blanket royalty-free claim with no
per-directory provenance. Any new kit needs the following before bundle:

1. **Identify the actual license string.** Treat these distinct categories:
   - **CC0 / Public Domain** - safe, no attribution required. Preferred.
   - **CC-BY** - safe to bundle but requires per-kit attribution in
     `LICENSE.md` and ideally in an in-app "Credits" panel.
   - **"Royalty-free" / "free for use in your music"** - usually does *not*
     authorize redistribution as part of a software product. Reject unless
     the author confirms in writing.
   - **"Personal use only"** - reject.
   - **EPROM/firmware images (e.g. Electrongate DMX)** - reject; these are
     copyrighted by the original manufacturer regardless of how they're
     wrapped.

2. **Capture provenance per directory.** Add a row to `samples/LICENSE.md`
   for each new kit: source URL, author, license, date downloaded, and
   SHA-256 of the source archive. This lets future maintainers re-verify.

3. **Prefer Tier-1 sources** in this order: Oramics `sampled` repo,
   Freesound CC0 tag, Pixabay, Producer Space, Internet Archive items
   where the uploader explicitly chose Public Domain at upload.

4. **Flag for rejection right now**:
   - Electrongate DMX EPROMs (personal use)
   - Samples From Mars "free" packs (TOS = "for use in music", not
     redistribution; mailing-list signup also problematic for CI)
   - Anything from SamplePhonics, Noiiz, Loopmasters, Splice (commercial
     paid packs even when "free" preview - their EULAs forbid
     redistribution)
   - SoundCloud-hosted ZIPs (terms vary, hard to verify)

5. **For CC-BY samples**, add an "Attribution" section to
   `samples/LICENSE.md` listing original author + URL per file or per kit.

---

## 3. Install size impact

Reference math: 16-bit 44.1 kHz mono = **88.2 KB/sec**. A typical drum
one-shot is 0.05-2.0s (kick ~0.5s, snare ~0.3s, open hat ~1.5s, sub-808
up to 4s).

Per 8-slot kit, expect:
- Compact analog kit (808/909-style, all hits ≤ 1s): ~150-300 KB
- Mixed digital kit (LinnDrum-style with crash/ride): ~400-700 KB
- Trap 808 kit with long sub kicks (4s sustains): ~1.0-1.5 MB
- Acoustic/breakbeat kit (longer cymbals): ~700 KB - 1.2 MB

Adding all 8 recommended kits at moderate sample counts: **~6-8 MB**.

**Recommended ceiling: keep `samples/` under 25 MB total** (currently
5.6 MB). That's under a 5x growth and keeps the AppImage well under 100 MB.
If a contributor wants a fuller pack (e.g. all 255 DR-660 samples), ship a
trimmed 8-slot subset built-in and offer the full pack as an optional
download via the auto-import directory below.

Conversion command for new contributions (already documented in
`docs/SAMPLE_SOURCES.md`):
```
ffmpeg -i input.wav -ac 1 -ar 44100 -sample_fmt s16 output.wav
```

---

## 4. Auto-import directory layout proposal

The codebase already uses platform-correct user data dirs in
`src/app/session.c`:
- Linux: `$XDG_DATA_HOME/0x808/` (fallback `~/.local/share/0x808/`)
- Windows: `%APPDATA%\0x808\`
- macOS: `~/Library/Application Support/0x808/`

Proposed extension - a `kits/` subdirectory scanned at startup:

```
<user_data_dir>/0x808/
  kits/
    my-trap-kit/
      kick.wav             # required
      snare.wav            # required
      hihat-closed.wav     # required
      clap.wav             # optional, slot 4
      hihat-open.wav       # optional, slot 5
      cowbell.wav          # optional, slot 6 (any of: cowbell, perc, ride, cymbal)
      rimshot.wav          # optional, slot 7 (any of: rimshot, rim, sidestick)
      tom.wav              # optional, slot 8 (any of: tom, tom-h, tom-l)
      kit.json             # optional override (display name, icon, sample mapping)
    another-kit/
      ...
```

**File-naming convention** (case-insensitive, glob match on stem):

| Slot | Required tokens (any one matches) |
|------|------------------------------------|
| 0 kick     | `kick*`, `bd*`, `bassdrum*` |
| 1 snare    | `snare*`, `sd*` |
| 2 closed hh| `hihat-closed*`, `hh-closed*`, `ch*`, `closedhat*` |
| 3 clap     | `clap*`, `cp*`, `snap*` |
| 4 open hh  | `hihat-open*`, `hh-open*`, `oh*`, `openhat*` |
| 5 perc     | `cowbell*`, `cb*`, `perc*`, `ride*`, `cymbal*` |
| 6 rimshot  | `rim*`, `rimshot*`, `sidestick*`, `click*` |
| 7 tom      | `tom*`, `lt*`, `mt*`, `ht*` |

Kit display name = parent directory name unless `kit.json` overrides:
```json
{ "name": "My Trap Kit", "slot_overrides": { "5": "ride-bell.wav" } }
```

A folder is "scannable" if it contains at least the kick + snare + closed
hihat. Missing optional slots fall back to silence (or the previously
loaded sample). This mirrors the `num_slots` field already on
`sq_kit_def_t`.

Documentation note for `samples/LICENSE.md`: user-imported kits are not
covered by the project's license; the user is responsible for the
licensing of files they drop in.

---

## 5. Implementation sketch

Goal: keep `sq_kit_def_t` compatible, support both built-in (compile-time
constant) and scanned (runtime heap-allocated) kits behind a single
enumeration API.

### Header changes (`src/engine/kits.h`)

```c
#define SQ_NUM_BUILTIN_KITS  6
#define SQ_MAX_USER_KITS    32
#define SQ_MAX_TOTAL_KITS   (SQ_NUM_BUILTIN_KITS + SQ_MAX_USER_KITS)
/* Bump path length for absolute user paths; built-ins still fit in 64. */
#define SQ_KIT_PATH_LEN    256

typedef enum { SQ_KIT_BUILTIN, SQ_KIT_USER } sq_kit_origin_t;

typedef struct {
    char name[SQ_KIT_NAME_LEN];
    char paths[SQ_KIT_SLOTS][SQ_KIT_PATH_LEN];
    int  num_slots;
    sq_kit_origin_t origin;   /* NEW */
    /* For user kits, paths[] are absolute. For built-ins, relative to base_dir. */
} sq_kit_def_t;

extern const sq_kit_def_t sq_kits[SQ_NUM_BUILTIN_KITS];

/* Scan user data dir, populate user_kits[], return count. Idempotent. */
int sq_kits_scan_user_dir(const char *user_data_dir);

/* Unified accessor: 0..SQ_NUM_BUILTIN_KITS-1 = built-in,
 * SQ_NUM_BUILTIN_KITS.. = scanned user kits. */
const sq_kit_def_t *sq_kit_get(int index);
int sq_kit_count(void);
```

### Implementation notes

- Keep the `const sq_kits[]` array exactly as today (constant data, no heap).
- Add a `static sq_kit_def_t g_user_kits[SQ_MAX_USER_KITS];` plus
  `static int g_user_kit_count = 0;` in `kits.c`.
- `sq_kits_scan_user_dir()` uses `opendir`/`readdir` (POSIX) or
  `FindFirstFile`/`FindNextFile` (Win32) - already a pattern in
  `user_patterns.c` per the grep above.
- For each subdirectory, glob-match files against the slot tokens from
  Section 4. If kick/snare/closed-hh all resolve, populate a
  `sq_kit_def_t` with `origin = SQ_KIT_USER` and absolute paths.
- `sq_kit_load()` switches on `origin`: built-ins go through the existing
  `base_dir` resolution; user kits skip resolution entirely (paths are
  already absolute).
- Frontends iterate via `sq_kit_count()` / `sq_kit_get(i)` instead of the
  raw `sq_kits` array. The kit-picker UI shows a divider between built-in
  and user kits.
- Call `sq_kits_scan_user_dir()` once at engine init, after `sq_engine_init`
  but before the first `sq_kit_load(0, ...)`. Rescan on demand from a
  Settings panel button.
- Cross-platform: reuse the same path resolution as `session.c` for the
  user data dir; do not introduce a new env var.

### Senior-checklist items hit by this change

- New engine field (`origin`) - covered by `sq_engine_init` memset; needs
  no special teardown.
- Frontend parity - both `main_gui.c` and `main_gtk.c` must use the new
  `sq_kit_count()` / `sq_kit_get()` API. The kit picker in each frontend
  needs the divider between built-in and user kits.
- No heap allocation in the audio path - scanning happens at startup,
  off the audio thread.
- Plugin build - the user-kits directory should resolve identically; verify
  the plugin host's working directory is not the audio host's CWD.

---

## Sources verified during this research

- [Oramics sampled - LM-2](https://oramics.github.io/sampled/DM/LM-2/)
- [Oramics sampled - CR-78](https://oramics.github.io/sampled/DM/CR-78/)
- [Oramics sampled - GitHub](https://github.com/oramics/sampled)
- [Mailbox Badger PD Drum Samples Vol 1 (Internet Archive)](https://archive.org/details/HeatDish)
- [Mailbox Badger PD Drum Samples Vol 2 (Internet Archive)](https://archive.org/details/mailboxbadgerdrumsamplesvolume2)
- [Boss DR-660 Sample Pack (Internet Archive)](https://archive.org/details/DR660Samples)
- [Boss DR-660 Sample Pack (Martin Janus mirror)](https://sounds.martinjanus.com/free-samples/boss-dr-660-sample-pack/)
- [SP-1200 Turbo Kit (Internet Archive)](https://archive.org/details/sp_1200_drums_turbokit)
- [SP-1200 Percotron (Internet Archive)](https://archive.org/details/sp_1200_drums_percotron)
- [Reverb / Nick Hook SP-1200 pack (BPB writeup)](https://bedroomproducersblog.com/2021/01/15/nick-hook-sp-1200-sample-pack/)
- [Angelspit DMX free samples](https://www.angelspit.net/oberheim-dmx/)
- [Internet Archive - DMX Stock Sounds](https://archive.org/details/dmx-stock-dmx-sounds)
- [Waxadisc DMX factory hits](https://www.waxadisc.com/post/free-oberheim-dmx-drum-machine-how-to-produce-blue-monday-by-new-order)
- [Electrongate DMX EPROMs (NOT redistributable)](http://electrongate.com/dmxfiles/eproms.html)
- [TriSamples 808 Trapstep Vol 1](https://trisamples.com/808-trapstep-pack-vol-1/)
- [Patchbanks Free 808 Bass Samples](https://www.patchbanks.com/urban/free-808-bass-samples/)
- [Pixabay 808 bass sound effects](https://pixabay.com/sound-effects/search/808%20bass/)
- [Pixabay amen-break sound effects](https://pixabay.com/sound-effects/search/amen-break/)
- [SoundPacks Lo-Fi Hip-Hop Drum Kit](https://soundpacks.com/free-sound-packs/lo-fi-hip-hop-drum-kit/)
- [Freesound deadrobotmusic CC0 snare pack](https://freesound.org/people/deadrobotmusic/packs/32405/)
- [Freesound CC0 tag browse](https://freesound.org/browse/tags/cc0)
- [Producer Space CC0 packs](https://producerspace.com/)
