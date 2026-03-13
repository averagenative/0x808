/*
 * synth_editor.c — Synth parameter editor panel.
 *
 * Displays controls for the currently selected synth preset.
 * Switches between subtractive and FM mode layouts.
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/synth_editor.h"
#include "engine/engine.h"

#define LOG_TAG "synth_ed"
#include "core/log.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* Waveform names for display */
static const char *wave_names[] = {"Saw", "Square", "Triangle", "Sine"};
static const char *filter_names[] = {"LowPass", "HiPass", "BandPass"};
static const char *lfo_dest_names[] = {"None", "Pitch", "Filter", "Amp"};
static const char *lfo_sync_names[] = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};
static const char *synth_mode_names[] = {"Subtractive", "FM", "Wavetable"};
static const char *fm_alg_names[] = {
    "0: 3>2>1>0",
    "1: 2>1>0+3>0",
    "2: 3>2, 1>0",
    "3: 3>2>1, 0",
    "4: 3,2,1>0",
    "5: 3>2,1,0",
    "6: 3,2,1,0",
    "7: 3>(1,2),0"
};

/* ─── TASK-200: ADSR envelope visualization ─────────────────────────────── */

static void draw_adsr_curve(struct nk_context *ctx, const sq_adsr_params_t *env)
{
    nk_layout_row_dynamic(ctx, 60, 1);
    struct nk_rect bounds = nk_widget_bounds(ctx);
    nk_spacing(ctx, 1);
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

    /* Background */
    nk_fill_rect(canvas, bounds, 2, nk_rgba(25, 25, 30, 255));

    /* Calculate phase widths */
    float total_time = env->attack + env->decay + 0.3f + env->release;
    if (total_time < 0.01f) total_time = 0.01f;
    float w = bounds.w - 4;
    float h = bounds.h - 4;
    float x0 = bounds.x + 2;
    float y0 = bounds.y + 2;
    float bottom = y0 + h;

    float ax = x0 + (env->attack / total_time) * w;
    float dx = ax + (env->decay / total_time) * w;
    float sx = dx + (0.3f / total_time) * w;
    float rx = sx + (env->release / total_time) * w;
    if (rx > x0 + w) rx = x0 + w;

    float sus_y = bottom - env->sustain * h;

    struct nk_color line_col = nk_rgba(80, 180, 255, 200);

    /* Attack: (x0, bottom) -> (ax, top) */
    nk_stroke_line(canvas, x0, bottom, ax, y0, 1.5f, line_col);
    /* Decay: (ax, top) -> (dx, sus_y) */
    nk_stroke_line(canvas, ax, y0, dx, sus_y, 1.5f, line_col);
    /* Sustain: (dx, sus_y) -> (sx, sus_y) */
    nk_stroke_line(canvas, dx, sus_y, sx, sus_y, 1.5f, line_col);
    /* Release: (sx, sus_y) -> (rx, bottom) */
    nk_stroke_line(canvas, sx, sus_y, rx, bottom, 1.5f, line_col);

    /* Phase boundary dots */
    struct nk_color dot = nk_rgba(255, 255, 255, 150);
    nk_fill_circle(canvas, nk_rect(ax - 2, y0 - 2, 4, 4), dot);
    nk_fill_circle(canvas, nk_rect(dx - 2, sus_y - 2, 4, 4), dot);
    nk_fill_circle(canvas, nk_rect(sx - 2, sus_y - 2, 4, 4), dot);

    /* Labels */
    nk_draw_text(canvas, nk_rect(x0, bottom - 12, 10, 12), "A", 1,
                 ctx->style.font, nk_rgba(0,0,0,0), nk_rgba(150,150,150,200));
    nk_draw_text(canvas, nk_rect(ax, bottom - 12, 10, 12), "D", 1,
                 ctx->style.font, nk_rgba(0,0,0,0), nk_rgba(150,150,150,200));
    nk_draw_text(canvas, nk_rect(dx, bottom - 12, 10, 12), "S", 1,
                 ctx->style.font, nk_rgba(0,0,0,0), nk_rgba(150,150,150,200));
    nk_draw_text(canvas, nk_rect(sx, bottom - 12, 10, 12), "R", 1,
                 ctx->style.font, nk_rgba(0,0,0,0), nk_rgba(150,150,150,200));
}

