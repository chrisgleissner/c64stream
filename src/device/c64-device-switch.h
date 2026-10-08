/* Bookkeeping for device switches: the queue of devices switched away from
 * that still have to be told to stop. Pure data structure with no I/O, so its
 * edge cases (switching back, repeated switches, overflow, give-up) are unit
 * tested on every platform; the retry worker in c64-source.c drives it. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define C64_STOP_QUEUE_MAX 4
#define C64_STOP_QUEUE_HOST_MAX 64
#define C64_STOP_QUEUE_PASSWORD_MAX 256

typedef struct {
    char host[C64_STOP_QUEUE_HOST_MAX];
    uint32_t control_port;
    char password[C64_STOP_QUEUE_PASSWORD_MAX];
    uint64_t rest_demoted_until_ns;
    uint32_t attempts;
} c64_stop_entry_t;

typedef struct {
    c64_stop_entry_t entries[C64_STOP_QUEUE_MAX];
    size_t count;
} c64_stop_queue_t;

/* Entries are keyed by host and control port: two devices can share an address
 * behind port forwarding, or in tests.
 *
 * Queues host to be stopped. A host already queued is refreshed (attempts
 * reset) rather than duplicated. When the queue is full the oldest entry is
 * dropped and reported through dropped_host (may be NULL). Returns false for an
 * empty host or the receive-only host 0.0.0.0, which have nothing to stop. */
bool c64_stop_queue_add(c64_stop_queue_t *queue, const char *host, uint32_t control_port, const char *password,
                        uint64_t rest_demoted_until_ns, char *dropped_host, size_t dropped_host_size);
/* Removes host:control_port from the queue, e.g. because the source switched
 * back to it and a stop arriving after the new start would end the stream. */
bool c64_stop_queue_cancel(c64_stop_queue_t *queue, const char *host, uint32_t control_port);
void c64_stop_queue_remove_at(c64_stop_queue_t *queue, size_t index);
/* Records a failed stop attempt; returns true when the entry has used up
 * max_attempts and should be given up on. */
bool c64_stop_queue_record_failure(c64_stop_queue_t *queue, size_t index, uint32_t max_attempts);
