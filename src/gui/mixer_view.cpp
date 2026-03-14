/*
 * mixer_view.cpp — Mixer view with master effects controls and LED level meters
 *                  (Dear ImGui port).
 *
 * Shows master effects chain (3 slots) with type selector and parameter sliders,
 * plus per-track and master LED-style level meters.
 */

#include "imgui.h"

extern "C" {
#include "engine/engine.h"
#include "engine/effects.h"
}

extern "C" {
#define LOG_TAG "mixer"
#include "core/log.h"
}

#include <cstdio>
#include <cstring>

static const char *effect_type_names[] = {"None", "Filter", "Delay", "Reverb"};
static const char *filter_mode_names[] = {"LowPass", "HiPass", "BandPass"};
static const char *delay_sync_names[]  = {"1/1", "1/2", "1/4", "1/8", "1/16"};

/* --- LED-style level meter --- */

static void draw_level_meter(ImDrawList *dl, ImVec2 pos, float w, float h, float level)
{
    /* Background */
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(20, 20, 25, 255));

    /* Clamp level */
    if (level > 1.5f) level = 1.5f;
    if (level < 0.0f) level = 0.0f;

    int segs = 12;
    float seg_gap = 1.0f;
    float seg_h = (h - seg_gap * (segs - 1)) / (float)segs;
    if (seg_h < 1.0f) seg_h = 1.0f;

    for (int i = 0; i < segs; i++) {
        float seg_y = pos.y + h - (float)(i + 1) * (seg_h + seg_gap);
        float seg_level = (float)(i + 1) / (float)segs;

        ImU32 c;
        if (seg_level <= level) {
            /* Lit segment */
            if (seg_level < 0.6f)
                c = IM_COL32(40, 180, 40, 220);       /* green */
            else if (seg_level < 0.85f)
                c = IM_COL32(200, 200, 40, 220);      /* yellow */
            else
                c = IM_COL32(220, 40, 40, 220);       /* red */
        } else {
            /* Unlit segment (dim) */
            if (seg_level < 0.6f)
                c = IM_COL32(15, 40, 15, 255);
            else if (seg_level < 0.85f)
                c = IM_COL32(40, 40, 15, 255);
            else
                c = IM_COL32(40, 15, 15, 255);
        }

        dl->AddRectFilled(ImVec2(pos.x, seg_y),
                          ImVec2(pos.x + w, seg_y + seg_h), c);
    }
}

/* --- Effect slot drawing --- */

