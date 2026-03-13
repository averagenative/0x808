/*
 * command_queue.c — Lock-free SPSC ring buffer implementation.
 *
 * Single-producer (GUI thread) single-consumer (audio thread).
 * Uses acquire/release memory ordering on the atomic indices to ensure
 * the command data is visible before the index update.
 */

#include "engine/command_queue.h"
#include <string.h>

void cmd_queue_init(sq_command_queue_t *q)
{
    memset(q->commands, 0, sizeof(q->commands));
    atomic_store(&q->write_idx, 0);
    atomic_store(&q->read_idx, 0);
}

bool cmd_queue_push(sq_command_queue_t *q, const sq_command_t *cmd)
{
    unsigned w = atomic_load_explicit(&q->write_idx, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&q->read_idx, memory_order_acquire);
    unsigned next = (w + 1) & (CMD_QUEUE_SIZE - 1);
    if (next == r) return false; /* full */
    q->commands[w] = *cmd;
    atomic_store_explicit(&q->write_idx, next, memory_order_release);
    return true;
}

bool cmd_queue_pop(sq_command_queue_t *q, sq_command_t *cmd)
{
    unsigned r = atomic_load_explicit(&q->read_idx, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&q->write_idx, memory_order_acquire);
    if (r == w) return false; /* empty */
    *cmd = q->commands[r];
    atomic_store_explicit(&q->read_idx, (r + 1) & (CMD_QUEUE_SIZE - 1), memory_order_release);
    return true;
}
