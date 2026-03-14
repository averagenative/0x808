/*
 * command_queue.h — Lock-free SPSC command queue for GUI→audio thread communication.
 *
 * The GUI thread pushes commands; the audio thread polls and applies them.
 * This eliminates direct GUI→engine writes during playback.
 *
 * Uses a single-producer single-consumer ring buffer with atomic indices.
 */

#ifndef SQ_COMMAND_QUEUE_H
#define SQ_COMMAND_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef __cplusplus
#include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CMD_QUEUE_SIZE 256  /* must be power of 2 */

typedef enum {
    CMD_NONE = 0,
    CMD_PLAY,
    CMD_STOP,
    CMD_SET_BPM,
    CMD_SET_VOLUME,
    CMD_SET_SWING,
    CMD_SET_STEP,          /* toggle step on/off */
    CMD_SET_PATTERN,       /* switch current pattern */
    CMD_SET_MODE,          /* pattern/song/perform */
    CMD_QUEUE_SECTION,     /* queue section for perform mode */
    CMD_TRIGGER_NOTE,      /* virtual keyboard note on */
    CMD_RELEASE_NOTE,      /* virtual keyboard note off */
    CMD_SET_TRACK_VOLUME,
    CMD_SET_TRACK_PAN,
    CMD_SET_TRACK_MUTE,
    CMD_SET_TRACK_SOLO,
} sq_cmd_type_t;

typedef struct {
    sq_cmd_type_t type;
    union {
        double   f64_val;     /* for BPM */
        float    f32_val;     /* for volume, swing, pan */
        int      int_val;     /* for pattern index, mode, section */
        bool     bool_val;    /* for mute, solo */
        struct {              /* for step toggle */
            uint16_t track;
            uint16_t step;
            uint8_t  velocity;
            int8_t   pitch;
        } step;
        struct {              /* for note trigger */
            int   preset;
            float frequency;
            float velocity;
        } note;
        struct {              /* for track params */
            uint16_t track;
            float    value;
        } track_param;
    };
} sq_command_t;

typedef struct {
    sq_command_t commands[CMD_QUEUE_SIZE];
#ifdef __cplusplus
    volatile unsigned int write_idx;   /* GUI thread writes here */
    volatile unsigned int read_idx;    /* audio thread reads here */
#else
    atomic_uint  write_idx;   /* GUI thread writes here */
    atomic_uint  read_idx;    /* audio thread reads here */
#endif
} sq_command_queue_t;

/* Initialize queue (call once at startup) */
void cmd_queue_init(sq_command_queue_t *q);

/* Push a command (GUI thread). Returns false if queue is full. */
bool cmd_queue_push(sq_command_queue_t *q, const sq_command_t *cmd);

/* Pop a command (audio thread). Returns false if queue is empty. */
bool cmd_queue_pop(sq_command_queue_t *q, sq_command_t *cmd);

/* Convenience push helpers */
static inline void cmd_play(sq_command_queue_t *q) {
    sq_command_t c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_PLAY;
    cmd_queue_push(q, &c);
}
static inline void cmd_stop(sq_command_queue_t *q) {
    sq_command_t c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_STOP;
    cmd_queue_push(q, &c);
}
static inline void cmd_set_bpm(sq_command_queue_t *q, double bpm) {
    sq_command_t c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_SET_BPM;
    c.f64_val = bpm;
    cmd_queue_push(q, &c);
}
static inline void cmd_set_volume(sq_command_queue_t *q, float vol) {
    sq_command_t c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_SET_VOLUME;
    c.f32_val = vol;
    cmd_queue_push(q, &c);
}
static inline void cmd_set_step(sq_command_queue_t *q, uint16_t track, uint16_t step, uint8_t vel, int8_t pitch) {
    sq_command_t c;
    memset(&c, 0, sizeof(c));
    c.type = CMD_SET_STEP;
    c.step.track = track;
    c.step.step = step;
    c.step.velocity = vel;
    c.step.pitch = pitch;
    cmd_queue_push(q, &c);
}

#ifdef __cplusplus
}
#endif

#endif /* SQ_COMMAND_QUEUE_H */
