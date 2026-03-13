/*
 * export_dialog.c — GUI dialog for audio export.
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/export_dialog.h"
#include "engine/export.h"

#define LOG_TAG "export_ui"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── State ──────────────────────────────────────────────────────────────── */

static int s_visible = 0;
static char s_filename[256] = "output.wav";
static int s_format = 0;       /* index: 0=WAV 16, 1=WAV 24, 2=WAV 32-float, 3=MP3 128, 4=MP3 192, 5=MP3 256, 6=MP3 320 */
static int s_num_bars = 4;
static char s_status[256] = "";
static int s_export_done = 0;

static const char *format_names[] = {
    "WAV 16-bit", "WAV 24-bit", "WAV 32-float",
    "MP3 128k", "MP3 192k", "MP3 256k", "MP3 320k"
};
#define NUM_FORMATS 7

void export_dialog_show(void) { s_visible = 1; s_export_done = 0; s_status[0] = '\0'; }
void export_dialog_hide(void) { s_visible = 0; }
int  export_dialog_visible(void) { return s_visible; }

int export_dialog_draw(struct nk_context *ctx, sq_engine_t *engine)
{
    if (!s_visible) return 0;

    if (nk_begin(ctx, "Export Audio",
                 nk_rect(200, 150, 400, 300),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE |
                 NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE))
    {
        nk_layout_row_dynamic(ctx, 25, 2);

        /* Filename */
        nk_label(ctx, "Filename:", NK_TEXT_LEFT);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_SIMPLE, s_filename,
                                       sizeof(s_filename), nk_filter_default);

        /* Format */
        nk_label(ctx, "Format:", NK_TEXT_LEFT);
        nk_combobox(ctx, format_names, NUM_FORMATS, &s_format, 20, nk_vec2(150, 160));

        /* Number of bars */
        nk_label(ctx, "Bars:", NK_TEXT_LEFT);
        nk_slider_int(ctx, 1, &s_num_bars, 32, 1);

        nk_layout_row_dynamic(ctx, 20, 1);
        nk_labelf(ctx, NK_TEXT_LEFT, "Duration: %d bars at %.0f BPM",
                  s_num_bars, engine->transport.bpm);

        /* Export button */
        nk_layout_row_dynamic(ctx, 30, 2);
        if (nk_button_label(ctx, "Export")) {
            sq_export_config_t config = {0};
            config.sample_rate = engine->sample_rate;
            config.num_bars = s_num_bars;
            config.pattern_index = engine->transport.current_pattern;

            sq_export_result_t result = {0};
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

        if (nk_button_label(ctx, "Close")) {
            s_visible = 0;
        }

        /* Status */
        if (s_status[0]) {
            nk_layout_row_dynamic(ctx, 20, 1);
            struct nk_color status_color = s_export_done
                ? nk_rgb(80, 200, 80)
                : nk_rgb(200, 80, 80);
            nk_label_colored(ctx, s_status, NK_TEXT_LEFT, status_color);
        }
    } else {
        /* X button clicked */
        s_visible = 0;
    }
    nk_end(ctx);

    return 0;
}
