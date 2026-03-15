/*
 * engine.h — Core data structures and API for the 0x808 DSP engine.
 *
 * This is the central header that defines every type the engine uses.
 * The engine (Layer 1) has ZERO knowledge of GUI, audio drivers, or plugin
 * formats. It takes transport state in, produces float audio buffers out.
 *
 * All code in src/engine/ includes only this header and <math.h>.
 * No <stdio.h>, no <stdlib.h> in the audio path — allocations happen at init.
 */

#ifndef SQ_ENGINE_H
#define SQ_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "engine/effects.h"
#include "engine/command_queue.h"

/* ─── Limits ──────────────────────────────────────────────────────────────── */

#define SQ_MAX_TRACKS      16   /* max tracks per pattern                    */
#define SQ_MAX_STEPS       64   /* max steps per track                       */
#define SQ_MAX_PATTERNS    64   /* max patterns in a project                 */
#define SQ_MAX_SECTIONS    32   /* max sections in a song arrangement        */
#define SQ_MAX_SAMPLES    128   /* max loaded samples                        */
#define SQ_MAX_VOICES      16   /* max simultaneous playback voices          */
#define SQ_SAMPLE_NAME_LEN 64   /* max chars for a sample name               */
#define SQ_PATTERN_NAME_LEN 32  /* max chars for a pattern name              */

/* ─── Sample: a loaded audio file decoded to float ────────────────────────── */

typedef struct {
    float   *data;              /* interleaved float samples (L,R,L,R,...)   */
    uint32_t num_frames;        /* total frames (1 frame = 1 sample per ch)  */
    uint32_t num_channels;      /* 1 = mono, 2 = stereo                     */
    uint32_t sample_rate;       /* original sample rate (e.g., 44100)        */
    char     name[SQ_SAMPLE_NAME_LEN];
} sq_sample_t;

/* ─── Voice: a single instance of a sample being played back ──────────────── */

typedef struct {
    bool     active;            /* is this voice currently producing audio?   */
    int      sample_index;      /* which sample in the engine's sample array  */
    double   position;          /* fractional playback position (in frames)   */
    double   rate;              /* playback rate (1.0 = normal, 2.0 = +12st) */
    float    velocity;          /* amplitude scale from step velocity (0-1)  */
    float    volume;            /* track volume (0-1)                        */
    float    pan;               /* track pan (-1 = left, 0 = center, 1 = R) */
    uint64_t start_time;        /* sample position when voice was triggered  */
} sq_voice_t;

/* ─── Step: one cell in the drum grid / piano roll ────────────────────────── */

typedef struct {
    uint8_t  velocity;          /* 0 = off, 1-127 = on (louder = higher)     */
    int8_t   pitch_offset;      /* semitones from root (-24 to +24)          */
    uint8_t  note;              /* MIDI note number (for piano roll mode)    */
    float    length;            /* note length in steps (for piano roll)     */
    float    param[4];          /* per-step parameter automation slots       */
} sq_step_t;

/* ─── Track: one row in the drum grid ─────────────────────────────────────── */

typedef enum {
    TRACK_SAMPLER,              /* plays a loaded audio sample               */
    TRACK_SYNTH,                /* plays the built-in synthesizer            */
    TRACK_SF2                   /* plays a SoundFont preset via TSF          */
} sq_track_type_t;

typedef struct {
    sq_track_type_t type;       /* sampler or synth                          */
    sq_step_t  steps[SQ_MAX_STEPS]; /* the step data for this track          */
    uint32_t   length;          /* how many steps are active (4-64)          */
    int        sample_index;    /* which sample to play (if type=SAMPLER)    */
    int        synth_preset;    /* which synth preset (if type=SYNTH)        */
    int        sf2_preset;      /* which SF2 preset (if type=SF2)            */
    float      volume;          /* track volume, 0.0 to 1.0                 */
    float      pan;             /* track pan, -1.0 (L) to 1.0 (R)          */
    bool       mute;            /* if true, track produces no audio          */
    bool       solo;            /* if true, only soloed tracks are heard     */
    float      humanize;        /* velocity randomization 0.0-1.0           */
    uint8_t    color_index;     /* 0-7, index into track color palette      */
    sq_effect_slot_t effects[MAX_TRACK_EFFECTS]; /* per-track insert effects */
} sq_track_t;