/* ─── TASK-201: Filter frequency response curve ────────────────────────── */

static void draw_filter_curve(struct nk_context *ctx, sq_filter_type_t type,
                               float cutoff, float resonance)
{
    nk_layout_row_dynamic(ctx, 50, 1);
    struct nk_rect bounds = nk_widget_bounds(ctx);
    nk_spacing(ctx, 1);
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

    nk_fill_rect(canvas, bounds, 2, nk_rgba(25, 25, 30, 255));

    float w = bounds.w;
    float h = bounds.h;
    struct nk_color line_col = nk_rgba(220, 120, 80, 200);
    float prev_y = 0;
    for (int px = 0; px < (int)w; px++) {
        float t = (float)px / w;
        float freq = 20.0f * powf(1000.0f, t); /* 20 to 20000 Hz */

        float ratio = freq / cutoff;
        float mag;
        float Q = resonance;
        if (Q < 0.5f) Q = 0.5f;

        float denom = sqrtf(1.0f + powf(ratio, 4.0f)
                      - 2.0f * ratio * ratio * (1.0f - 1.0f / (2.0f * Q * Q)));
        if (denom < 0.001f) denom = 0.001f;

        switch (type) {
        case FILTER_LOWPASS:
            mag = 1.0f / denom;
            break;
        case FILTER_HIGHPASS:
            mag = (ratio * ratio) / denom;
            break;
        default: /* bandpass */
            mag = (ratio / Q) / denom;
            break;
        }
        if (mag > 2.0f) mag = 2.0f;
        if (mag < 0.0f) mag = 0.0f;

        float y = bounds.y + h - (mag / 2.0f) * h;
        if (px > 0) {
            nk_stroke_line(canvas, bounds.x + px - 1, prev_y,
                           bounds.x + px, y, 1.5f, line_col);
        }
        prev_y = y;
    }

    /* Cutoff frequency indicator line */
    float cutoff_x = bounds.x + w * logf(cutoff / 20.0f) / logf(1000.0f);
    if (cutoff_x > bounds.x && cutoff_x < bounds.x + w)
        nk_stroke_line(canvas, cutoff_x, bounds.y, cutoff_x, bounds.y + h,
                       1.0f, nk_rgba(255, 255, 255, 60));
}

/* ─── TASK-203: FM algorithm diagram ────────────────────────────────────── */

/* FM algorithm routing — must match fm_algorithms[] in synth.c */
static const struct {
    int  mod_sources[FM_NUM_OPERATORS][FM_NUM_OPERATORS]; /* -1 terminated */
    bool is_carrier[FM_NUM_OPERATORS];
} fm_alg_vis[FM_NUM_ALGORITHMS] = {
    /* 0: 3->2->1->0 */
    {{{1,-1},{2,-1},{3,-1},{-1}},  {true,false,false,false}},
    /* 1: 2->1->0, 3->0 */
    {{{1,3,-1},{2,-1},{-1},{-1}},  {true,false,false,false}},
    /* 2: 3->2, 1->0 */
    {{{1,-1},{-1},{3,-1},{-1}},    {true,false,true,false}},
    /* 3: 3->2->1, 0 */
    {{{-1},{2,-1},{3,-1},{-1}},    {true,true,false,false}},
    /* 4: 3,2, 1->0 */
    {{{1,-1},{-1},{-1},{-1}},      {true,false,true,true}},
    /* 5: 3->2, 1, 0 */
    {{{-1},{-1},{3,-1},{-1}},      {true,true,true,false}},
    /* 6: all carriers */
    {{{-1},{-1},{-1},{-1}},        {true,true,true,true}},
    /* 7: 3->(1,2), 0 */
    {{{-1},{3,-1},{3,-1},{-1}},    {true,true,true,false}},
};

