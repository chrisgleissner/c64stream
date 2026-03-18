/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_NETWORK_FIFO_H
#define C64_NETWORK_FIFO_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <util/threading.h>

#include "c64-protocol.h"

// Stage-1 UDP network FIFO:
// - Single producer (socket recv thread), single consumer (buffering/processing thread)
// - Unordered FIFO; no sorting, no per-packet logging, no blocking
// - Preallocated entries; fixed max packet size

#ifndef C64_NETWORK_FIFO_PACKET_MAX_SIZE
#define C64_NETWORK_FIFO_PACKET_MAX_SIZE C64_VIDEO_PACKET_SIZE
#endif

struct c64_network_fifo_packet {
    uint64_t timestamp_ns;
    uint16_t size;
    uint8_t data[C64_NETWORK_FIFO_PACKET_MAX_SIZE];
};

struct c64_network_fifo {
    struct c64_network_fifo_packet *entries;
    uint32_t capacity;
    volatile long head;
    volatile long tail;
    volatile long dropped_full;
};

static inline void c64_network_fifo_reset(struct c64_network_fifo *fifo)
{
    if (!fifo) {
        return;
    }

    os_atomic_set_long(&fifo->head, 0);
    os_atomic_set_long(&fifo->tail, 0);
    os_atomic_set_long(&fifo->dropped_full, 0);
}

static inline bool c64_network_fifo_push(struct c64_network_fifo *fifo, const uint8_t *data, uint16_t size,
                                         uint64_t timestamp_ns)
{
    if (!fifo || !fifo->entries || !data || fifo->capacity < 2) {
        return false;
    }

    if (size > C64_NETWORK_FIFO_PACKET_MAX_SIZE) {
        return false;
    }

    const long head = os_atomic_load_long(&fifo->head);
    const long tail = os_atomic_load_long(&fifo->tail);

    const long next_head = (head + 1) % (long)fifo->capacity;
    if (next_head == tail) {
        os_atomic_inc_long(&fifo->dropped_full);
        return false;
    }

    struct c64_network_fifo_packet *slot = &fifo->entries[(uint32_t)head];
    slot->timestamp_ns = timestamp_ns;
    slot->size = size;
    memcpy(slot->data, data, size);

    os_atomic_set_long(&fifo->head, next_head);
    return true;
}

static inline struct c64_network_fifo_packet *c64_network_fifo_peek(struct c64_network_fifo *fifo)
{
    if (!fifo || !fifo->entries) {
        return NULL;
    }

    const long head = os_atomic_load_long(&fifo->head);
    const long tail = os_atomic_load_long(&fifo->tail);

    if (head == tail) {
        return NULL;
    }

    return &fifo->entries[(uint32_t)tail];
}

static inline void c64_network_fifo_commit_pop(struct c64_network_fifo *fifo)
{
    if (!fifo) {
        return;
    }

    const long tail = os_atomic_load_long(&fifo->tail);
    const long next_tail = (tail + 1) % (long)fifo->capacity;
    os_atomic_set_long(&fifo->tail, next_tail);
}

static inline uint32_t c64_network_fifo_size_approx(struct c64_network_fifo *fifo)
{
    if (!fifo) {
        return 0;
    }

    const long head = os_atomic_load_long(&fifo->head);
    const long tail = os_atomic_load_long(&fifo->tail);

    if (head >= tail) {
        return (uint32_t)(head - tail);
    }

    return (uint32_t)((long)fifo->capacity - tail + head);
}

#endif // C64_NETWORK_FIFO_H
