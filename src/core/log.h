/*
 * log.h — Simple leveled logging with timestamps.
 *
 * Usage:
 *   LOG_INFO("Audio started: %s", device_name);
 *   LOG_DEBUG("Step %d triggered", step);
 *   LOG_WARN("Buffer underrun detected");
 *   LOG_ERROR("Failed to init device");
 *
 * Set minimum level at compile time:  -DSQ_LOG_LEVEL=SQ_LOG_DEBUG
 * Or at runtime:                      sq_log_set_level(SQ_LOG_WARN);
 *
 * Default level: SQ_LOG_DEBUG (shows everything).
 */

#ifndef SQ_LOG_H
#define SQ_LOG_H

#include <stdio.h>
#include <time.h>

/* Log levels */
typedef enum {
    SQ_LOG_DEBUG = 0,
    SQ_LOG_INFO  = 1,
    SQ_LOG_WARN  = 2,
    SQ_LOG_ERROR = 3,
    SQ_LOG_NONE  = 4
} sq_log_level_t;

/* Runtime log level — defined in log.c */
extern sq_log_level_t g_sq_log_level;
extern double         g_sq_log_start_ms;

void sq_log_init(void);
void sq_log_set_level(sq_log_level_t level);

/* Internal: get elapsed ms since init */
#ifdef _WIN32
#include <windows.h>
static inline double sq_log_elapsed_ms(void) {
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (now.QuadPart * 1000.0 / freq.QuadPart) - g_sq_log_start_ms;
}
#else
static inline double sq_log_elapsed_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6) - g_sq_log_start_ms;
}
#endif

/* Level labels */
static inline const char *sq_log_level_str(sq_log_level_t lvl) {
    switch (lvl) {
        case SQ_LOG_DEBUG: return "DEBUG";
        case SQ_LOG_INFO:  return "INFO ";
        case SQ_LOG_WARN:  return "WARN ";
        case SQ_LOG_ERROR: return "ERROR";
        default:           return "?????";
    }
}

/* Internal: format local time as HH:MM:SS */
static inline void sq_log_localtime(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, len, "%H:%M:%S", tm);
}

/*
 * Core logging macro.
 * Format: 14:23:05 [  123.4 ms] INFO  engine: Audio started
 */
#define SQ_LOG(level, tag, fmt, ...) do { \
    if ((level) >= g_sq_log_level) { \
        char _sq_timebuf[12]; \
        sq_log_localtime(_sq_timebuf, sizeof(_sq_timebuf)); \
        fprintf(stderr, "%s [%9.1f ms] %s %s: " fmt "\n", \
                _sq_timebuf, sq_log_elapsed_ms(), sq_log_level_str(level), \
                (tag), ##__VA_ARGS__); \
        fflush(stderr); \
    } \
} while(0)

/* Convenience macros — callers define LOG_TAG before using these */
#ifndef LOG_TAG
#define LOG_TAG "app"
#endif

#define LOG_DEBUG(fmt, ...) SQ_LOG(SQ_LOG_DEBUG, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  SQ_LOG(SQ_LOG_INFO,  LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  SQ_LOG(SQ_LOG_WARN,  LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) SQ_LOG(SQ_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)

#endif /* SQ_LOG_H */