static void draw_effect_slot(sq_effect_slot_t *slot, const char *label,
                             uint32_t sample_rate, int slot_id)
{
    ImGui::PushID(slot_id);

    ImGui::Text("%s", label);
    ImGui::Separator();

    /* Type selector */
    int t = (int)slot->type;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("Type", &t, effect_type_names, EFFECT_TYPE_COUNT)) {
        if ((sq_effect_type_t)t != slot->type) {
            effect_init(slot, (sq_effect_type_t)t, sample_rate);
        }
    }

    if (slot->type != EFFECT_NONE) {
        /* Bypass toggle */
        bool bp = slot->bypass;
        ImGui::Checkbox("Bypass", &bp);
        slot->bypass = bp;

        /* Type-specific parameters */
        switch (slot->type) {
        case EFFECT_FILTER: {
            int m = (int)slot->filter.mode;
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::Combo("Mode", &m, filter_mode_names, 3)) {
                slot->filter.mode = (sq_efx_filter_mode_t)m;
            }

            ImGui::Text("Cutoff: %.0f", slot->filter.cutoff);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##Cutoff", &slot->filter.cutoff, 20.0f, 20000.0f,
                               "%.0f Hz", ImGuiSliderFlags_Logarithmic);

            ImGui::Text("Reso: %.1f", slot->filter.resonance);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##Reso", &slot->filter.resonance, 0.5f, 20.0f);

            ImGui::Text("Wet: %.0f%%", slot->filter.wet * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##FiltWet", &slot->filter.wet, 0.0f, 1.0f,
                               "%.0f%%");
            break;
        }

        case EFFECT_DELAY: {
            ImGui::Text("Time: %.3fs", slot->delay.time);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##DlyTime", &slot->delay.time, 0.01f, 2.0f);

            ImGui::Text("Feedback: %.0f%%", slot->delay.feedback * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##DlyFB", &slot->delay.feedback, 0.0f, 0.95f);

            ImGui::Text("Wet: %.0f%%", slot->delay.wet * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##DlyWet", &slot->delay.wet, 0.0f, 1.0f);

            bool sync = slot->delay.bpm_sync;
            ImGui::Checkbox("BPM Sync", &sync);
            slot->delay.bpm_sync = sync;

            if (slot->delay.bpm_sync) {
                ImGui::SetNextItemWidth(80.0f);
                ImGui::Combo("Div", &slot->delay.sync_division,
                             delay_sync_names, 5);
            }
            break;
        }

        case EFFECT_REVERB: {
            ImGui::Text("Room: %.0f%%", slot->reverb.room_size * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##RvRoom", &slot->reverb.room_size, 0.0f, 1.0f);

            ImGui::Text("Damp: %.0f%%", slot->reverb.damping * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##RvDamp", &slot->reverb.damping, 0.0f, 1.0f);

            ImGui::Text("Wet: %.0f%%", slot->reverb.wet * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##RvWet", &slot->reverb.wet, 0.0f, 1.0f);
            break;
        }

        default:
            break;
        }
    }

    ImGui::PopID();
}

/* --- Main mixer view --- */

extern "C" void mixer_view_draw(sq_engine_t *engine,
                                float x, float y, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoScrollbar;

    if (!ImGui::Begin("MasterFX", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Mixer / Master FX");
    ImGui::Separator();

    ImDrawList *dl = ImGui::GetWindowDrawList();

    float meter_panel_w = 180.0f;
    float master_meter_w = 50.0f;
    float effects_w = w - meter_panel_w - master_meter_w - 30.0f;
    if (effects_w < 200.0f) effects_w = 200.0f;

    float content_h = ImGui::GetContentRegionAvail().y;

    /* --- Section 1: Per-track level meters --- */
    if (ImGui::BeginChild("TrackMeters", ImVec2(meter_panel_w, content_h),
                          ImGuiChildFlags_Borders)) {
        int pat_idx = engine->transport.current_pattern;
        uint32_t num_tracks = 0;
        if (pat_idx >= 0 && (uint32_t)pat_idx < engine->num_patterns) {
            num_tracks = engine->patterns[pat_idx].num_tracks;
        }

        ImGui::Text("Track Levels");

        if (num_tracks > 0) {
            float meter_w = (meter_panel_w - 20.0f) / (float)num_tracks;
            if (meter_w > 16.0f) meter_w = 16.0f;
            float meter_h = content_h - 50.0f;
            if (meter_h < 40.0f) meter_h = 40.0f;

            ImVec2 base = ImGui::GetCursorScreenPos();

            /* Draw meters */
            for (uint32_t t = 0; t < num_tracks; t++) {
                ImVec2 mpos = ImVec2(base.x + t * (meter_w + 2.0f), base.y);
                draw_level_meter(dl, mpos, meter_w, meter_h, engine->track_peaks[t]);
            }

            /* Advance cursor past meters */
            ImGui::Dummy(ImVec2(num_tracks * (meter_w + 2.0f), meter_h));

            /* Track number labels */
            for (uint32_t t = 0; t < num_tracks; t++) {
                if (t > 0) ImGui::SameLine();
                char num[4];
                snprintf(num, sizeof(num), "%u", t + 1);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + t * 0.5f);
                ImGui::Text("%s", num);
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* --- Section 2: Master effects slots --- */
    if (ImGui::BeginChild("EffectsPanel", ImVec2(effects_w, content_h),
                          ImGuiChildFlags_Borders)) {
        /* Divide effects panel into columns */
        if (ImGui::BeginTable("FXSlots", MAX_TRACK_EFFECTS,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            for (int i = 0; i < MAX_TRACK_EFFECTS; i++) {
                ImGui::TableSetColumnIndex(i);
                char slot_label[32];
                snprintf(slot_label, sizeof(slot_label), "Slot %d", i + 1);
                draw_effect_slot(&engine->master_effects[i], slot_label,
                                 engine->sample_rate, i);
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* --- Section 3: Master output level meter --- */
    if (ImGui::BeginChild("MasterMeter", ImVec2(master_meter_w, content_h),
                          ImGuiChildFlags_Borders)) {
        ImGui::Text("MST");

        float meter_h = content_h - 50.0f;
        if (meter_h < 40.0f) meter_h = 40.0f;

        ImVec2 base = ImGui::GetCursorScreenPos();

        /* L meter */
        draw_level_meter(dl, base, 14.0f, meter_h, engine->master_peak[0]);
        /* R meter */
        draw_level_meter(dl, ImVec2(base.x + 18.0f, base.y), 14.0f, meter_h,
                         engine->master_peak[1]);

        ImGui::Dummy(ImVec2(34.0f, meter_h));

        /* L/R labels */
        ImGui::Text("L");
        ImGui::SameLine(20.0f);
        ImGui::Text("R");
    }
    ImGui::EndChild();

    ImGui::End();
}
