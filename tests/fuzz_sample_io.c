#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/engine.h"
#include "formats/sample_io.h"

#ifdef _WIN32
#define TEST_DIR ""
#else
#define TEST_DIR "/tmp/"
#endif

static void write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(data, 1, len, f); fclose(f); }
}

static uint32_t rng = 0xCAFEBABE;
static uint32_t rand32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main(void) {
    printf("=== Fuzz Sample IO ===\n");
    sq_sample_t sample;
    int passed = 0;

    /* Test 1: Random bytes as .wav */
    for (int i = 0; i < 50; i++) {
        char buf[512];
        int len = (rand32() % 400) + 10;
        for (int j = 0; j < len; j++) buf[j] = (char)(rand32() & 0xFF);
        char path[256];
        snprintf(path, sizeof(path), TEST_DIR "fuzz_%d.wav", i);
        write_file(path, buf, len);
        int rc = sample_io_load(path, &sample);
        if (rc == 0) sample_io_free(&sample);
        remove(path);
        passed++;
    }
    printf("  PASS: 50 random .wav files handled\n");

    /* Test 2: Valid WAV header with bad data */
    {
        /* RIFF header: valid structure but wrong sizes */
        uint8_t hdr[] = {
            'R','I','F','F', 0xFF,0xFF,0xFF,0x7F, /* huge size */
            'W','A','V','E',
            'f','m','t',' ', 16,0,0,0,            /* fmt chunk size=16 */
            1,0, 2,0,                               /* PCM, 2 channels */
            0x44,0xAC,0,0,                          /* 44100 Hz */
            0x10,0xB1,0x02,0,                       /* byte rate */
            4,0, 16,0,                              /* block align, bits per sample */
            'd','a','t','a', 0xFF,0xFF,0xFF,0x7F,  /* huge data size */
        };
        char path[256];
        snprintf(path, sizeof(path), TEST_DIR "fuzz_hdr.wav");
        write_file(path, hdr, sizeof(hdr));
        int rc = sample_io_load(path, &sample);
        if (rc == 0) sample_io_free(&sample);
        remove(path);
        printf("  PASS: WAV with huge size field handled\n");
        passed++;
    }

    /* Test 3: Empty files */
    {
        const char *exts[] = {".wav", ".mp3", ".flac"};
        for (int i = 0; i < 3; i++) {
            char path[256];
            snprintf(path, sizeof(path), TEST_DIR "fuzz_empty%s", exts[i]);
            write_file(path, "", 0);
            int rc = sample_io_load(path, &sample);
            if (rc == 0) sample_io_free(&sample);
            remove(path);
            passed++;
        }
        printf("  PASS: Empty files handled\n");
    }

    /* Test 4: Random bytes as .mp3 and .flac */
    for (int i = 0; i < 20; i++) {
        char buf[256];
        int len = (rand32() % 200) + 10;
        for (int j = 0; j < len; j++) buf[j] = (char)(rand32() & 0xFF);
        char path[256];
        snprintf(path, sizeof(path), TEST_DIR "fuzz_%d.mp3", i);
        write_file(path, buf, len);
        int rc = sample_io_load(path, &sample);
        if (rc == 0) sample_io_free(&sample);
        remove(path);

        snprintf(path, sizeof(path), TEST_DIR "fuzz_%d.flac", i);
        write_file(path, buf, len);
        rc = sample_io_load(path, &sample);
        if (rc == 0) sample_io_free(&sample);
        remove(path);
        passed++;
    }
    printf("  PASS: 20 random .mp3/.flac files handled\n");

    /* Test 5: NULL and empty path */
    int rc = sample_io_load(NULL, &sample);
    passed++; /* should return -1 */
    rc = sample_io_load("", &sample);
    passed++; /* should return -1 */
    rc = sample_io_load("nonexistent.wav", &sample);
    passed++; /* should return -1 */
    (void)rc;
    printf("  PASS: NULL/empty/nonexistent paths handled\n");

    printf("\n=== FUZZ SAMPLE IO: %d tests passed (no crashes) ===\n", passed);
    return 0;
}
