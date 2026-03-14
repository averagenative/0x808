/*
 * export_dialog.cpp — GUI dialog for audio export (Dear ImGui port).
 */

#include "imgui.h"

extern "C" {
#include "engine/engine.h"
#include "engine/export.h"
}

extern "C" {
#define LOG_TAG "export_ui"
#include "core/log.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* --- State --- */

static int  s_visible = 0;
static char s_filename[256] = "output.wav";
static int  s_format = 0;       /* 0=WAV 16, 1=WAV 24, 2=WAV 32-float, 3-6=MP3 */
static int  s_num_bars = 4;
static char s_status[256] = "";
static int  s_export_done = 0;

static const char *format_names[] = {
    "WAV 16-bit", "WAV 24-bit", "WAV 32-float",
    "MP3 128k", "MP3 192k", "MP3 256k", "MP3 320k"
};
#define NUM_FORMATS 7

extern "C" void export_dialog_show(void)
{
    s_visible = 1;
    s_export_done = 0;
    s_status[0] = '\0';
}

extern "C" void export_dialog_hide(void)
{
    s_visible = 0;
}

extern "C" int export_dialog_visible(void)
{
    return s_visible;
}

extern "C" void export_dialog_draw(sq_engine_t *engine)
{
    if (!s_visible) return;

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(200, 150), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Export Audio", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open) s_visible = 0;
        return;
    }

    if (!open) {
        s_visible = 0;
        ImGui::End();
        return;
    }

    /* Filename */
    ImGui::Text("Filename:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##Filename", s_filename, sizeof(s_filename));

    /* Format */
    ImGui::Text("Format:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::Combo("##Format", &s_format, format_names, NUM_FORMATS);

    /* Number of bars */
    ImGui::Text("Bars:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderInt("##Bars", &s_num_bars, 1, 32);

    ImGui::Text("Duration: %d bars at %.0f BPM", s_num_bars, engine->transport.bpm);

    ImGui::Spacing();

    /* Export button */
    if (ImGui::Button("Export", ImVec2(120, 30))) {
        sq_export_config_t config;
        memset(&config, 0, sizeof(config));
        config.sample_rate = engine->sample_rate;
        config.num_bars = s_num_bars;
        config.pattern_index = engine->transport.current_pattern;

        sq_export_result_t result;
        memset(&result, 0, sizeof(result));
        if (sq_export_render(engine, &config, &result) == 0) {
            int write_ok;
            if (s_format >= 3) {
                /* MP3 export */
                static const int mp3_bitrates[] = {128, 192, 256, 320};
                int bitrate = mp3_bitrates[s_format - 3];
                write_ok = sq_export_write_mp3(s_filename, &result, bitrate);
            } else {
                /* WAV export */
                static const int wav_depths[] = {16, 24, 32};
                write_ok = sq_export_write_wav(s_filename, &result,
                                                wav_depths[s_format]);
            }
            if (write_ok == 0) {
                snprintf(s_status, sizeof(s_status),
                         "Exported: %s (%.1fs, peak=%.3f)",
                         s_filename,
                         (double)result.num_frames / result.sample_rate,
                         result.peak_level);
                s_export_done = 1;
            } else {
                snprintf(s_status, sizeof(s_status), "Error writing %s",
                         s_filename);
            }
            free(result.data);
        } else {
            snprintf(s_status, sizeof(s_status), "Render failed!");
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Close", ImVec2(120, 30))) {
        s_visible = 0;
    }

    /* Status */
    if (s_status[0]) {
        ImGui::Spacing();
        ImVec4 status_color = s_export_done
            ? ImVec4(0.31f, 0.78f, 0.31f, 1.0f)
            : ImVec4(0.78f, 0.31f, 0.31f, 1.0f);
        ImGui::TextColored(status_color, "%s", s_status);
    }

    ImGui::End();
}
