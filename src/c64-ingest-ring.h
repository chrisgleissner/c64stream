/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_INGEST_RING_H
#define C64_INGEST_RING_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <util/threading.h>

#include "c64-protocol.h"

// Stage-1 UDP ingest ring buffer:
// - Single producer (socket recv thread), single consumer (buffering/processing thread)
// - Unordered FIFO; no sorting, no per-packet logging, no blocking
// - Preallocated entries; fixed max packet size

#ifndef C64_INGEST_PACKET_MAX_SIZE
#define C64_INGEST_PACKET_MAX_SIZE C64_VIDEO_PACKET_SIZE
#endif

struct c64_ingest_packet {
    uint64_t timestamp_ns;
    uint16_t size;
    uint8_t data[C64_INGEST_PACKET_MAX_SIZE];
};

struct c64_ingest_ring {
    struct c64_ingest_packet *entries;
    uint32_t capacity;
    volatile long head;
    volatile long tail;
    volatile long dropped_full;
};

static inline void c64_ingest_ring_reset(struct c64_ingest_ring *ring)
{
    if (!ring) {
        return;
    }

    os_atomic_set_long(&ring->head, 0);
    os_atomic_set_long(&ring->tail, 0);
    os_atomic_set_long(&ring->dropped_full, 0);
}

static inline bool c64_ingest_ring_push(struct c64_ingest_ring *ring, const uint8_t *data, uint16_t size,
                                        uint64_t timestamp_ns)
{
    if (!ring || !ring->entries || !data || ring->capacity < 2) {
        return false;
    }

    if (size > C64_INGEST_PACKET_MAX_SIZE) {
        return false;
    }

    const long head = os_atomic_load_long(&ring->head);
    const long tail = os_atomic_load_long(&ring->tail);

    const long next_head = (head + 1) % (long)ring->capacity;
    if (next_head == tail) {
        os_atomic_inc_long(&ring->dropped_full);
        return false;
    }

    struct c64_ingest_packet *slot = &ring->entries[(uint32_t)head];
    slot->timestamp_ns = timestamp_ns;
    slot->size = size;
    memcpy(slot->data, data, size);

    os_atomic_set_long(&ring->head, next_head);
    return true;
}

static inline struct c64_ingest_packet *c64_ingest_ring_peek(struct c64_ingest_ring *ring)
{
    if (!ring || !ring->entries) {
        return NULL;
    }

    const long head = os_atomic_load_long(&ring->head);
    const long tail = os_atomic_load_long(&ring->tail);

    if (head == tail) {
        return NULL;
    }

    return &ring->entries[(uint32_t)tail];
}

static inline void c64_ingest_ring_commit_pop(struct c64_ingest_ring *ring)
{
    if (!ring) {
        return;
    }

    const long tail = os_atomic_load_long(&ring->tail);
    const long next_tail = (tail + 1) % (long)ring->capacity;
    os_atomic_set_long(&ring->tail, next_tail);
}

static inline uint32_t c64_ingest_ring_size_approx(struct c64_ingest_ring *ring)
{
    if (!ring) {
        return 0;
    }

    const long head = os_atomic_load_long(&ring->head);
    const long tail = os_atomic_load_long(&ring->tail);

    if (head >= tail) {
        return (uint32_t)(head - tail);
    }

    return (uint32_t)((long)ring->capacity - tail + head);
}

#endif // C64_INGEST_RING_H
