/*
 * synth_editor.cpp -- Synth parameter editor panel (Dear ImGui port).
 *
 * Displays controls for the currently selected synth preset.
 * Switches between subtractive, FM, and wavetable mode layouts.
 */

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <math.h>

extern "C" {
#include "engine/engine.h"
#include "engine/sq_midi.h"
#define LOG_TAG "synth_ed"
#include "core/log.h"
#include "gui/knobs.h"
#include "gui/gui.h"
struct sq_midi *gui_get_midi(void); /* defined in gui.cpp */
}

/* ─── MIDI learn helper ──────────────────────────────────────────────────── */

static void midi_learn_check(sq_param_id_t param)
{
    struct sq_midi *midi = gui_get_midi();
    if (!midi) return;

    /* Tooltip for MIDI learn */
    if (g_tooltips_enabled && ImGui::IsItemHovered())
        ImGui::SetTooltip("Right-click to MIDI learn\n(applies to all presets globally)");

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (sq_midi_learn_active(midi) == param)
            sq_midi_learn_cancel(midi);
        else
            sq_midi_learn_start(midi, param);
    }

    if (sq_midi_learn_active(midi) == param) {
        float t = (float)ImGui::GetTime();
        float pulse = 0.5f + 0.5f * sinf(t * 6.0f);
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(
            ImVec2(mn.x - 2, mn.y - 2), ImVec2(mx.x + 2, mx.y + 2),
            IM_COL32(255, 220, 40, (int)(pulse * 255)), 4.0f, 0, 2.0f);
    }
}

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

/* --- ADSR envelope visualization with interactive drag -------------------- */

/* Track which ADSR point is being dragged: 0=none, 1=A, 2=D, 3=S */
static int  s_adsr_drag_point = 0;
/* Unique ID for the envelope being dragged (address cast to uintptr_t) */
static uintptr_t s_adsr_drag_id = 0;