/* ─── Pattern: the thing you edit in the grid/piano roll ──────────────────── */

typedef struct {
    sq_track_t tracks[SQ_MAX_TRACKS];
    uint32_t   num_tracks;      /* how many tracks are in use (1-16)         */
    char       name[SQ_PATTERN_NAME_LEN];
} sq_pattern_t;

/* ─── Arrangement: chain of sections for song playback ────────────────────── */

typedef struct {
    int      pattern_index;     /* which pattern this section plays          */
    int      repeat_count;      /* how many times to loop (1-99)            */
} sq_section_t;

typedef struct {
    sq_section_t sections[SQ_MAX_SECTIONS];
    uint32_t     num_sections;
} sq_arrangement_t;

/* ─── Transport: timing, playback state, and mode ─────────────────────────── */

typedef enum {
    MODE_PATTERN,               /* loop current pattern (default editing)    */
    MODE_SONG,                  /* play arrangement linearly                 */
    MODE_PERFORM                /* trigger sections live                     */
} sq_play_mode_t;

typedef struct {
    double         bpm;             /* beats per minute (20-300)             */
    double         current_beat;    /* fractional beat position              */
    uint64_t       sample_position; /* absolute sample count since play     */
    bool           playing;         /* are we currently playing?             */
    sq_play_mode_t mode;            /* pattern / song / perform              */
    int            current_pattern; /* index of currently active pattern     */
    int            current_step;    /* which step is currently playing       */
    int            current_section; /* which section in arrangement          */
    int            queued_section;  /* next section to play (perform mode)   */
    bool           pattern_completed; /* set when pattern wraps to step 0   */
    int            section_repeat;    /* current repeat count within section */
    float          swing;           /* swing amount 0.0 (straight) to 1.0   */
} sq_transport_t;

/* ─── Synthesizer types ──────────────────────────────────────────────────── */

#define SQ_MAX_SYNTH_VOICES 16
#define SQ_MAX_SYNTH_PRESETS 64
#define SQ_WAVETABLE_SIZE  2048  /* samples per single-cycle waveform         */
#define SQ_NUM_WAVEFORMS      4  /* saw, square, triangle, sine               */
#define SQ_WT_MAX_FRAMES     64  /* max waveforms in a wavetable bank         */
#define SQ_WT_MAX_BANKS       8  /* max wavetable banks                       */

typedef enum {
    WAVE_SAW,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_SINE
} sq_waveform_t;

/* ADSR envelope — generates a 0.0-1.0 amplitude curve over time */
typedef enum {
    ENV_IDLE,                   /* not active                                */
    ENV_ATTACK,                 /* rising from 0 to 1                       */
    ENV_DECAY,                  /* falling from 1 to sustain                */
    ENV_SUSTAIN,                /* holding at sustain level                  */
    ENV_RELEASE                 /* falling from current to 0                */
} sq_env_stage_t;

typedef struct {
    float attack;               /* seconds (0.001 - 10.0)                   */
    float decay;                /* seconds (0.001 - 10.0)                   */
    float sustain;              /* level 0.0 - 1.0                          */
    float release;              /* seconds (0.001 - 10.0)                   */
} sq_adsr_params_t;

typedef struct {
    sq_env_stage_t stage;
    float level;                /* current envelope output (0.0 - 1.0)      */
    float rate;                 /* increment per sample for current stage    */
} sq_envelope_t;

/* LFO — low-frequency oscillator for modulation */
typedef enum {
    LFO_DEST_NONE,
    LFO_DEST_PITCH,
    LFO_DEST_FILTER,
    LFO_DEST_AMP
} sq_lfo_dest_t;

typedef struct {
    sq_waveform_t waveform;     /* LFO shape                                */
    float rate;                 /* Hz (0.1 - 50)                            */
    float depth;                /* modulation amount (0.0 - 1.0)            */
    sq_lfo_dest_t dest;         /* what parameter to modulate                */
    double phase;               /* current phase (0.0 - 1.0)                */
} sq_lfo_t;

/* Biquad filter state */
typedef enum {
    FILTER_LOWPASS,
    FILTER_HIGHPASS,
    FILTER_BANDPASS
} sq_filter_type_t;

