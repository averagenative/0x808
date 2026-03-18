/*
 * sq_app.c — Shared application controller (C99).
 *
 * Non-rendering app logic: keyboard shortcuts, panel state, playhead,
 * status messages, undo coordination. Used by all frontends.
 */

#include "app/sq_app.h"
#include "gui/undo.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define STEPS_PER_BEAT 4

/* ─── Init ────────────────────────────────────────────────────────────────── */

void sq_app_init(sq_app_t *app)
{
    memset(app, 0, sizeof(*app));
    app->selected_track = -1;
    sq_app_init_rec_config(&app->rec_config);

    /* Audio defaults */
    app->audio_config.device_name[0] = '\0'; /* empty = default */
    app->audio_config.sample_rate = 44100;
    app->audio_config.device_index = -1;

    /* MIDI defaults */
    app->midi_device_name[0] = '\0';
    app->midi_port_index = -1;

    /* UI preferences */
    app->show_tooltips = true;
}

void sq_app_init_rec_config(sq_rec_config_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    /* Platform-appropriate default directory */
#ifdef _WIN32
    const char *userprofile = getenv("USERPROFILE");
    if (userprofile)
        snprintf(cfg->output_dir, SQ_REC_DIR_LEN, "%s\\Music\\0x808", userprofile);
    else
        snprintf(cfg->output_dir, SQ_REC_DIR_LEN, ".");
#else
    const char *home = getenv("HOME");
    if (home)
        snprintf(cfg->output_dir, SQ_REC_DIR_LEN, "%s/Music/0x808", home);
    else
        snprintf(cfg->output_dir, SQ_REC_DIR_LEN, ".");
#endif

    snprintf(cfg->prefix, SQ_REC_PREFIX_LEN, "recording");
    cfg->bit_depth = 16;
}

/* ─── Key handler ─────────────────────────────────────────────────────────── */

sq_app_action_t sq_app_handle_key(sq_app_t *app, sq_engine_t *engine,
                                   int key, int mod, bool down)
{
    if (!down) return SQ_ACTION_NONE;

    bool ctrl  = (mod & SQ_MOD_CTRL) != 0;
    bool shift = (mod & SQ_MOD_SHIFT) != 0;

    /* Transport: space always works */
    if (key == SQ_KEY_SPACE) {
        bool was_playing = engine->transport.playing;
        engine->transport.playing = !engine->transport.playing;
        engine->transport.current_beat = 0.0;
        engine->transport.sample_position = 0;
        engine->transport.current_step = 0;
        app->visual_step = 0;
        if (was_playing && !engine->transport.playing) {
            /* Stop: kill all voices immediately */
            for (int v = 0; v < SQ_MAX_VOICES; v++)
                engine->voices[v].active = false;
            for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++)
                engine->synth_voices[v].active = false;
        } else if (!was_playing && engine->transport.playing) {
            /* Play: trigger step 0 immediately so first beat isn't missed */
            engine->transport.step0_pending = true;
        }
        /* Caller must set play_start_ticks if playing started */
        return SQ_ACTION_NONE;
    }

    /* Escape */
    if (key == SQ_KEY_ESCAPE)
        return SQ_ACTION_QUIT;

    /* Pattern select 1-9 (no modifiers) */
    if (!ctrl && key >= SQ_KEY_1 && key <= SQ_KEY_9) {
        int pat = key - SQ_KEY_1;
        if ((uint32_t)pat < engine->num_patterns) {
            engine->transport.current_pattern = pat;
            sq_app_set_status(app, NULL, 90);
            snprintf(app->status_msg, SQ_STATUS_LEN, "Pattern %d", pat + 1);
        }
        return SQ_ACTION_NONE;
    }

    /* Ctrl shortcuts */
    if (ctrl) {
        switch (key) {
        case SQ_KEY_S:
            return SQ_ACTION_SAVE;

        case SQ_KEY_O:
            return SQ_ACTION_LOAD;

        case SQ_KEY_T:
            return SQ_ACTION_TAP_TEMPO;

        case SQ_KEY_G:
            return SQ_ACTION_TOGGLE_THEME;

        case SQ_KEY_C: {
            int pi = engine->transport.current_pattern;
            if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                app->clipboard = engine->patterns[pi];
                app->clipboard_valid = true;
                snprintf(app->status_msg, SQ_STATUS_LEN,
                         "Copied pattern %d", pi + 1);
                app->status_timer = 90;
            }
            return SQ_ACTION_NONE;
        }

        case SQ_KEY_V:
            if (app->clipboard_valid) {
                int pi = engine->transport.current_pattern;
                if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                    engine->patterns[pi] = app->clipboard;
                    snprintf(app->status_msg, SQ_STATUS_LEN,
                             "Pasted to pattern %d", pi + 1);
                    app->status_timer = 90;
                }
            }
            return SQ_ACTION_NONE;

        case SQ_KEY_Z:
            if (shift) {
                if (undo_redo(engine))
                    sq_app_set_status(app, "Redo", 90);
            } else {
                if (undo_undo(engine))
                    sq_app_set_status(app, "Undo", 90);
            }
            return SQ_ACTION_NONE;

        default:
            break;
        }
    }

    /* Add new pattern (+/= key, no ctrl) */
    if (key == SQ_KEY_EQUALS && !ctrl) {
        if (engine->num_patterns < SQ_MAX_PATTERNS) {
            int ni = (int)engine->num_patterns;
            engine->num_patterns++;
            sq_app_init_new_pattern(engine, ni);
            engine->transport.current_pattern = ni;
            snprintf(app->status_msg, SQ_STATUS_LEN, "New pattern %d", ni + 1);
            app->status_timer = 90;
        }
        return SQ_ACTION_NONE;
    }

    return SQ_ACTION_NONE;
}