static void draw_adsr_curve(sq_adsr_params_t *env, float width, float height)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    uintptr_t env_id = (uintptr_t)env;

    /* Reserve space (Dummy — no ImGui interaction, we do manual hit testing) */
    ImGui::Dummy(ImVec2(width, height));

    /* Background */
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                      IM_COL32(35, 38, 45, 255), 2.0f);

    /* Calculate phase widths */
    float total_time = env->attack + env->decay + 0.3f + env->release;
    if (total_time < 0.01f) total_time = 0.01f;
    float w = width - 4;
    float h = height - 4;
    float x0 = pos.x + 2;
    float y0 = pos.y + 2;
    float bottom = y0 + h;

    float ax = x0 + (env->attack / total_time) * w;
    float dx = ax + (env->decay / total_time) * w;
    float sx = dx + (0.3f / total_time) * w;
    float rx = sx + (env->release / total_time) * w;
    if (rx > x0 + w) rx = x0 + w;

    float sus_y = bottom - env->sustain * h;

    /* --- Manual mouse interaction (no InvisibleButton — avoids focus issues) --- */
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    bool mouse_in_rect = (mouse.x >= pos.x && mouse.x <= pos.x + width &&
                          mouse.y >= pos.y && mouse.y <= pos.y + height);
    float grab_radius = 10.0f;

    /* Click to grab nearest control point */
    if (mouse_in_rect && io.MouseClicked[0] && ImGui::IsWindowHovered()) {
        float best_dist = grab_radius * grab_radius;
        int best_pt = 0;

        float d;
        d = (mouse.x - ax) * (mouse.x - ax) + (mouse.y - y0) * (mouse.y - y0);
        if (d < best_dist) { best_dist = d; best_pt = 1; }
        d = (mouse.x - dx) * (mouse.x - dx) + (mouse.y - sus_y) * (mouse.y - sus_y);
        if (d < best_dist) { best_dist = d; best_pt = 2; }
        d = (mouse.x - sx) * (mouse.x - sx) + (mouse.y - sus_y) * (mouse.y - sus_y);
        if (d < best_dist) { best_dist = d; best_pt = 3; }

        if (best_pt > 0) {
            s_adsr_drag_point = best_pt;
            s_adsr_drag_id = env_id;
        }
    }

    /* Dragging (continues even if mouse leaves the rect) */
    if (io.MouseDown[0] && s_adsr_drag_id == env_id && s_adsr_drag_point > 0) {
        float rel_x = (mouse.x - x0) / w;
        float rel_y = (mouse.y - y0) / h;
        if (rel_x < 0.0f) rel_x = 0.0f;
        if (rel_x > 1.0f) rel_x = 1.0f;
        if (rel_y < 0.0f) rel_y = 0.0f;
        if (rel_y > 1.0f) rel_y = 1.0f;

        float time_at_x = rel_x * total_time;

        if (s_adsr_drag_point == 1) {
            /* Attack point — drag X to change attack time */
            env->attack = time_at_x;
            if (env->attack < 0.001f) env->attack = 0.001f;
            if (env->attack > 5.0f) env->attack = 5.0f;
        } else if (s_adsr_drag_point == 2) {
            /* Decay point — drag X for decay time, Y for sustain level */
            env->decay = time_at_x - env->attack;
            if (env->decay < 0.001f) env->decay = 0.001f;
            if (env->decay > 5.0f) env->decay = 5.0f;
            env->sustain = 1.0f - rel_y;
            if (env->sustain < 0.0f) env->sustain = 0.0f;
            if (env->sustain > 1.0f) env->sustain = 1.0f;
        } else if (s_adsr_drag_point == 3) {
            /* Sustain/Release — drag Y for sustain, X for release */
            env->sustain = 1.0f - rel_y;
            if (env->sustain < 0.0f) env->sustain = 0.0f;
            if (env->sustain > 1.0f) env->sustain = 1.0f;
            float remaining = total_time - time_at_x;
            if (remaining < 0.001f) remaining = 0.001f;
            if (remaining > 5.0f) remaining = 5.0f;
            env->release = remaining;
        }
    }

    /* Release drag on mouse up */
    if (!io.MouseDown[0] && s_adsr_drag_id == env_id) {
        s_adsr_drag_point = 0;
        s_adsr_drag_id = 0;
    }

    /* Recalculate after possible drag changes */
    total_time = env->attack + env->decay + 0.3f + env->release;
    if (total_time < 0.01f) total_time = 0.01f;
    ax = x0 + (env->attack / total_time) * w;
    dx = ax + (env->decay / total_time) * w;
    sx = dx + (0.3f / total_time) * w;
    rx = sx + (env->release / total_time) * w;
    if (rx > x0 + w) rx = x0 + w;
    sus_y = bottom - env->sustain * h;

    ImU32 line_col = IM_COL32(80, 180, 255, 200);

    /* Attack: (x0, bottom) -> (ax, top) */
    dl->AddLine(ImVec2(x0, bottom), ImVec2(ax, y0), line_col, 1.5f);
    /* Decay: (ax, top) -> (dx, sus_y) */
    dl->AddLine(ImVec2(ax, y0), ImVec2(dx, sus_y), line_col, 1.5f);
    /* Sustain: (dx, sus_y) -> (sx, sus_y) */
    dl->AddLine(ImVec2(dx, sus_y), ImVec2(sx, sus_y), line_col, 1.5f);
    /* Release: (sx, sus_y) -> (rx, bottom) */
    dl->AddLine(ImVec2(sx, sus_y), ImVec2(rx, bottom), line_col, 1.5f);

    /* Phase boundary dots — highlight active drag point */
    ImU32 dot_col = IM_COL32(255, 255, 255, 150);
    ImU32 dot_active = IM_COL32(255, 220, 80, 255);
    float dot_r = 3.5f;
    bool dragging_this = (s_adsr_drag_id == env_id);

    dl->AddCircleFilled(ImVec2(ax, y0), dot_r,
        (dragging_this && s_adsr_drag_point == 1) ? dot_active : dot_col);
    dl->AddCircleFilled(ImVec2(dx, sus_y), dot_r,
        (dragging_this && s_adsr_drag_point == 2) ? dot_active : dot_col);
    dl->AddCircleFilled(ImVec2(sx, sus_y), dot_r,
        (dragging_this && s_adsr_drag_point == 3) ? dot_active : dot_col);

    /* Labels */
    ImU32 label_col = IM_COL32(150, 150, 150, 200);
    dl->AddText(ImVec2(x0, bottom - 12), label_col, "A");
    dl->AddText(ImVec2(ax, bottom - 12), label_col, "D");
    dl->AddText(ImVec2(dx, bottom - 12), label_col, "S");
    dl->AddText(ImVec2(sx, bottom - 12), label_col, "R");
}