static void draw_fm_algorithm(struct nk_context *ctx, int algorithm)
{
    nk_layout_row_dynamic(ctx, 70, 1);
    struct nk_rect bounds = nk_widget_bounds(ctx);
    nk_spacing(ctx, 1);
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

    nk_fill_rect(canvas, bounds, 2, nk_rgba(25, 25, 30, 255));

    if (algorithm < 0 || algorithm >= FM_NUM_ALGORITHMS) algorithm = 0;

    float bw = 28, bh = 20;
    float cx = bounds.x + bounds.w * 0.5f;
    float cy = bounds.y + bounds.h * 0.5f;

    /* Operator positions — lay out based on algorithm topology */
    struct { float x, y; } ops[4];
    float top_y = cy - 18;
    float bot_y = cy + 12;

    switch (algorithm) {
    case 0: /* serial: 3->2->1->0 */
        ops[3] = (typeof(ops[0])){cx - 54, top_y};
        ops[2] = (typeof(ops[0])){cx - 18, top_y};
        ops[1] = (typeof(ops[0])){cx + 18, top_y};
        ops[0] = (typeof(ops[0])){cx + 54, bot_y};
        break;
    case 1: /* 2->1->0, 3->0 */
        ops[2] = (typeof(ops[0])){cx - 36, top_y};
        ops[1] = (typeof(ops[0])){cx, top_y};
        ops[3] = (typeof(ops[0])){cx + 36, top_y};
        ops[0] = (typeof(ops[0])){cx, bot_y};
        break;
    case 2: /* two pairs: 3->2, 1->0 */
        ops[1] = (typeof(ops[0])){cx - 30, top_y};
        ops[0] = (typeof(ops[0])){cx - 30, bot_y};
        ops[3] = (typeof(ops[0])){cx + 30, top_y};
        ops[2] = (typeof(ops[0])){cx + 30, bot_y};
        break;
    case 3: /* 3->2->1, 0 */
        ops[3] = (typeof(ops[0])){cx - 36, top_y};
        ops[2] = (typeof(ops[0])){cx, top_y};
        ops[1] = (typeof(ops[0])){cx + 36, bot_y};
        ops[0] = (typeof(ops[0])){cx - 54, bot_y};
        break;
    case 4: /* 3,2, 1->0 */
        ops[1] = (typeof(ops[0])){cx - 18, top_y};
        ops[0] = (typeof(ops[0])){cx - 18, bot_y};
        ops[2] = (typeof(ops[0])){cx + 18, bot_y};
        ops[3] = (typeof(ops[0])){cx + 54, bot_y};
        break;
    case 5: /* 3->2, 1, 0 */
        ops[3] = (typeof(ops[0])){cx - 18, top_y};
        ops[2] = (typeof(ops[0])){cx - 18, bot_y};
        ops[1] = (typeof(ops[0])){cx + 18, bot_y};
        ops[0] = (typeof(ops[0])){cx + 54, bot_y};
        break;
    case 6: /* all carriers */
        ops[0] = (typeof(ops[0])){cx - 54, bot_y};
        ops[1] = (typeof(ops[0])){cx - 18, bot_y};
        ops[2] = (typeof(ops[0])){cx + 18, bot_y};
        ops[3] = (typeof(ops[0])){cx + 54, bot_y};
        break;
    case 7: /* 3->(1,2), 0 */
        ops[3] = (typeof(ops[0])){cx, top_y};
        ops[1] = (typeof(ops[0])){cx - 24, bot_y};
        ops[2] = (typeof(ops[0])){cx + 24, bot_y};
        ops[0] = (typeof(ops[0])){cx - 60, bot_y};
        break;
    default:
        for (int i = 0; i < 4; i++)
            ops[i] = (typeof(ops[0])){cx - 54 + i * 36, cy};
        break;
    }

    /* Draw modulation arrows */
    struct nk_color arrow_col = nk_rgba(200, 200, 100, 160);
    for (int op = 0; op < FM_NUM_OPERATORS; op++) {
        for (int j = 0; j < FM_NUM_OPERATORS; j++) {
            int src = fm_alg_vis[algorithm].mod_sources[op][j];
            if (src < 0) break;
            /* Arrow from src to op */
            nk_stroke_line(canvas, ops[src].x, ops[src].y + bh / 2,
                           ops[op].x, ops[op].y - bh / 2,
                           1.5f, arrow_col);
            /* Arrowhead */
            float dx = ops[op].x - ops[src].x;
            float dy = (ops[op].y - bh / 2) - (ops[src].y + bh / 2);
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.01f) {
                dx /= len; dy /= len;
                float ax = ops[op].x - dx * 5 - dy * 3;
                float ay = ops[op].y - bh / 2 - dy * 5 + dx * 3;
                float bx = ops[op].x - dx * 5 + dy * 3;
                float by = ops[op].y - bh / 2 - dy * 5 - dx * 3;
                nk_stroke_line(canvas, ops[op].x, ops[op].y - bh / 2,
                               ax, ay, 1.5f, arrow_col);
                nk_stroke_line(canvas, ops[op].x, ops[op].y - bh / 2,
                               bx, by, 1.5f, arrow_col);
            }
        }
    }

    /* Draw operator boxes */
    for (int i = 0; i < 4; i++) {
        struct nk_color bg = fm_alg_vis[algorithm].is_carrier[i]
            ? nk_rgba(60, 140, 80, 200) : nk_rgba(80, 100, 160, 200);
        nk_fill_rect(canvas, nk_rect(ops[i].x - bw / 2, ops[i].y - bh / 2,
                                      bw, bh), 3, bg);
        char label[4];
        snprintf(label, sizeof(label), "%d", i + 1);
        nk_draw_text(canvas, nk_rect(ops[i].x - 4, ops[i].y - 6, 12, 12),
                     label, 1, ctx->style.font, nk_rgba(0,0,0,0),
                     nk_rgba(255, 255, 255, 220));
    }

    /* Algorithm label */
    char alg_label[16];
    snprintf(alg_label, sizeof(alg_label), "Alg %d", algorithm + 1);
    nk_draw_text(canvas, nk_rect(bounds.x + 4, bounds.y + 2, 40, 12),
                 alg_label, (int)strlen(alg_label), ctx->style.font,
                 nk_rgba(0,0,0,0), nk_rgba(150, 150, 150, 180));
}

