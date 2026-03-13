/*
 * project.h — Save and load complete project state as JSON (.sqproj).
 *
 * Serializes: patterns, tracks, steps, synth presets, arrangement,
 * transport settings, effect settings, and sample file paths.
 */

#ifndef SQ_PROJECT_H
#define SQ_PROJECT_H

#include "engine/engine.h"

/*
 * Save the full engine state to a .sqproj JSON file.
 * Sample data is NOT saved — only file paths (relative to project file).
 * Returns 0 on success, -1 on failure.
 */
int project_save(const sq_engine_t *engine, const char *filepath);

/*
 * Load a .sqproj JSON file and restore engine state.
 * Reloads referenced samples from their saved paths.
 * Returns 0 on success, -1 on failure.
 */
int project_load(sq_engine_t *engine, const char *filepath);

#endif /* SQ_PROJECT_H */