/* --- Filter frequency response curve -------------------------------------- */

static void draw_filter_curve(sq_filter_type_t type, float cutoff,
                               float resonance, float width, float height)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Reserve space */
    ImGui::Dummy(ImVec2(width, height));

    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                      IM_COL32(35, 38, 45, 255), 2.0f);

    float w = width;
    float h = height;
    ImU32 line_col = IM_COL32(220, 120, 80, 200);
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

        float y = pos.y + h - (mag / 2.0f) * h;
        if (px > 0) {
            dl->AddLine(ImVec2(pos.x + px - 1, prev_y),
                        ImVec2(pos.x + px, y), line_col, 1.5f);
        }
        prev_y = y;
    }

    /* Cutoff frequency indicator line */
    float cutoff_x = pos.x + w * logf(cutoff / 20.0f) / logf(1000.0f);
    if (cutoff_x > pos.x && cutoff_x < pos.x + w)
        dl->AddLine(ImVec2(cutoff_x, pos.y), ImVec2(cutoff_x, pos.y + h),
                    IM_COL32(255, 255, 255, 60), 1.0f);
}

/* --- FM algorithm diagram ------------------------------------------------- */

/* FM algorithm routing -- must match fm_algorithms[] in synth.c */
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

static void draw_fm_algorithm(int algorithm, float width, float height)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Reserve space */
    ImGui::Dummy(ImVec2(width, height));

    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                      IM_COL32(35, 38, 45, 255), 2.0f);

    if (algorithm < 0 || algorithm >= FM_NUM_ALGORITHMS) algorithm = 0;

    float bw = 28, bh = 20;
    float cx = pos.x + width * 0.5f;
    float cy = pos.y + height * 0.5f;

    /* Operator positions */
    struct { float x, y; } ops[4];
    float top_y = cy - 18;
    float bot_y = cy + 12;

    switch (algorithm) {
    case 0:
        ops[3] = {cx - 54, top_y};
        ops[2] = {cx - 18, top_y};
        ops[1] = {cx + 18, top_y};
        ops[0] = {cx + 54, bot_y};
        break;
    case 1:
        ops[2] = {cx - 36, top_y};
        ops[1] = {cx, top_y};
        ops[3] = {cx + 36, top_y};
        ops[0] = {cx, bot_y};
        break;
    case 2:
        ops[1] = {cx - 30, top_y};
        ops[0] = {cx - 30, bot_y};
        ops[3] = {cx + 30, top_y};
        ops[2] = {cx + 30, bot_y};
        break;
    case 3:
        ops[3] = {cx - 36, top_y};
        ops[2] = {cx, top_y};
        ops[1] = {cx + 36, bot_y};
        ops[0] = {cx - 54, bot_y};
        break;
    case 4:
        ops[1] = {cx - 18, top_y};
        ops[0] = {cx - 18, bot_y};
        ops[2] = {cx + 18, bot_y};
        ops[3] = {cx + 54, bot_y};
        break;
    case 5:
        ops[3] = {cx - 18, top_y};
        ops[2] = {cx - 18, bot_y};
        ops[1] = {cx + 18, bot_y};
        ops[0] = {cx + 54, bot_y};
        break;
    case 6:
        ops[0] = {cx - 54, bot_y};
        ops[1] = {cx - 18, bot_y};
        ops[2] = {cx + 18, bot_y};
        ops[3] = {cx + 54, bot_y};
        break;
    case 7:
        ops[3] = {cx, top_y};
        ops[1] = {cx - 24, bot_y};
        ops[2] = {cx + 24, bot_y};
        ops[0] = {cx - 60, bot_y};
        break;
    default:
        for (int i = 0; i < 4; i++)
            ops[i] = {cx - 54.0f + i * 36.0f, cy};
        break;
    }

    /* Draw modulation arrows */
    ImU32 arrow_col = IM_COL32(200, 200, 100, 160);
    for (int op = 0; op < FM_NUM_OPERATORS; op++) {
        for (int j = 0; j < FM_NUM_OPERATORS; j++) {
            int src = fm_alg_vis[algorithm].mod_sources[op][j];
            if (src < 0) break;
            /* Arrow from src to op */
            dl->AddLine(ImVec2(ops[src].x, ops[src].y + bh / 2),
                        ImVec2(ops[op].x, ops[op].y - bh / 2),
                        arrow_col, 1.5f);
            /* Arrowhead */
            float ddx = ops[op].x - ops[src].x;
            float ddy = (ops[op].y - bh / 2) - (ops[src].y + bh / 2);
            float len = sqrtf(ddx * ddx + ddy * ddy);
            if (len > 0.01f) {
                ddx /= len; ddy /= len;
                float ax2 = ops[op].x - ddx * 5 - ddy * 3;
                float ay2 = ops[op].y - bh / 2 - ddy * 5 + ddx * 3;
                float bx2 = ops[op].x - ddx * 5 + ddy * 3;
                float by2 = ops[op].y - bh / 2 - ddy * 5 - ddx * 3;
                dl->AddLine(ImVec2(ops[op].x, ops[op].y - bh / 2),
                            ImVec2(ax2, ay2), arrow_col, 1.5f);
                dl->AddLine(ImVec2(ops[op].x, ops[op].y - bh / 2),
                            ImVec2(bx2, by2), arrow_col, 1.5f);
            }
        }
    }

    /* Draw operator boxes */
    for (int i = 0; i < 4; i++) {
        ImU32 bg = fm_alg_vis[algorithm].is_carrier[i]
            ? IM_COL32(60, 140, 80, 200) : IM_COL32(80, 100, 160, 200);
        dl->AddRectFilled(
            ImVec2(ops[i].x - bw / 2, ops[i].y - bh / 2),
            ImVec2(ops[i].x + bw / 2, ops[i].y + bh / 2),
            bg, 3.0f);
        char label[4];
        snprintf(label, sizeof(label), "%d", i + 1);
        dl->AddText(ImVec2(ops[i].x - 4, ops[i].y - 6),
                    IM_COL32(255, 255, 255, 220), label);
    }

    /* Algorithm label */
    char alg_label[16];
    snprintf(alg_label, sizeof(alg_label), "Alg %d", algorithm + 1);
    dl->AddText(ImVec2(pos.x + 4, pos.y + 2),
                IM_COL32(150, 150, 150, 180), alg_label);
}