typedef struct {
    sq_filter_type_t type;
    float cutoff;               /* Hz (20 - 20000)                          */
    float resonance;            /* Q factor (0.5 - 20.0)                    */
    /* biquad state variables (per-channel) */
    float z1[2], z2[2];        /* delay elements [0]=left, [1]=right       */
} sq_filter_t;

/* ─── Synthesis mode ─────────────────────────────────────────────────────── */

typedef enum {
    SYNTH_SUBTRACTIVE,          /* classic osc+filter                        */
    SYNTH_FM,                   /* 4-operator frequency modulation           */
    SYNTH_WAVETABLE             /* wavetable scanning                        */
} sq_synth_mode_t;

/* ─── FM synthesis types ─────────────────────────────────────────────────── */

#define FM_NUM_OPERATORS   4
#define FM_NUM_ALGORITHMS  8

/* One FM operator: sine oscillator with frequency ratio and envelope */
typedef struct {
    float freq_ratio;           /* frequency multiplier (0.5 - 16.0)        */
    float level;                /* output level (0.0 - 1.0)                 */
    float feedback;             /* self-feedback amount (0.0 - 1.0)         */
    sq_adsr_params_t env;       /* per-operator amplitude envelope           */
} sq_fm_operator_t;

/* FM algorithm: which operators modulate which.
 * Each operator has a list of sources (operators whose output modulates it).
 * Carriers (operators that contribute to audio output) are flagged.
 *
 * Algorithms (DX7-inspired):
 *   0: [4→3→2→1]    serial chain
 *   1: [3→2→1, 4→1] 3→2→1 + 4→1
 *   2: [4→3, 2→1]   two parallel pairs
 *   3: [4→3→2, 1]   3-chain + carrier
 *   4: [4, 3, 2→1]  two carriers + pair
 *   5: [4→3, 2, 1]  pair + two carriers
 *   6: [4, 3, 2, 1] all carriers (additive)
 *   7: [4→(2,3), 1] 4 mods both 2&3, 1 carrier
 */

/* Synth preset — all the knobs for one synth sound */
typedef struct {
    char name[32];
    sq_synth_mode_t synth_mode; /* subtractive, FM, or wavetable             */

    /* === Subtractive parameters === */
    sq_waveform_t osc1_wave;
    sq_waveform_t osc2_wave;
    float osc_mix;              /* 0.0 = all osc1, 1.0 = all osc2          */
    float osc2_detune;          /* semitones (-24 to +24)                   */
    int   unison_voices;        /* 1-7                                      */
    float unison_detune;        /* cents (0 - 50)                           */
    /* Filter */
    sq_filter_type_t filter_type;
    float filter_cutoff;        /* Hz (20 - 20000)                          */
    float filter_resonance;     /* Q (0.5 - 20.0)                          */
    float filter_env_depth;     /* how much amp envelope modulates cutoff   */
    /* Envelopes */
    sq_adsr_params_t amp_env;   /* amplitude envelope                       */
    sq_adsr_params_t filter_env;/* filter envelope                          */
    /* LFO */
    sq_lfo_t lfo;
    bool  lfo_bpm_sync;        /* sync LFO rate to BPM?                    */
    int   lfo_sync_division;   /* 0=1/1, 1=1/2, 2=1/4, 3=1/8, 4=1/16, 5=1/32 */

    /* === FM parameters === */
    int fm_algorithm;                        /* 0-7 algorithm index          */
    sq_fm_operator_t fm_ops[FM_NUM_OPERATORS]; /* 4 operators                */

    /* === Wavetable parameters === */
    int  wt_bank_index;         /* which wavetable bank (0 - SQ_WT_MAX_BANKS-1) */
    float wt_position;          /* scan position (0.0 - 1.0)                */
    float wt_env_depth;         /* envelope modulation of position (-1 to 1) */
    float wt_lfo_depth;         /* LFO modulation of position (0 to 1)      */
} sq_synth_preset_t;

