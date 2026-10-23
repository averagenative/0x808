/*
 * builtin_samples.h — Every kit sample, built into the plugin.
 *
 * CMake generates builtin_samples.c (in the build directory) from the kit
 * table in src/engine/kits.c, and #embeds each WAV file, so adding a kit
 * adds its samples here too.
 */

#ifndef SQ_BUILTIN_SAMPLES_H
#define SQ_BUILTIN_SAMPLES_H

typedef struct {
    const char          *path;  /* as in the kit table: "samples/808/01.BD.808.wav" */
    const unsigned char *data;  /* the whole WAV file */
    unsigned int         size;
} sq_builtin_file_t;

extern const sq_builtin_file_t SQ_BUILTIN_SAMPLES[];
extern const int SQ_NUM_BUILTIN_SAMPLES;

#endif /* SQ_BUILTIN_SAMPLES_H */