/* --- Wavetable position visualizer ---------------------------------------- */

static void draw_wt_waveform(const sq_engine_t *engine, int bank_idx,
                              float position, float width, float height)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* Reserve space */
    ImGui::Dummy(ImVec2(width, height));

    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                      IM_COL32(35, 38, 45, 255), 2.0f);

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
    float w = width;
    float h = height;
    float cy_wave = pos.y + h * 0.5f;
    ImU32 wave_col = IM_COL32(120, 200, 180, 200);

    float prev_y = cy_wave;
    for (int px = 0; px < (int)w; px++) {
        int idx = (int)((float)px / w * SQ_WAVETABLE_SIZE);
        if (idx >= SQ_WAVETABLE_SIZE) idx = SQ_WAVETABLE_SIZE - 1;
        float sample = bank->frames[frame][idx];
        float y = cy_wave - sample * (h * 0.45f);
        if (px > 0) {
            dl->AddLine(ImVec2(pos.x + px - 1, prev_y),
                        ImVec2(pos.x + px, y), wave_col, 1.0f);
        }
        prev_y = y;
    }

    /* Position indicator bar */
    float pos_x = pos.x + position * w;
    dl->AddLine(ImVec2(pos_x, pos.y), ImVec2(pos_x, pos.y + h),
                IM_COL32(255, 200, 80, 120), 1.0f);

    /* Center line */
    dl->AddLine(ImVec2(pos.x, cy_wave), ImVec2(pos.x + w, cy_wave),
                IM_COL32(60, 60, 65, 150), 0.5f);
}

/* --- Helper: combo with scroll wheel cycling ------------------------------ */

static bool combo_with_scroll(const char *label, int *current, const char **items,
                               int count)
{
    if (count <= 0) return false;
    bool changed = false;
    int old_val = *current;

    /* Clamp input to valid range */
    if (*current < 0) *current = 0;
    if (*current >= count) *current = count - 1;

    if (ImGui::Combo(label, current, items, count)) {
        changed = true;
    }
    /* Only process scroll when hovered AND no popup is open (avoids
       changing value while ImGui's combo popup is still rendering).
       Consume the wheel event so it doesn't bleed to parent panels. */
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
        ImGui::GetIO().MouseWheel != 0)
    {
        int delta = (ImGui::GetIO().MouseWheel > 0) ? -1 : 1;
        *current += delta;
        if (*current < 0) *current = count - 1;
        if (*current >= count) *current = 0;
        changed = true;
        /* Consume the scroll event to prevent parent window scrolling
           and other widgets on the same line from also reacting */
        ImGui::GetIO().MouseWheel = 0;
    }
    /* Final bounds clamp */
    if (*current < 0) *current = 0;
    if (*current >= count) *current = count - 1;
    if (!changed && *current != old_val) changed = true;
    return changed;
}

