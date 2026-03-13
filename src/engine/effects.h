/*
 * effects.h — Audio effects: biquad filter, delay, reverb.
 *
 * Effects can be used as per-track inserts or on the master bus.
 */

#ifndef SQ_EFFECTS_H
#define SQ_EFFECTS_H

#include <stdint.h>
#include <stdbool.h>

/* ─── Effect types ───────────────────────────────────────────────────────── */

typedef enum {
    EFFECT_NONE,
    EFFECT_FILTER,
    EFFECT_DELAY,
    EFFECT_REVERB,
    EFFECT_TYPE_COUNT
} sq_effect_type_t;

/* ─── Biquad filter effect ───────────────────────────────────────────────── */

typedef enum {
    EFX_FILTER_LP,
    EFX_FILTER_HP,
    EFX_FILTER_BP
} sq_efx_filter_mode_t;

typedef struct {
    sq_efx_filter_mode_t mode;
    float cutoff;           /* Hz (20-20000) */
    float resonance;        /* Q (0.5-20) */
    float wet;              /* wet/dry mix (0-1) */
    /* State */
    float z1[2], z2[2];    /* biquad delay elements [L,R] */
    float b0, b1, b2, a1, a2; /* computed coefficients */
    float last_cutoff;      /* for detecting coefficient changes */
    float last_resonance;
} sq_efx_filter_t;

/* ─── Delay effect ───────────────────────────────────────────────────────── */

#define DELAY_MAX_SAMPLES (48000 * 2)  /* 2 seconds at 48kHz */

typedef struct {
    float time;             /* delay time in seconds (0.001-2.0) */
    float feedback;         /* feedback amount (0-0.95) */
    float wet;              /* wet/dry mix (0-1) */
    bool  bpm_sync;         /* sync delay time to BPM */
    int   sync_division;    /* 0=1/1, 1=1/2, 2=1/4, 3=1/8 */
    /* State */
    float *buffer;          /* heap-allocated stereo circular buffer */
    int   write_pos;
    int   buffer_size;      /* actual size in stereo frames */
    bool  allocated;
} sq_efx_delay_t;

/* ─── Reverb (Freeverb-style) ────────────────────────────────────────────── */

#define REVERB_NUM_COMBS    8
#define REVERB_NUM_ALLPASS  4
#define REVERB_COMB_MAX     2048
#define REVERB_ALLPASS_MAX  1024

typedef struct {
    float buffer[REVERB_COMB_MAX];
    int   size;
    int   idx;
    float filterstore;
} reverb_comb_t;

typedef struct {
    float buffer[REVERB_ALLPASS_MAX];
    int   size;
    int   idx;
} reverb_allpass_t;

typedef struct {
    float room_size;        /* 0.0-1.0 */
    float damping;          /* 0.0-1.0 */
    float wet;              /* wet/dry mix (0-1) */
    /* State — heap allocated on first use */
    reverb_comb_t    *combs;     /* [2][REVERB_NUM_COMBS] */
    reverb_allpass_t *allpasses; /* [2][REVERB_NUM_ALLPASS] */
    bool initialized;
} sq_efx_reverb_t;

/* ─── Generic effect slot ────────────────────────────────────────────────── */

#define MAX_TRACK_EFFECTS 3

typedef struct {
    sq_effect_type_t type;
    bool bypass;
    union {
        sq_efx_filter_t filter;
        sq_efx_delay_t  delay;
        sq_efx_reverb_t reverb;
    };
} sq_effect_slot_t;

/* ─── API ────────────────────────────────────────────────────────────────── */

/* Free heap-allocated buffers inside an effect slot (delay buffer, reverb
   comb/allpass arrays).  Resets type to EFFECT_NONE.  Safe to call on an
   already-empty or zeroed slot. */
void effect_free(sq_effect_slot_t *slot);

/* Initialize effect to default state (calls effect_free first to avoid leaks) */
void effect_init(sq_effect_slot_t *slot, sq_effect_type_t type, uint32_t sample_rate);

/* Process a stereo buffer through an effect (in-place) */
void effect_process(sq_effect_slot_t *slot, float *buffer, uint32_t num_frames,
                    uint32_t sample_rate, double bpm);

/* Process a chain of effects */
void effects_chain_process(sq_effect_slot_t *slots, int num_slots,
                           float *buffer, uint32_t num_frames,
                           uint32_t sample_rate, double bpm);

#endif /* SQ_EFFECTS_H */