/* One active synth voice */
typedef struct {
    bool active;
    int  preset_index;          /* which preset to use                      */
    float frequency;            /* base frequency in Hz                     */
    double osc1_phase;          /* oscillator 1 phase (0.0 - 1.0)          */
    double osc2_phase;          /* oscillator 2 phase (0.0 - 1.0)          */
    double unison_phases[7];    /* per-unison-voice phases (max 7)          */
    sq_envelope_t amp_env;      /* amplitude envelope state                 */
    sq_envelope_t filter_env;   /* filter envelope state                    */
    sq_filter_t   filter;       /* per-voice filter state                   */
    sq_lfo_t      lfo;          /* per-voice LFO state                      */
    float velocity;             /* 0.0 - 1.0                                */
    float volume;               /* track volume                             */
    float pan;                  /* track pan                                */
    float smoothed_cutoff;      /* smoothed filter cutoff (prevents clicks) */
    uint64_t start_time;
    /* FM per-voice state */
    double fm_phase[FM_NUM_OPERATORS];     /* operator phases               */
    sq_envelope_t fm_env[FM_NUM_OPERATORS]; /* operator envelopes           */
    float fm_feedback_state[FM_NUM_OPERATORS]; /* self-feedback memory      */
    /* Wavetable per-voice state */
    double wt_phase;            /* oscillator phase (0.0 - 1.0)             */
    float  wt_smoothed_pos;     /* smoothed wavetable position               */
} sq_synth_voice_t;

/* Wavetable storage — pre-computed waveforms (for subtractive oscillators) */
typedef struct {
    float tables[SQ_NUM_WAVEFORMS][SQ_WAVETABLE_SIZE];
    bool  initialized;
} sq_wavetables_t;

/* Wavetable bank — a set of single-cycle waveforms for scanning */
typedef struct {
    char  name[32];
    float frames[SQ_WT_MAX_FRAMES][SQ_WAVETABLE_SIZE]; /* waveform data   */
    int   num_frames;                                    /* actual count     */
} sq_wt_bank_t;

/* ─── SoundFont (SF2) support via TinySoundFont ──────────────────────────── */

#define SQ_MAX_SF2_PRESETS  128  /* max SF2 presets exposed */
#define SQ_SF2_NAME_LEN      32

typedef struct {
    char  name[SQ_SF2_NAME_LEN];
    int   tsf_preset_index;      /* index in the TSF instance */
    int   bank;
    int   preset_number;
} sq_sf2_preset_t;

/* ─── Streaming recorder ─────────────────────────────────────────────────── */

typedef enum {
    SQ_REC_IDLE = 0,        /* not recording                              */
    SQ_REC_ACTIVE,          /* streaming to disk                          */
    SQ_REC_ERROR             /* write failed (disk full, I/O error)       */
} sq_rec_state_t;

typedef struct {
    void           *wav;             /* open dr_wav streaming handle (drwav*) */
    sq_rec_state_t  state;           /* current recording state            */
    uint32_t        bit_depth;       /* 16, 24, or 32                     */
    uint32_t        sample_rate;     /* sample rate at recording start    */
    uint64_t        frames_written;  /* total frames written to disk      */
    char            filepath[512];   /* path to current recording file    */

    /* Disk space monitoring (updated periodically, not every callback) */
    uint64_t        disk_free_bytes; /* last-checked available bytes       */
    bool            disk_low;        /* true when < 500 MB free            */
    uint32_t        disk_check_countdown; /* frames until next disk check */

    /* Auto-increment state */
    int             next_number;     /* next file number (0 = needs scan) */
} sq_recorder_t;

/* ─── Engine: the top-level state container ───────────────────────────────── */

typedef struct tsf tsf; /* forward declare */

