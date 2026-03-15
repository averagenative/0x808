/*
 * settings_panel.h — Settings panel for audio device and recording config.
 */

#ifndef SQ_SETTINGS_PANEL_H
#define SQ_SETTINGS_PANEL_H

#include "engine/engine.h"
#include "engine/sq_midi.h"
#include "app/sq_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the settings panel. Call when SQ_PANEL_SETTINGS is active.
 * midi may be NULL (e.g., in plugin builds). */
void settings_panel_draw(sq_engine_t *engine, sq_app_t *app,
                         sq_midi_t *midi,
                         float x, float y, float w, float h);

#ifdef __cplusplus
}
#endif

#endif /* SQ_SETTINGS_PANEL_H */