/* --- Draw subtractive synth controls -------------------------------------- */

static void draw_subtractive(sq_synth_preset_t *p, float panel_h)
{
    float col_h = panel_h - 80;
    if (col_h < 100) col_h = 100;

    /* Four columns: Oscillators | Filter | Amp Envelope | LFO */
    ImGui::Columns(4, "sub_cols", true);

    /* -- Column 1: Oscillators -- */
    ImGui::BeginChild("Osc", ImVec2(0, col_h), true);
    ImGui::Text("Oscillators");
    ImGui::Separator();

    int w1 = (int)p->osc1_wave;
    combo_with_scroll("Osc1", &w1, wave_names, 4);
    p->osc1_wave = (sq_waveform_t)w1;

    int w2 = (int)p->osc2_wave;
    combo_with_scroll("Osc2", &w2, wave_names, 4);
    p->osc2_wave = (sq_waveform_t)w2;

    knob_float("Mix", &p->osc_mix, 0.0f, 1.0f, 0.5f, 0.01f);
    ImGui::SameLine();
    knob_float("Det", &p->osc2_detune, -24.0f, 24.0f, 0.0f, 0.1f);

    int uv = p->unison_voices;
    ImGui::SliderInt("Unison", &uv, 1, 7);
    p->unison_voices = uv;

    if (p->unison_voices > 1) {
        knob_float("Spread", &p->unison_detune, 0.0f, 50.0f, 0.0f, 0.5f);
    }

    ImGui::EndChild();
    ImGui::NextColumn();

    /* -- Column 2: Filter -- */
    ImGui::BeginChild("Filter", ImVec2(0, col_h), true);
    ImGui::Text("Filter");
    ImGui::Separator();

    int ft = (int)p->filter_type;
    combo_with_scroll("Type", &ft, filter_names, 3);
    p->filter_type = (sq_filter_type_t)ft;

    knob_float("Cut", &p->filter_cutoff, 20.0f, 20000.0f, 1000.0f, 10.0f);
    midi_learn_check(SQ_PARAM_FILTER_CUTOFF);
    ImGui::SameLine();
    knob_float("Res", &p->filter_resonance, 0.5f, 20.0f, 1.0f, 0.1f);
    midi_learn_check(SQ_PARAM_FILTER_RESONANCE);
    knob_float("Env", &p->filter_env_depth, -10000.0f, 10000.0f, 0.0f, 50.0f);

    ImGui::Text("Filter Env:");
    knob_float("A##fe", &p->filter_env.attack, 0.001f, 2.0f, 0.005f, 0.001f);
    ImGui::SameLine();
    knob_float("D##fe", &p->filter_env.decay, 0.001f, 2.0f, 0.3f, 0.001f);
    knob_float("S##fe", &p->filter_env.sustain, 0.0f, 1.0f, 0.5f, 0.01f);
    ImGui::SameLine();
    knob_float("R##fe", &p->filter_env.release, 0.001f, 5.0f, 0.5f, 0.001f);

    /* Filter frequency response curve */
    float avail_w = ImGui::GetContentRegionAvail().x;
    draw_filter_curve(p->filter_type, p->filter_cutoff, p->filter_resonance,
                      avail_w, 50);

    /* Filter envelope visualization */
    draw_adsr_curve(&p->filter_env, avail_w, 60);

    ImGui::EndChild();
    ImGui::NextColumn();

    /* -- Column 3: Amp Envelope -- */
    ImGui::BeginChild("AmpEnv", ImVec2(0, col_h), true);
    ImGui::Text("Amp Envelope");
    ImGui::Separator();

    knob_float("A##amp", &p->amp_env.attack, 0.001f, 2.0f, 0.005f, 0.001f);
    midi_learn_check(SQ_PARAM_AMP_ATTACK);
    ImGui::SameLine();
    knob_float("D##amp", &p->amp_env.decay, 0.001f, 2.0f, 0.3f, 0.001f);
    midi_learn_check(SQ_PARAM_AMP_DECAY);
    knob_float("S##amp", &p->amp_env.sustain, 0.0f, 1.0f, 0.8f, 0.01f);
    midi_learn_check(SQ_PARAM_AMP_SUSTAIN);
    ImGui::SameLine();
    knob_float("R##amp", &p->amp_env.release, 0.001f, 5.0f, 0.5f, 0.001f);
    midi_learn_check(SQ_PARAM_AMP_RELEASE);

    /* Amp envelope visualization */
    float avail_w2 = ImGui::GetContentRegionAvail().x;
    draw_adsr_curve(&p->amp_env, avail_w2, 60);

    ImGui::EndChild();
    ImGui::NextColumn();

    /* -- Column 4: LFO -- */
    ImGui::BeginChild("LFO", ImVec2(0, col_h), true);
    ImGui::Text("LFO");
    ImGui::Separator();

    int lw = (int)p->lfo.waveform;
    combo_with_scroll("Wave##lfo", &lw, wave_names, 4);
    p->lfo.waveform = (sq_waveform_t)lw;

    int ld = (int)p->lfo.dest;
    combo_with_scroll("Dest", &ld, lfo_dest_names, 4);
    p->lfo.dest = (sq_lfo_dest_t)ld;

    knob_float("Rate", &p->lfo.rate, 0.0f, 50.0f, 1.0f, 0.1f);
    ImGui::SameLine();
    knob_float("Depth", &p->lfo.depth, 0.0f, 1.0f, 0.5f, 0.01f);

    /* BPM sync */
    bool sync = p->lfo_bpm_sync;
    ImGui::Checkbox("BPM Sync", &sync);
    p->lfo_bpm_sync = sync;

    if (p->lfo_bpm_sync) {
        int sd = p->lfo_sync_division;
        combo_with_scroll("Div", &sd, lfo_sync_names, 6);
        p->lfo_sync_division = sd;
    }

    ImGui::EndChild();
    ImGui::NextColumn();

    ImGui::Columns(1);
}

