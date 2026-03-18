/*
 * log.c — Logging runtime state.
 */

#include "core/log.h"

/* Default: warnings in release, everything in debug builds */
#ifdef NDEBUG
sq_log_level_t g_sq_log_level    = SQ_LOG_WARN;
#else
sq_log_level_t g_sq_log_level    = SQ_LOG_INFO;
#endif
double         g_sq_log_start_ms = 0.0;

void sq_log_init(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    g_sq_log_start_ms = now.QuadPart * 1000.0 / freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    g_sq_log_start_ms = ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
#endif
}

void sq_log_set_level(sq_log_level_t level)
{
    g_sq_log_level = level;
}
