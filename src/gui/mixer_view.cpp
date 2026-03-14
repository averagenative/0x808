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
#include <cmath>

static const char *effect_type_names[] = {"None", "Filter", "Delay", "Reverb", "Overdrive", "Fuzz", "Chorus"};
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

        case EFFECT_OVERDRIVE: {
            ImGui::Text("Drive: %.0f%%", slot->overdrive.drive * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##OdDrive", &slot->overdrive.drive, 0.0f, 1.0f);

            ImGui::Text("Tone: %.0f%%", slot->overdrive.tone * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##OdTone", &slot->overdrive.tone, 0.0f, 1.0f);

            ImGui::Text("Mix: %.0f%%", slot->overdrive.mix * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##OdMix", &slot->overdrive.mix, 0.0f, 1.0f);
            break;
        }

        case EFFECT_FUZZ: {
            ImGui::Text("Gain: %.0f%%", slot->fuzz.gain * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##FzGain", &slot->fuzz.gain, 0.0f, 1.0f);

            ImGui::Text("Tone: %.0f%%", slot->fuzz.tone * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##FzTone", &slot->fuzz.tone, 0.0f, 1.0f);

            ImGui::Text("Mix: %.0f%%", slot->fuzz.mix * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##FzMix", &slot->fuzz.mix, 0.0f, 1.0f);
            break;
        }

        case EFFECT_CHORUS: {
            ImGui::Text("Rate: %.1f Hz", slot->chorus.rate);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##ChRate", &slot->chorus.rate, 0.1f, 10.0f);

            ImGui::Text("Depth: %.0f%%", slot->chorus.depth * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##ChDepth", &slot->chorus.depth, 0.0f, 1.0f);

            ImGui::Text("Mix: %.0f%%", slot->chorus.mix * 100);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##ChMix", &slot->chorus.mix, 0.0f, 1.0f);
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

    ImDrawList *dl = ImGui::GetWindowDrawList();

    int pat_idx_g = engine->transport.current_pattern;
    uint32_t num_tracks_g = 0;
    sq_pattern_t *pat_g = NULL;
    if (pat_idx_g >= 0 && (uint32_t)pat_idx_g < engine->num_patterns) {
        pat_g = &engine->patterns[pat_idx_g];
        num_tracks_g = pat_g->num_tracks;
    }

    /* --- Top section: Effects browse + slots --- */
    float total_h = ImGui::GetContentRegionAvail().y;
    float bottom_h = total_h * 0.55f;
    if (bottom_h < 100.0f) bottom_h = 100.0f;
    float top_h = total_h - bottom_h;
    if (top_h < 50.0f) top_h = 50.0f;

    if (ImGui::BeginChild("EffectsPanel", ImVec2(0, top_h),
                          ImGuiChildFlags_Borders)) {
        static int fx_tab = 0; /* 0 = Master, 1+ = track index+1 */

        int pat_idx2 = engine->transport.current_pattern;
        uint32_t nt = 0;
        if (pat_idx2 >= 0 && (uint32_t)pat_idx2 < engine->num_patterns)
            nt = engine->patterns[pat_idx2].num_tracks;

        /* Clamp fx_tab if tracks changed */
        if (fx_tab > (int)nt) fx_tab = 0;

        /* Build display label: "Master" or "Trk N: Name (Type)" */
        char fx_label[128];
        if (fx_tab == 0) {
            snprintf(fx_label, sizeof(fx_label), "Master Bus");
        } else {
            uint32_t ti = (uint32_t)(fx_tab - 1);
            const char *tname = "(empty)";
            const char *ttype = "Sampler";
            if (ti < nt) {
                sq_track_t *trk = &engine->patterns[pat_idx2].tracks[ti];
                if (trk->type == TRACK_SYNTH) {
                    ttype = "Synth";
                    if (trk->synth_preset >= 0 &&
                        (uint32_t)trk->synth_preset < engine->num_synth_presets)
                        tname = engine->synth_presets[trk->synth_preset].name;
                } else if (trk->type == TRACK_SF2) {
                    ttype = "SF2";
                    if (trk->sf2_preset >= 0 &&
                        (uint32_t)trk->sf2_preset < engine->num_sf2_presets)
                        tname = engine->sf2_presets[trk->sf2_preset].name;
                } else {
                    if (trk->sample_index >= 0 &&
                        (uint32_t)trk->sample_index < engine->num_samples)
                        tname = engine->samples[trk->sample_index].name;
                }
                snprintf(fx_label, sizeof(fx_label), "Trk %u: %s (%s)", ti + 1, tname, ttype);
            } else {
                snprintf(fx_label, sizeof(fx_label), "Trk %u", ti + 1);
            }
        }

        /* Browse bar: [<] [label clipped] [>] — fixed layout using a table */
        {
            float btn_w = 30.0f;
            bool can_prev = (fx_tab > 0);
            bool can_next = (fx_tab < (int)nt);

            if (ImGui::BeginTable("FXBrowse", 3, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("prev", ImGuiTableColumnFlags_WidthFixed, btn_w);
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("next", ImGuiTableColumnFlags_WidthFixed, btn_w);

                ImGui::TableNextColumn();
                if (!can_prev) ImGui::BeginDisabled();
                if (ImGui::Button("<##fx_prev", ImVec2(btn_w, 0)))
                    fx_tab--;
                if (!can_prev) ImGui::EndDisabled();

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(fx_label);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", fx_label);

                ImGui::TableNextColumn();
                if (!can_next) ImGui::BeginDisabled();
                if (ImGui::Button(">##fx_next", ImVec2(btn_w, 0)))
                    fx_tab++;
                if (!can_next) ImGui::EndDisabled();

                ImGui::EndTable();
            }
        }

        ImGui::Separator();

        /* Draw effect slots for selected tab */
        sq_effect_slot_t *slots = NULL;
        if (fx_tab == 0) {
            slots = engine->master_effects;
        } else {
            int pat_idx2 = engine->transport.current_pattern;
            if (pat_idx2 >= 0 && (uint32_t)pat_idx2 < engine->num_patterns) {
                uint32_t ti = (uint32_t)(fx_tab - 1);
                if (ti < engine->patterns[pat_idx2].num_tracks)
                    slots = engine->patterns[pat_idx2].tracks[ti].effects;
            }
        }

        if (slots) {
            float slot_w = (ImGui::GetContentRegionAvail().x - 8.0f) / MAX_TRACK_EFFECTS;
            float slot_h = ImGui::GetContentRegionAvail().y;
            for (int i = 0; i < MAX_TRACK_EFFECTS; i++) {
                if (i > 0) ImGui::SameLine(0, 4);
                char child_id[32];
                snprintf(child_id, sizeof(child_id), "FXSlot%d", i);
                ImGui::BeginChild(child_id, ImVec2(slot_w, slot_h),
                                  ImGuiChildFlags_Borders);
                char slot_label[32];
                snprintf(slot_label, sizeof(slot_label), "Slot %d", i + 1);
                draw_effect_slot(&slots[i], slot_label,
                                 engine->sample_rate, i + fx_tab * 10);
                ImGui::EndChild();
            }
        }
    }
    ImGui::EndChild();

    /*
     * Bottom section (vertical stack matching GTK):
     *   [< 1 2 3 ... >]   ← track selector
     *   [VU meters + MST]  ← level meters full width
     *   [Strips + MST]     ← vol/pan/mute + master meter
     */
    static int track_scroll = 0;
    if (track_scroll >= (int)num_tracks_g) track_scroll = 0;

    float full_w = ImGui::GetContentRegionAvail().x;
    float master_col_w = 40.0f;
    float track_area_w = full_w - master_col_w - 4.0f;
    float strip_w = 32.0f;
    int max_vis = (int)((track_area_w - 40.0f) / (strip_w + 2.0f));
    if (max_vis < 1) max_vis = 1;
    int end_t = track_scroll + max_vis;
    if (end_t > (int)num_tracks_g) end_t = (int)num_tracks_g;
    bool can_l = (track_scroll > 0);
    bool can_r = (track_scroll + max_vis < (int)num_tracks_g);

    /* --- Track selector row: [<] 1 2 3 ... [>] --- */
    {
        if (!can_l) ImGui::BeginDisabled();
        if (ImGui::Button("<##ts", ImVec2(16, 16))) track_scroll--;
        if (!can_l) ImGui::EndDisabled();

        for (int t = track_scroll; t < end_t; t++) {
            ImGui::SameLine(0, 0);
            char lbl[8];
            snprintf(lbl, sizeof(lbl), " %d ", t + 1);
            float tx = 20.0f + (float)(t - track_scroll) * (strip_w + 2.0f) +
                       strip_w * 0.5f - 6.0f;
            ImGui::SetCursorPosX(tx);
            ImGui::Text("%s", lbl);
        }

        ImGui::SameLine(track_area_w + 2.0f);
        if (!can_r) ImGui::BeginDisabled();
        if (ImGui::Button(">##ts", ImVec2(16, 16))) track_scroll++;
        if (!can_r) ImGui::EndDisabled();
    }

    /* --- Combined: [VU+Strips | Master meter] side by side --- */
    float remaining_h = ImGui::GetContentRegionAvail().y;
    if (remaining_h < 40.0f) remaining_h = 40.0f;

    /* VU meter row */
    float vu_h = 30.0f;
    {
        float meter_w = 10.0f;
        ImVec2 base = ImGui::GetCursorScreenPos();
        for (int t = track_scroll; t < end_t; t++) {
            float cx = 20.0f + (float)(t - track_scroll) * (strip_w + 2.0f) +
                       strip_w * 0.5f - meter_w * 0.5f;
            draw_level_meter(dl, ImVec2(base.x + cx, base.y), meter_w, vu_h,
                             engine->track_peaks[t]);
        }

        /* MST label above master meter column */
        ImVec2 mst_pos(base.x + track_area_w + 4.0f, base.y);
        dl->AddText(mst_pos, IM_COL32(255, 255, 255, 255), "MST");

        ImGui::Dummy(ImVec2(full_w, vu_h + 2.0f));
    }

    /* Channel strips */
    float strip_h = ImGui::GetContentRegionAvail().y;
    if (strip_h < 20.0f) strip_h = 20.0f;
    float vol_h = strip_h - 44.0f;
    if (vol_h < 10.0f) vol_h = 10.0f;

    if (num_tracks_g > 0 && pat_g) {
        /* Style overrides for all strips */
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.45f, 0.65f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.55f, 0.75f, 1.0f, 1.0f));

        for (int t = track_scroll; t < end_t; t++) {
            if (t > track_scroll) ImGui::SameLine(0, 2);
            ImGui::PushID(t);

            ImVec4 bg = (t % 2 == 0)
                ? ImVec4(0.22f, 0.22f, 0.26f, 1.0f)
                : ImVec4(0.18f, 0.18f, 0.21f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);

            ImGuiWindowFlags sf = ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse;
            if (ImGui::BeginChild("s", ImVec2(strip_w, strip_h),
                                  ImGuiChildFlags_None, sf)) {
                float sw = strip_w - 4.0f;

                /* Volume slider */
                ImGui::PushID("vol");
                float vol = pat_g->tracks[t].volume * 100.0f;
                if (ImGui::VSliderFloat("##v", ImVec2(sw, vol_h),
                                        &vol, 0.0f, 100.0f, ""))
                    pat_g->tracks[t].volume = vol / 100.0f;
                ImGui::PopID();

                /* Pan knob */
                {
                    float pan = pat_g->tracks[t].pan;
                    ImVec2 kp = ImGui::GetCursorScreenPos();
                    float kr = 9.0f;
                    float kcx = kp.x + strip_w * 0.5f - 2.0f;
                    float kcy = kp.y + kr + 1.0f;
                    float a0 = (float)(M_PI * 0.75);
                    float a1 = (float)(M_PI * 2.25);
                    float norm = (pan + 1.0f) * 0.5f;
                    float ca = (float)(M_PI * 1.5);
                    float va = a0 + norm * (a1 - a0);

                    /* Background arc */
                    dl->PathArcTo(ImVec2(kcx, kcy), kr, a0, a1, 24);
                    dl->PathStroke(IM_COL32(45, 45, 55, 160), 0, 2.5f);

                    /* Value arc (bipolar from center) */
                    if (va > ca) {
                        dl->PathArcTo(ImVec2(kcx, kcy), kr, ca, va, 12);
                    } else {
                        dl->PathArcTo(ImVec2(kcx, kcy), kr, va, ca, 12);
                    }
                    dl->PathStroke(IM_COL32(50, 230, 50, 220), 0, 2.5f);

                    /* Knob body */
                    dl->AddCircleFilled(ImVec2(kcx, kcy), kr * 0.6f,
                                        IM_COL32(75, 78, 88, 255));
                    dl->AddCircle(ImVec2(kcx, kcy), kr * 0.6f,
                                  IM_COL32(110, 115, 130, 255), 0, 1.5f);

                    /* Indicator line */
                    float ix = kcx + cosf(va) * kr * 0.5f;
                    float iy = kcy + sinf(va) * kr * 0.5f;
                    dl->AddLine(ImVec2(kcx, kcy), ImVec2(ix, iy),
                                IM_COL32(50, 230, 50, 255), 2.0f);

                    /* Drag interaction */
                    ImGui::PushID("pan");
                    ImGui::InvisibleButton("##pk", ImVec2(sw, kr * 2 + 2));
                    if (ImGui::IsItemActive()) {
                        pan -= ImGui::GetIO().MouseDelta.y * 0.01f;
                        if (pan < -1.0f) pan = -1.0f;
                        if (pan > 1.0f) pan = 1.0f;
                        pat_g->tracks[t].pan = pan;
                    }
                    ImGui::PopID();
                }

                /* Mute button */
                bool muted = pat_g->tracks[t].mute;
                if (muted)
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("M", ImVec2(sw, 0)))
                    pat_g->tracks[t].mute = !pat_g->tracks[t].mute;
                if (muted) ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(); /* ChildBg */
            ImGui::PopID();
        }

        ImGui::PopStyleColor(4); /* FrameBg, FrameBgHovered, SliderGrab, SliderGrabActive */

        /* Master L/R VU meters (full strip height) */
        ImGui::SameLine(track_area_w + 4.0f);
        {
            ImVec2 mst_base = ImGui::GetCursorScreenPos();
            float mst_h = strip_h - 16.0f;
            if (mst_h < 10.0f) mst_h = 10.0f;
            draw_level_meter(dl, mst_base, 14.0f, mst_h, engine->master_peak[0]);
            draw_level_meter(dl, ImVec2(mst_base.x + 18.0f, mst_base.y),
                             14.0f, mst_h, engine->master_peak[1]);
            ImGui::Dummy(ImVec2(34.0f, mst_h));
            ImGui::SetCursorPosX(track_area_w + 6.0f);
            ImGui::Text("L  R");
        }
    }

    ImGui::End();
}