/* ─── TASK-204: Wavetable position visualizer ───────────────────────────── */

static void draw_wt_waveform(struct nk_context *ctx, const sq_engine_t *engine,
                              int bank_idx, float position)
{
    nk_layout_row_dynamic(ctx, 50, 1);
    struct nk_rect bounds = nk_widget_bounds(ctx);
    nk_spacing(ctx, 1);
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

    nk_fill_rect(canvas, bounds, 2, nk_rgba(25, 25, 30, 255));

    if (!engine->wt_banks || bank_idx < 0 ||
        (uint32_t)bank_idx >= engine->num_wt_banks) return;
    const sq_wt_bank_t *bank = &engine->wt_banks[bank_idx];
    if (bank->num_frames < 1) return;

    /* Get frame index from position */
    float fpos = position * (bank->num_frames - 1);
    int frame = (int)fpos;
    if (frame >= bank->num_frames) frame = bank->num_frames - 1;
    if (frame < 0) frame = 0;

    /* Draw waveform */
    float w = bounds.w;
    float h = bounds.h;
    float cy = bounds.y + h * 0.5f;
    struct nk_color wave_col = nk_rgba(120, 200, 180, 200);

    float prev_y = cy;
    for (int px = 0; px < (int)w; px++) {
        int idx = (int)((float)px / w * SQ_WAVETABLE_SIZE);
        if (idx >= SQ_WAVETABLE_SIZE) idx = SQ_WAVETABLE_SIZE - 1;
        float sample = bank->frames[frame][idx];
        float y = cy - sample * (h * 0.45f);
        if (px > 0) {
            nk_stroke_line(canvas, bounds.x + px - 1, prev_y,
                           bounds.x + px, y, 1.0f, wave_col);
        }
        prev_y = y;
    }

    /* Position indicator bar */
    float pos_x = bounds.x + position * w;
    nk_stroke_line(canvas, pos_x, bounds.y, pos_x, bounds.y + h,
                   1.0f, nk_rgba(255, 200, 80, 120));

    /* Center line */
    nk_stroke_line(canvas, bounds.x, cy, bounds.x + w, cy,
                   0.5f, nk_rgba(60, 60, 65, 150));
}

