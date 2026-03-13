#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/engine.h"
#include "formats/project.h"

#ifdef _WIN32
#define TEST_FILE "fuzz_project.sqproj"
#else
#define TEST_FILE "/tmp/fuzz_project.sqproj"
#endif

static void write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(data, 1, len, f); fclose(f); }
}

static uint32_t rng = 0xDEADBEEF;
static uint32_t rand32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main(void) {
    printf("=== Fuzz Project Load ===\n");
    sq_engine_t engine;
    int passed = 0, total = 0;

    /* Test 1: Random bytes */
    for (int i = 0; i < 100; i++) {
        char buf[256];
        int len = (rand32() % 200) + 1;
        for (int j = 0; j < len; j++) buf[j] = (char)(rand32() & 0xFF);
        write_file(TEST_FILE, buf, len);
        sq_engine_init(&engine, 44100);
        project_load(&engine, TEST_FILE); /* should not crash */
        sq_engine_shutdown(&engine);
        total++; passed++;
    }
    printf("  PASS: %d random byte inputs survived\n", 100);

    /* Test 2: Valid JSON with wrong types */
    const char *bad_types[] = {
        "{\"bpm\":\"not a number\",\"patterns\":[]}",
        "{\"bpm\":null,\"patterns\":null}",
        "{\"bpm\":[1,2,3],\"patterns\":\"string\"}",
        "{\"patterns\":[{\"tracks\":\"not_array\"}]}",
        "{\"version\":999999}",
    };
    for (int i = 0; i < 5; i++) {
        write_file(TEST_FILE, bad_types[i], strlen(bad_types[i]));
        sq_engine_init(&engine, 44100);
        project_load(&engine, TEST_FILE);
        sq_engine_shutdown(&engine);
        total++; passed++;
    }
    printf("  PASS: %d wrong-type JSON inputs survived\n", 5);

    /* Test 3: Valid structure with extreme values */
    const char *extreme[] = {
        "{\"bpm\":99999999,\"master_volume\":-999,\"patterns\":[{\"num_tracks\":999999,\"tracks\":[]}]}",
        "{\"bpm\":0.0001,\"patterns\":[{\"num_tracks\":0,\"tracks\":[{\"length\":0,\"volume\":-1}]}]}",
        "{\"patterns\":[{\"num_tracks\":1,\"tracks\":[{\"type\":99,\"synth_preset\":9999}]}]}",
    };
    for (int i = 0; i < 3; i++) {
        write_file(TEST_FILE, extreme[i], strlen(extreme[i]));
        sq_engine_init(&engine, 44100);
        project_load(&engine, TEST_FILE);
        sq_engine_shutdown(&engine);
        total++; passed++;
    }
    printf("  PASS: %d extreme value inputs survived\n", 3);

    /* Test 4: Truncated valid JSON */
    const char *valid = "{\"version\":1,\"bpm\":120,\"patterns\":[{\"name\":\"Test\",\"num_tracks\":1,\"tracks\":[{\"type\":0,\"length\":16}]}]}";
    size_t vlen = strlen(valid);
    for (size_t cut = 1; cut < vlen; cut += 7) {
        write_file(TEST_FILE, valid, cut);
        sq_engine_init(&engine, 44100);
        project_load(&engine, TEST_FILE);
        sq_engine_shutdown(&engine);
        total++; passed++;
    }
    printf("  PASS: %d truncated JSON inputs survived\n", (int)((vlen-1)/7 + 1));

    /* Test 5: Deeply nested JSON */
    char deep[2048];
    int pos = 0;
    for (int i = 0; i < 100 && pos < 2000; i++) pos += snprintf(deep+pos, sizeof(deep)-pos, "{\"a\":");
    pos += snprintf(deep+pos, sizeof(deep)-pos, "1");
    for (int i = 0; i < 100 && pos < 2040; i++) pos += snprintf(deep+pos, sizeof(deep)-pos, "}");
    write_file(TEST_FILE, deep, pos);
    sq_engine_init(&engine, 44100);
    project_load(&engine, TEST_FILE);
    sq_engine_shutdown(&engine);
    printf("  PASS: Deeply nested JSON survived\n");

    remove(TEST_FILE);
    printf("\n=== FUZZ PROJECT LOAD: %d/%d passed (no crashes) ===\n", passed, total);
    return 0;
}