typedef struct {
    /* Loaded samples */
    sq_sample_t    samples[SQ_MAX_SAMPLES];
    uint32_t       num_samples;

    /* Patterns */
    sq_pattern_t   patterns[SQ_MAX_PATTERNS];
    uint32_t       num_patterns;

    /* Song arrangement */
    sq_arrangement_t arrangement;

    /* Transport (timing/playback) */
    sq_transport_t transport;

    /* Voice pool (polyphonic playback) */
    sq_voice_t     voices[SQ_MAX_VOICES];

    /* Synth */
    sq_synth_voice_t  synth_voices[SQ_MAX_SYNTH_VOICES];
    sq_synth_preset_t synth_presets[SQ_MAX_SYNTH_PRESETS];
    uint32_t          num_synth_presets;
    sq_wavetables_t   wavetables;
    sq_wt_bank_t     *wt_banks;   /* heap-allocated array [SQ_WT_MAX_BANKS] */
    uint32_t          num_wt_banks;

    /* SoundFont */
    tsf             *sf2;                               /* TSF instance (NULL if none loaded) */
    sq_sf2_preset_t  sf2_presets[SQ_MAX_SF2_PRESETS];
    uint32_t         num_sf2_presets;
    char             sf2_path[256];                     /* path to loaded .sf2 file */

    /* Master effects */
    sq_effect_slot_t master_effects[MAX_TRACK_EFFECTS];

    /* Output settings */
    uint32_t       sample_rate;     /* output sample rate (e.g., 44100)      */
    float          master_volume;   /* master output level, 0.0 to 1.0      */

    /* Simple PRNG state for humanization (xorshift32) */
    uint32_t       rng_state;

    /* Streaming recorder (writes to disk in real-time) */
    sq_recorder_t  recorder;

    /* Peak level metering (computed by mixer, read by GUI) */
    float          track_peaks[SQ_MAX_TRACKS]; /* per-track peak (0.0 - 1.0+) */
    float          master_peak[2];             /* master L/R peak             */

    /* Lock-free command queue: GUI pushes, audio thread pops */
    sq_command_queue_t cmd_queue;

    /* Base directory (where exe lives) for resolving output paths */
    char base_dir[512];
} sq_engine_t;

/* ─── Engine API ──────────────────────────────────────────────────────────── */

/*
 * Initialize the engine with the given output sample rate.
 * Sets defaults: 120 BPM, master volume 1.0, one empty pattern.
 */
void sq_engine_init(sq_engine_t *engine, uint32_t sample_rate);

/*
 * Shut down the engine and free all loaded sample data.
 */
void sq_engine_shutdown(sq_engine_t *engine);

/*
 * Process audio: advance the transport, trigger voices from the sequencer,
 * render active voices, mix to the output buffer.
 *
 * output: interleaved stereo float buffer (L,R,L,R,...), must hold num_frames*2 floats.
 * num_frames: how many stereo frames to produce.
 *
 * This is the ONE function called by both the standalone audio callback
 * and the plugin process callback. It must be real-time safe:
 * no malloc, no printf, no file I/O, no locks.
 */
void sq_engine_process(sq_engine_t *engine, float *output, uint32_t num_frames);

/* ─── Streaming recorder API ──────────────────────────────────────────────── */

/*
 * Start streaming recording to a WAV file on disk.
 * filepath: full path to the output .wav file.
 * bit_depth: 16, 24, or 32.
 * Call from the GUI thread before playback or during playback.
 * Returns 0 on success, -1 on failure.
 */
int sq_recorder_start(sq_recorder_t *rec, const char *filepath,
                      uint32_t sample_rate, uint32_t bit_depth);

/*
 * Write audio frames to the open recording file.
 * Called from the audio callback — must be fast.
 * output: interleaved stereo float buffer.
 * num_frames: number of stereo frames.
 */
void sq_recorder_write(sq_recorder_t *rec, const float *output,
                       uint32_t num_frames);

/*
 * Stop recording and finalize the WAV file.
 * Safe to call even if not recording (no-op).
 */
void sq_recorder_stop(sq_recorder_t *rec);

/*
 * Scan output_dir for files matching "{prefix}_NNN.wav" and return
 * the next available filename in out_path (must be at least 512 bytes).
 * Returns the number used (e.g., 7 for prefix_007.wav).
 */
int sq_recorder_next_filename(const char *output_dir, const char *prefix,
                              int last_known, char *out_path, size_t out_path_size);

/*
 * Get available disk space in bytes for the given path.
 * Returns 0 on error.
 */
uint64_t sq_recorder_disk_free(const char *path);

/*
 * Safe project load — stops playback and recording before modifying engine state.
 * Returns 0 on success, -1 on failure.
 */
int sq_engine_safe_load(sq_engine_t *engine, const char *filepath);

#ifdef __cplusplus
}
#endif

#endif /* SQ_ENGINE_H */
