/*
 * user_patterns.h — User-saved drum patterns (separate from project files).
 *
 * Each pattern lives as a single .drumpat JSON file under a per-user
 * data directory (~/.local/share/0x808/patterns/ on Linux,
 * %APPDATA%/0x808/patterns/ on Windows). Library is scanned at startup
 * and refreshed after save/delete.
 */

#ifndef SQ_USER_PATTERNS_H
#define SQ_USER_PATTERNS_H

#include "engine/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SQ_USER_PATTERN_NAME_LEN 64
#define SQ_USER_PATTERN_MAX      128

typedef struct {
    char name[SQ_USER_PATTERN_NAME_LEN];
    char filename[SQ_USER_PATTERN_NAME_LEN + 16]; /* name + ".drumpat" */
} sq_user_pattern_entry_t;

/* Re-scan the user patterns directory. Cheap; safe to call every frame
 * (uses mtime cache). Returns the current number of entries. */
int user_patterns_refresh(void);

/* Number of currently-cached entries. */
int user_patterns_count(void);

/* Entry at index [0..count). Returns NULL if out of range. */
const sq_user_pattern_entry_t *user_patterns_get(int index);

/* Save the given pattern under `name`. Returns 0 on success. */
int user_patterns_save(const sq_pattern_t *pat, const char *name,
                       double bpm);

/* Load pattern from the given entry index into `out`. Returns 0 on success. */
int user_patterns_load(int index, sq_pattern_t *out, double *out_bpm);

/* Delete the user pattern at `index`. Returns 0 on success. */
int user_patterns_delete(int index);

#ifdef __cplusplus
}
#endif

#endif