/* ─── Panel toggling ──────────────────────────────────────────────────────── */

void sq_app_toggle_panel(sq_app_t *app, sq_panel_t panel)
{
    if (panel >= SQ_PANEL_COUNT) return;
    app->panels[panel] = !app->panels[panel];
}

/* ─── Playhead ────────────────────────────────────────────────────────────── */

void sq_app_update_playhead(sq_app_t *app, sq_engine_t *engine,
                            uint64_t now, uint64_t freq)
{
    if (engine->transport.playing) {
        double elapsed = (double)(now - app->play_start_ticks) / (double)freq;
        double beats = elapsed * (engine->transport.bpm / 60.0);
        int pat_len = 16;
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
            /* Use longest track length for visual playhead (polymeter) */
            for (uint32_t t = 0; t < engine->patterns[pi].num_tracks; t++) {
                int tl = (int)engine->patterns[pi].tracks[t].length;
                if (tl > pat_len) pat_len = tl;
            }
        }
        app->visual_step = ((int)floor(beats * STEPS_PER_BEAT)) % pat_len;
    } else if (app->was_playing && !engine->transport.playing) {
        app->visual_step = 0;
    }
    app->was_playing = engine->transport.playing;
}

/* ─── Status messages ─────────────────────────────────────────────────────── */

void sq_app_set_status(sq_app_t *app, const char *msg, uint32_t frames)
{
    if (msg)
        snprintf(app->status_msg, SQ_STATUS_LEN, "%s", msg);
    app->status_timer = frames;
}

void sq_app_update_status(sq_app_t *app)
{
    if (app->status_timer > 0) {
        app->status_timer--;
        if (app->status_timer == 0)
            app->status_msg[0] = '\0';
    }
}

double sq_app_tap_tempo(sq_app_t *app, uint64_t now_us)
{
    /* Reset if gap > 2 seconds since last tap */
    if (app->tap_count > 0 &&
        (now_us - app->tap_times[(app->tap_count - 1) % 4]) > 2000000) {
        app->tap_count = 0;
    }

    /* Record this tap */
    app->tap_times[app->tap_count % 4] = now_us;
    app->tap_count++;

    /* Need at least 2 taps to compute interval */
    if (app->tap_count < 2) return 0.0;

    /* Average interval from available taps (up to 4) */
    int n = app->tap_count;
    if (n > 4) n = 4;
    uint64_t first = app->tap_times[(app->tap_count - n) % 4];
    uint64_t last  = app->tap_times[(app->tap_count - 1) % 4];
    double avg_interval_sec = (double)(last - first) / (double)(n - 1) / 1000000.0;

    if (avg_interval_sec <= 0.0) return 0.0;

    double bpm = 60.0 / avg_interval_sec;

    /* Clamp to valid range */
    if (bpm < 20.0)  bpm = 20.0;
    if (bpm > 300.0) bpm = 300.0;

    return bpm;
}

/* ─── Keyboard preset lookup ──────────────────────────────────────────────── */

int sq_app_get_keyboard_preset(const sq_app_t *app, const sq_engine_t *engine)
{
    /* Check selected track first */
    if (app->selected_track >= 0) {
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
            const sq_pattern_t *pat = &engine->patterns[pi];
            if ((uint32_t)app->selected_track < pat->num_tracks &&
                pat->tracks[app->selected_track].type == TRACK_SYNTH)
                return pat->tracks[app->selected_track].synth_preset;
        }
    }
    /* Fall back to first synth track */
    int pi = engine->transport.current_pattern;
    if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
        const sq_pattern_t *pat = &engine->patterns[pi];
        for (uint32_t t = 0; t < pat->num_tracks; t++) {
            if (pat->tracks[t].type == TRACK_SYNTH)
                return pat->tracks[t].synth_preset;
        }
    }
    return -1;
}

/* ─── New pattern initialization ──────────────────────────────────────────── */

void sq_app_init_new_pattern(sq_engine_t *engine, int idx)
{
    sq_pattern_t *np  = &engine->patterns[idx];
    sq_pattern_t *src = &engine->patterns[0];
    memset(np, 0, sizeof(*np));
    snprintf(np->name, SQ_PATTERN_NAME_LEN, "Pattern %d", idx + 1);
    np->num_tracks = src->num_tracks;
    for (uint32_t t = 0; t < np->num_tracks; t++) {
        np->tracks[t].type         = src->tracks[t].type;
        np->tracks[t].length       = src->tracks[t].length;
        np->tracks[t].volume       = src->tracks[t].volume;
        np->tracks[t].pan          = src->tracks[t].pan;
        np->tracks[t].sample_index = src->tracks[t].sample_index;
        np->tracks[t].synth_preset = src->tracks[t].synth_preset;
        np->tracks[t].sf2_preset   = src->tracks[t].sf2_preset;
        np->tracks[t].color_index  = src->tracks[t].color_index;
    }
}