/* --- Draw one FM operator ------------------------------------------------- */

static void draw_fm_operator(sq_fm_operator_t *op, int op_index)
{
    char grp_name[32];
    snprintf(grp_name, sizeof(grp_name), "Op %d", op_index + 1);

    ImGui::BeginChild(grp_name, ImVec2(0, 0), true);
    ImGui::Text("Op %d", op_index + 1);
    ImGui::Separator();

    char id_ratio[16], id_level[16], id_fb[16];
    snprintf(id_ratio, sizeof(id_ratio), "Ratio##%d", op_index);
    snprintf(id_level, sizeof(id_level), "Level##%d", op_index);
    snprintf(id_fb, sizeof(id_fb), "FB##%d", op_index);

    ImGui::SliderFloat(id_ratio, &op->freq_ratio, 0.5f, 16.0f, "%.2f");
    ImGui::SliderFloat(id_level, &op->level, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(id_fb, &op->feedback, 0.0f, 1.0f, "%.2f");

    /* Operator envelope */
    ImGui::Text("Envelope:");
    char id_a[16], id_d[16], id_s[16], id_r[16];
    snprintf(id_a, sizeof(id_a), "A##op%d", op_index);
    snprintf(id_d, sizeof(id_d), "D##op%d", op_index);
    snprintf(id_s, sizeof(id_s), "S##op%d", op_index);
    snprintf(id_r, sizeof(id_r), "R##op%d", op_index);

    ImGui::SliderFloat(id_a, &op->env.attack, 0.001f, 5.0f, "%.3f");
    ImGui::SliderFloat(id_d, &op->env.decay, 0.001f, 5.0f, "%.3f");
    ImGui::SliderFloat(id_s, &op->env.sustain, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(id_r, &op->env.release, 0.001f, 5.0f, "%.3f");

    ImGui::EndChild();
}

/* --- Draw FM synth controls ----------------------------------------------- */

static void draw_fm(sq_synth_preset_t *p, float panel_h)
{
    /* Algorithm selector + Amp envelope on first row */
    int alg = p->fm_algorithm;
    combo_with_scroll("Algorithm", &alg, fm_alg_names, FM_NUM_ALGORITHMS);
    p->fm_algorithm = alg;

    ImGui::SameLine();
    ImGui::Text("Amp: A%.3f D%.3f S%.2f R%.3f",
                p->amp_env.attack, p->amp_env.decay,
                p->amp_env.sustain, p->amp_env.release);

    /* Voice amp envelope controls */
    ImGui::PushItemWidth(80);
    ImGui::Text("Amp A:"); ImGui::SameLine();
    ImGui::SliderFloat("##fmAmpA", &p->amp_env.attack, 0.001f, 5.0f, "%.3f");
    ImGui::SameLine(); ImGui::Text("D:"); ImGui::SameLine();
    ImGui::SliderFloat("##fmAmpD", &p->amp_env.decay, 0.001f, 5.0f, "%.3f");
    ImGui::SameLine(); ImGui::Text("S:"); ImGui::SameLine();
    ImGui::SliderFloat("##fmAmpS", &p->amp_env.sustain, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine(); ImGui::Text("R:"); ImGui::SameLine();
    ImGui::SliderFloat("##fmAmpR", &p->amp_env.release, 0.001f, 5.0f, "%.3f");
    ImGui::PopItemWidth();

    /* Amp envelope visualization */
    float avail_w = ImGui::GetContentRegionAvail().x;
    draw_adsr_curve(&p->amp_env, avail_w, 60);

    /* FM algorithm diagram */
    draw_fm_algorithm(p->fm_algorithm, avail_w, 70);

    /* Four operator columns */
    float op_h = panel_h - 120;
    if (op_h < 150) op_h = 150;

    ImGui::Columns(FM_NUM_OPERATORS, "fm_op_cols", true);
    for (int op = 0; op < FM_NUM_OPERATORS; op++) {
        char child_id[16];
        snprintf(child_id, sizeof(child_id), "fm_op_%d", op);
        ImGui::BeginChild(child_id, ImVec2(0, op_h), false);
        draw_fm_operator(&p->fm_ops[op], op);
        ImGui::EndChild();
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
}

/* --- Draw wavetable synth controls ---------------------------------------- */

static void draw_wavetable(sq_engine_t *engine, sq_synth_preset_t *p,
                           float panel_h)
{
    float col_h = panel_h - 80;
    if (col_h < 100) col_h = 100;

    ImGui::Columns(3, "wt_cols", true);

    /* -- Column 1: Wavetable selection + position -- */
    ImGui::BeginChild("Wavetable", ImVec2(0, col_h), true);
    ImGui::Text("Wavetable");
    ImGui::Separator();

    /* Bank selector */
    if (engine->wt_banks && engine->num_wt_banks > 0) {
        /* Build bank names list */
        static const char *bank_names[SQ_WT_MAX_BANKS];
        for (uint32_t b = 0; b < engine->num_wt_banks && b < SQ_WT_MAX_BANKS; b++) {
            bank_names[b] = engine->wt_banks[b].name;
        }
        int bi = p->wt_bank_index;
        combo_with_scroll("Bank", &bi, bank_names, (int)engine->num_wt_banks);
        p->wt_bank_index = bi;
    } else {
        ImGui::Text("Bank: (none)");
    }

    ImGui::SliderFloat("Position", &p->wt_position, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Env Mod", &p->wt_env_depth, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("LFO Mod", &p->wt_lfo_depth, 0.0f, 1.0f, "%.2f");

    /* LFO controls */
    ImGui::Text("LFO:");
    int lw = (int)p->lfo.waveform;
    combo_with_scroll("Wave##wtlfo", &lw, wave_names, 4);
    p->lfo.waveform = (sq_waveform_t)lw;

    ImGui::SliderFloat("Rate##wt", &p->lfo.rate, 0.0f, 50.0f, "%.1f Hz");
    ImGui::SliderFloat("Depth##wt", &p->lfo.depth, 0.0f, 1.0f, "%.2f");

    /* Wavetable waveform visualizer */
    float avail_w = ImGui::GetContentRegionAvail().x;
    draw_wt_waveform(engine, p->wt_bank_index, p->wt_position, avail_w, 50);

    ImGui::EndChild();
    ImGui::NextColumn();

    /* -- Column 2: Amp Envelope -- */
    ImGui::BeginChild("WTAmpEnv", ImVec2(0, col_h), true);
    ImGui::Text("Amp Envelope");
    ImGui::Separator();

    ImGui::SliderFloat("A##wtamp", &p->amp_env.attack, 0.001f, 5.0f, "%.3f");
    ImGui::SliderFloat("D##wtamp", &p->amp_env.decay, 0.001f, 5.0f, "%.3f");
    ImGui::SliderFloat("S##wtamp", &p->amp_env.sustain, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("R##wtamp", &p->amp_env.release, 0.001f, 5.0f, "%.3f");

    /* Amp envelope visualization */
    float avail_w2 = ImGui::GetContentRegionAvail().x;
    draw_adsr_curve(&p->amp_env, avail_w2, 60);

    ImGui::EndChild();
    ImGui::NextColumn();

    /* -- Column 3: Position Envelope (uses filter_env) -- */
    ImGui::BeginChild("WTModEnv", ImVec2(0, col_h), true);
    ImGui::Text("Position Envelope");
    ImGui::Separator();

    ImGui::SliderFloat("A##wtmod", &p->filter_env.attack, 0.001f, 5.0f, "%.3f");
    ImGui::SliderFloat("D##wtmod", &p->filter_env.decay, 0.001f, 5.0f, "%.3f");
    ImGui::SliderFloat("S##wtmod", &p->filter_env.sustain, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("R##wtmod", &p->filter_env.release, 0.001f, 5.0f, "%.3f");

    /* Position envelope visualization */
    float avail_w3 = ImGui::GetContentRegionAvail().x;
    draw_adsr_curve(&p->filter_env, avail_w3, 60);

    ImGui::EndChild();
    ImGui::NextColumn();

    ImGui::Columns(1);
}

/* --- Main synth editor entry point ---------------------------------------- */

extern "C" {

void synth_editor_draw(sq_engine_t *engine, int *synth_preset_ptr,
                       float x, float y, float w, float h)
{
    int preset_index = synth_preset_ptr ? *synth_preset_ptr : -1;
    if (preset_index < 0 || (uint32_t)preset_index >= engine->num_synth_presets)
        return;

    sq_synth_preset_t *p = &engine->synth_presets[preset_index];

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("SynthEditor", nullptr, flags)) {
        ImGui::End();
        return;
    }

    /* Header: preset selector + mode selector */

    /* Preset dropdown -- build string pointer array */
    static const char *preset_names[64];
    int count = (int)engine->num_synth_presets;
    if (count > 64) count = 64;
    for (int i = 0; i < count; i++)
        preset_names[i] = engine->synth_presets[i].name;

    int sel = preset_index;

    ImGui::PushItemWidth(200);
    combo_with_scroll("Preset", &sel, preset_names, count);
    ImGui::PopItemWidth();

    if (sel != preset_index) {
        /* Bounds check before using */
        if (sel >= 0 && (uint32_t)sel < engine->num_synth_presets) {
            LOG_DEBUG("Preset change: %d -> %d (count=%u)",
                      preset_index, sel, engine->num_synth_presets);
            /* Clear any ADSR drag state on preset change */
            s_adsr_drag_point = 0;
            s_adsr_drag_id = 0;
            *synth_preset_ptr = sel;
            preset_index = sel;
            p = &engine->synth_presets[preset_index];
        } else {
            LOG_WARN("Preset index out of bounds: sel=%d count=%u",
                     sel, engine->num_synth_presets);
            sel = preset_index; /* revert */
        }
    }

    ImGui::SameLine();

    int mode = (int)p->synth_mode;
    if (mode < 0 || mode > 2) mode = 0;
    int old_mode = mode;
    ImGui::PushItemWidth(120);
    combo_with_scroll("Mode", &mode, synth_mode_names, 3);
    ImGui::PopItemWidth();
    if (mode >= 0 && mode <= 2) {
        if (mode != old_mode) {
            LOG_DEBUG("Mode change: %s -> %s",
                      synth_mode_names[old_mode], synth_mode_names[mode]);
            /* Clear ADSR drag state on mode change */
            s_adsr_drag_point = 0;
            s_adsr_drag_id = 0;
        }
        p->synth_mode = (sq_synth_mode_t)mode;
    }

    ImGui::Separator();

    /* Draw mode-specific controls */
    switch (p->synth_mode) {
    case SYNTH_SUBTRACTIVE:
        draw_subtractive(p, h);
        break;
    case SYNTH_FM:
        draw_fm(p, h);
        break;
    case SYNTH_WAVETABLE:
        draw_wavetable(engine, p, h);
        break;
    }

    ImGui::End();
}

} /* extern "C" */