/* ─── Draw subtractive synth controls ────────────────────────────────────── */

static void draw_subtractive(struct nk_context *ctx, sq_synth_preset_t *p,
                             float h)
{
    /* Four columns: Oscillators | Filter | Amp Envelope | LFO */
    nk_layout_row_dynamic(ctx, h - 80, 4);

    /* ── Column 1: Oscillators ──────────────────────────────────── */
    if (nk_group_begin(ctx, "Osc", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_label(ctx, "Osc1:", NK_TEXT_LEFT);
        int w1 = (int)p->osc1_wave;
        nk_combobox(ctx, wave_names, 4, &w1, 18, nk_vec2(100, 80));
        p->osc1_wave = (sq_waveform_t)w1;

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_label(ctx, "Osc2:", NK_TEXT_LEFT);
        int w2 = (int)p->osc2_wave;
        nk_combobox(ctx, wave_names, 4, &w2, 18, nk_vec2(100, 80));
        p->osc2_wave = (sq_waveform_t)w2;

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_label(ctx, "Mix:", NK_TEXT_LEFT);
        nk_slider_float(ctx, 0.0f, &p->osc_mix, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Detune: %.1f", p->osc2_detune);
        nk_slider_float(ctx, -24.0f, &p->osc2_detune, 24.0f, 0.1f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Unison: %d", p->unison_voices);
        {
            int uv = p->unison_voices;
            nk_slider_int(ctx, 1, &uv, 7, 1);
            p->unison_voices = uv;
        }

        if (p->unison_voices > 1) {
            nk_layout_row_dynamic(ctx, 18, 2);
            nk_labelf(ctx, NK_TEXT_LEFT, "Spread: %.0fc", p->unison_detune);
            nk_slider_float(ctx, 0.0f, &p->unison_detune, 50.0f, 1.0f);
        }

        nk_group_end(ctx);
    }

    /* ── Column 2: Filter ───────────────────────────────────────── */
    if (nk_group_begin(ctx, "Filter", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_label(ctx, "Type:", NK_TEXT_LEFT);
        int ft = (int)p->filter_type;
        nk_combobox(ctx, filter_names, 3, &ft, 18, nk_vec2(100, 60));
        p->filter_type = (sq_filter_type_t)ft;

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Cutoff: %.0f", p->filter_cutoff);
        nk_slider_float(ctx, 20.0f, &p->filter_cutoff, 20000.0f, 10.0f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Reso: %.1f", p->filter_resonance);
        nk_slider_float(ctx, 0.5f, &p->filter_resonance, 20.0f, 0.1f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "EnvDep: %.0f", p->filter_env_depth);
        nk_slider_float(ctx, -10000.0f, &p->filter_env_depth, 10000.0f, 50.0f);

        /* Filter envelope ADSR */
        nk_layout_row_dynamic(ctx, 14, 1);
        nk_label(ctx, "Filter Env:", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "A: %.3f", p->filter_env.attack);
        nk_slider_float(ctx, 0.001f, &p->filter_env.attack, 2.0f, 0.001f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "D: %.3f", p->filter_env.decay);
        nk_slider_float(ctx, 0.001f, &p->filter_env.decay, 2.0f, 0.001f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "S: %.2f", p->filter_env.sustain);
        nk_slider_float(ctx, 0.0f, &p->filter_env.sustain, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "R: %.3f", p->filter_env.release);
        nk_slider_float(ctx, 0.001f, &p->filter_env.release, 5.0f, 0.001f);

        /* TASK-201: Filter frequency response curve */
        draw_filter_curve(ctx, p->filter_type, p->filter_cutoff,
                          p->filter_resonance);

        /* TASK-200: Filter envelope visualization */
        draw_adsr_curve(ctx, &p->filter_env);

        nk_group_end(ctx);
    }

    /* ── Column 3: Amp Envelope ─────────────────────────────────── */
    if (nk_group_begin(ctx, "Amp Env", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "A: %.3f", p->amp_env.attack);
        nk_slider_float(ctx, 0.001f, &p->amp_env.attack, 2.0f, 0.001f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "D: %.3f", p->amp_env.decay);
        nk_slider_float(ctx, 0.001f, &p->amp_env.decay, 2.0f, 0.001f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "S: %.2f", p->amp_env.sustain);
        nk_slider_float(ctx, 0.0f, &p->amp_env.sustain, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "R: %.3f", p->amp_env.release);
        nk_slider_float(ctx, 0.001f, &p->amp_env.release, 5.0f, 0.001f);

        /* TASK-200: Amp envelope visualization */
        draw_adsr_curve(ctx, &p->amp_env);

        nk_group_end(ctx);
    }

    /* ── Column 4: LFO ──────────────────────────────────────────── */
    if (nk_group_begin(ctx, "LFO", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_label(ctx, "Wave:", NK_TEXT_LEFT);
        int lw = (int)p->lfo.waveform;
        nk_combobox(ctx, wave_names, 4, &lw, 18, nk_vec2(100, 80));
        p->lfo.waveform = (sq_waveform_t)lw;

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_label(ctx, "Dest:", NK_TEXT_LEFT);
        int ld = (int)p->lfo.dest;
        nk_combobox(ctx, lfo_dest_names, 4, &ld, 18, nk_vec2(100, 80));
        p->lfo.dest = (sq_lfo_dest_t)ld;

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Rate: %.1f", p->lfo.rate);
        nk_slider_float(ctx, 0.0f, &p->lfo.rate, 50.0f, 0.1f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Depth: %.2f", p->lfo.depth);
        nk_slider_float(ctx, 0.0f, &p->lfo.depth, 1.0f, 0.01f);

        /* BPM sync */
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_label(ctx, "BPM Sync:", NK_TEXT_LEFT);
        {
            int sync = p->lfo_bpm_sync ? 1 : 0;
            nk_checkbox_label(ctx, "", &sync);
            p->lfo_bpm_sync = (sync != 0);
        }

        if (p->lfo_bpm_sync) {
            nk_layout_row_dynamic(ctx, 18, 2);
            nk_label(ctx, "Div:", NK_TEXT_LEFT);
            int sd = p->lfo_sync_division;
            nk_combobox(ctx, lfo_sync_names, 6, &sd, 18, nk_vec2(80, 120));
            p->lfo_sync_division = sd;
        }

        nk_group_end(ctx);
    }
}

/* ─── Draw one FM operator ───────────────────────────────────────────────── */

static void draw_fm_operator(struct nk_context *ctx, sq_fm_operator_t *op,
                             int op_index)
{
    char grp_name[16];
    snprintf(grp_name, sizeof(grp_name), "Op %d", op_index + 1);

    if (nk_group_begin(ctx, grp_name, NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Ratio: %.2f", op->freq_ratio);
        nk_slider_float(ctx, 0.5f, &op->freq_ratio, 16.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Level: %.2f", op->level);
        nk_slider_float(ctx, 0.0f, &op->level, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "FB: %.2f", op->feedback);
        nk_slider_float(ctx, 0.0f, &op->feedback, 1.0f, 0.01f);

        /* Operator envelope */
        nk_layout_row_dynamic(ctx, 14, 1);
        nk_label(ctx, "Envelope:", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "A: %.3f", op->env.attack);
        nk_slider_float(ctx, 0.001f, &op->env.attack, 5.0f, 0.001f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "D: %.3f", op->env.decay);
        nk_slider_float(ctx, 0.001f, &op->env.decay, 5.0f, 0.001f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "S: %.2f", op->env.sustain);
        nk_slider_float(ctx, 0.0f, &op->env.sustain, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "R: %.3f", op->env.release);
        nk_slider_float(ctx, 0.001f, &op->env.release, 5.0f, 0.001f);

        nk_group_end(ctx);
    }
}

/* ─── Draw FM synth controls ─────────────────────────────────────────────── */

static void draw_fm(struct nk_context *ctx, sq_synth_preset_t *p, float h)
{
    /* Algorithm selector + Amp envelope on first row */
    nk_layout_row_dynamic(ctx, 22, 3);
    nk_label(ctx, "Algorithm:", NK_TEXT_LEFT);
    {
        int alg = p->fm_algorithm;
        nk_combobox(ctx, fm_alg_names, FM_NUM_ALGORITHMS, &alg, 18,
                    nk_vec2(160, 160));
        p->fm_algorithm = alg;
    }
    nk_labelf(ctx, NK_TEXT_LEFT, "Amp: A%.3f D%.3f S%.2f R%.3f",
              p->amp_env.attack, p->amp_env.decay,
              p->amp_env.sustain, p->amp_env.release);

    /* Voice amp envelope controls */
    nk_layout_row_dynamic(ctx, 18, 8);
    nk_label(ctx, "Amp A:", NK_TEXT_RIGHT);
    nk_slider_float(ctx, 0.001f, &p->amp_env.attack, 5.0f, 0.001f);
    nk_label(ctx, "D:", NK_TEXT_RIGHT);
    nk_slider_float(ctx, 0.001f, &p->amp_env.decay, 5.0f, 0.001f);
    nk_label(ctx, "S:", NK_TEXT_RIGHT);
    nk_slider_float(ctx, 0.0f, &p->amp_env.sustain, 1.0f, 0.01f);
    nk_label(ctx, "R:", NK_TEXT_RIGHT);
    nk_slider_float(ctx, 0.001f, &p->amp_env.release, 5.0f, 0.001f);

    /* TASK-200: Amp envelope visualization */
    draw_adsr_curve(ctx, &p->amp_env);

    /* TASK-203: FM algorithm diagram */
    draw_fm_algorithm(ctx, p->fm_algorithm);

    /* Four operator columns */
    nk_layout_row_dynamic(ctx, h - 120, FM_NUM_OPERATORS);
    for (int op = 0; op < FM_NUM_OPERATORS; op++) {
        draw_fm_operator(ctx, &p->fm_ops[op], op);
    }
}

/* ─── Draw wavetable synth controls ──────────────────────────────────────── */

static void draw_wavetable(struct nk_context *ctx, sq_engine_t *engine,
                           sq_synth_preset_t *p, float h)
{
    nk_layout_row_dynamic(ctx, h - 80, 3);

    /* Column 1: Wavetable selection + position */
    if (nk_group_begin(ctx, "Wavetable", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        /* Bank selector */
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_label(ctx, "Bank:", NK_TEXT_LEFT);
        if (engine->wt_banks && engine->num_wt_banks > 0) {
            /* Build bank names list */
            static const char *bank_names[SQ_WT_MAX_BANKS];
            for (uint32_t b = 0; b < engine->num_wt_banks && b < SQ_WT_MAX_BANKS; b++) {
                bank_names[b] = engine->wt_banks[b].name;
            }
            int bi = p->wt_bank_index;
            nk_combobox(ctx, bank_names, (int)engine->num_wt_banks, &bi, 18,
                        nk_vec2(120, 80));
            p->wt_bank_index = bi;
        } else {
            nk_label(ctx, "(none)", NK_TEXT_LEFT);
        }

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Position: %.2f", p->wt_position);
        nk_slider_float(ctx, 0.0f, &p->wt_position, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Env Mod: %.2f", p->wt_env_depth);
        nk_slider_float(ctx, -1.0f, &p->wt_env_depth, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "LFO Mod: %.2f", p->wt_lfo_depth);
        nk_slider_float(ctx, 0.0f, &p->wt_lfo_depth, 1.0f, 0.01f);

        /* LFO controls */
        nk_layout_row_dynamic(ctx, 14, 1);
        nk_label(ctx, "LFO:", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_label(ctx, "Wave:", NK_TEXT_LEFT);
        int lw = (int)p->lfo.waveform;
        nk_combobox(ctx, wave_names, 4, &lw, 18, nk_vec2(100, 80));
        p->lfo.waveform = (sq_waveform_t)lw;

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Rate: %.1f", p->lfo.rate);
        nk_slider_float(ctx, 0.0f, &p->lfo.rate, 50.0f, 0.1f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Depth: %.2f", p->lfo.depth);
        nk_slider_float(ctx, 0.0f, &p->lfo.depth, 1.0f, 0.01f);

        /* TASK-204: Wavetable waveform visualizer */
        draw_wt_waveform(ctx, engine, p->wt_bank_index, p->wt_position);

        nk_group_end(ctx);
    }

    /* Column 2: Amp Envelope */
    if (nk_group_begin(ctx, "WT Amp Env", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "A: %.3f", p->amp_env.attack);
        nk_slider_float(ctx, 0.001f, &p->amp_env.attack, 5.0f, 0.001f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "D: %.3f", p->amp_env.decay);
        nk_slider_float(ctx, 0.001f, &p->amp_env.decay, 5.0f, 0.001f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "S: %.2f", p->amp_env.sustain);
        nk_slider_float(ctx, 0.0f, &p->amp_env.sustain, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "R: %.3f", p->amp_env.release);
        nk_slider_float(ctx, 0.001f, &p->amp_env.release, 5.0f, 0.001f);

        /* TASK-200: Amp envelope visualization */
        draw_adsr_curve(ctx, &p->amp_env);

        nk_group_end(ctx);
    }

    /* Column 3: Filter Envelope (modulates WT position) */
    if (nk_group_begin(ctx, "WT Mod Env", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 14, 1);
        nk_label(ctx, "Position Envelope:", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "A: %.3f", p->filter_env.attack);
        nk_slider_float(ctx, 0.001f, &p->filter_env.attack, 5.0f, 0.001f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "D: %.3f", p->filter_env.decay);
        nk_slider_float(ctx, 0.001f, &p->filter_env.decay, 5.0f, 0.001f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "S: %.2f", p->filter_env.sustain);
        nk_slider_float(ctx, 0.0f, &p->filter_env.sustain, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "R: %.3f", p->filter_env.release);
        nk_slider_float(ctx, 0.001f, &p->filter_env.release, 5.0f, 0.001f);

        /* TASK-200: Position envelope visualization */
        draw_adsr_curve(ctx, &p->filter_env);

        nk_group_end(ctx);
    }
}

/* ─── Main synth editor entry point ──────────────────────────────────────── */

int synth_editor_draw(struct nk_context *ctx, sq_engine_t *engine,
                      int *preset_index_ptr,
                      float x, float y, float w, float h)
{
    int preset_index = preset_index_ptr ? *preset_index_ptr : -1;
    if (preset_index < 0 || (uint32_t)preset_index >= engine->num_synth_presets)
        return 0;

    sq_synth_preset_t *p = &engine->synth_presets[preset_index];

    if (nk_begin(ctx, "SynthEditor",
                 nk_rect(x, y, w, h),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE))
    {
        /* Header: preset selector + mode selector */
        nk_layout_row_dynamic(ctx, 22, 4);

        /* Preset dropdown */
        nk_label(ctx, "Preset:", NK_TEXT_LEFT);
        {
            /* Build label for current preset */
            char current_label[64];
            snprintf(current_label, sizeof(current_label), "%s", p->name);
            float combo_h = (float)engine->num_synth_presets * 22.0f;
            if (combo_h > 300.0f) combo_h = 300.0f;
            if (nk_combo_begin_label(ctx, current_label, nk_vec2(200, combo_h))) {
                nk_layout_row_dynamic(ctx, 20, 1);
                for (uint32_t i = 0; i < engine->num_synth_presets; i++) {
                    if (nk_combo_item_label(ctx, engine->synth_presets[i].name, NK_TEXT_LEFT)) {
                        *preset_index_ptr = (int)i;
                        preset_index = (int)i;
                        p = &engine->synth_presets[preset_index];
                    }
                }
                nk_combo_end(ctx);
            }
        }

        nk_label(ctx, "Mode:", NK_TEXT_RIGHT);
        {
            int mode = (int)p->synth_mode;
            nk_combobox(ctx, synth_mode_names, 3, &mode, 18, nk_vec2(120, 60));
            p->synth_mode = (sq_synth_mode_t)mode;
        }

        /* Draw mode-specific controls */
        switch (p->synth_mode) {
        case SYNTH_SUBTRACTIVE:
            draw_subtractive(ctx, p, h);
            break;
        case SYNTH_FM:
            draw_fm(ctx, p, h);
            break;
        case SYNTH_WAVETABLE:
            draw_wavetable(ctx, engine, p, h);
            break;
        }
    }
    nk_end(ctx);

    return 1;
}
