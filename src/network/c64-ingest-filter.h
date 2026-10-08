/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_INGEST_FILTER_H
#define C64_INGEST_FILTER_H

#include <stdbool.h>

// Pulls in the complete `struct c64_source` (with expected_peer_ip[_set]) and
// `struct sockaddr_in`. This header is intentionally self-contained so the
// inline ownership check below can be shared by both the video and audio
// receiver paths without each one re-implementing it (DRY).
#include "c64-types.h"

// Expands a network-byte-order IPv4 address into four %u arguments.
#define C64_IPV4_ARGS(ip)                                                                                              \
    (unsigned)(((const uint8_t *)&(ip))[0]), (unsigned)(((const uint8_t *)&(ip))[1]),                                  \
        (unsigned)(((const uint8_t *)&(ip))[2]), (unsigned)(((const uint8_t *)&(ip))[3])

/**
 * Ingest ownership filter (approach D).
 *
 * Returns true when a packet should be accepted: either because the expected
 * peer is not known (fail-open — never let this filter black out a working
 * stream), or because the packet's sender matches the expected peer IP.
 *
 * Both `ctx->expected_peer_ip` and `from->sin_addr.s_addr` are stored in
 * network byte order, so they compare directly.
 *
 * `context` is only read (its expected-peer fields); it is never modified here.
 * Callers own the diagnostic counter increment on mismatch.
 */
static inline bool c64_packet_from_expected_peer(const struct c64_source *context, const struct sockaddr_in *from)
{
    if (!context || !from) {
        return true; // Defensive: accept rather than drop on a programmer error.
    }
    if (!context->expected_peer_ip_set) {
        return true; // Fail open: unresolved DNS / non-IPv4 host -> accept, as today.
    }
    return from->sin_addr.s_addr == context->expected_peer_ip ||
           (context->expected_peer_alt_ip_set && from->sin_addr.s_addr == context->expected_peer_alt_ip) ||
           (context->learned_peer_ip_set && from->sin_addr.s_addr == context->learned_peer_ip);
}

/**
 * Records the sender of a packet the filter rejected, so the retry worker can
 * check whether it is the selected device's other network interface (see
 * c64_try_adopt_rejected_sender in c64-source.c). A single word store; the
 * receive path stays non-blocking.
 */
static inline void c64_note_rejected_peer(struct c64_source *context, const struct sockaddr_in *from)
{
    if (context && from) {
        context->rejected_peer_ip = from->sin_addr.s_addr;
    }
}

/**
 * Receive-path admission including the device-switch handover: packets from
 * the previous device are accepted until the first packet from the selected
 * device arrives, then dropped. The old picture keeps running while the new
 * device starts, and the two streams never interleave afterwards.
 */
static inline bool c64_packet_admit(struct c64_source *context, const struct sockaddr_in *from)
{
    if (c64_packet_from_expected_peer(context, from)) {
        if (context && from && context->handover_peer_ip_set && context->expected_peer_ip_set &&
            from->sin_addr.s_addr != context->handover_peer_ip) {
            // Published before the handover closes, so the processor sees
            // the flush request no later than the first new packet.
            context->handover_flush_pending = true;
            context->handover_peer_ip_set = false;
        }
        return true;
    }
    if (context && from && context->handover_peer_ip_set && from->sin_addr.s_addr == context->handover_peer_ip) {
        return true;
    }
    c64_note_rejected_peer(context, from);
    return false;
}

/**
 * True for an admitted packet that came from the previous device during a
 * switch handover. Such packets are tagged in the receive FIFO so the
 * processor can discard any that are still queued once the new device's first
 * packet has arrived (c64_handover_should_drop).
 */
static inline bool c64_packet_from_handover(const struct c64_source *context, const struct sockaddr_in *from)
{
    return context && from && context->handover_peer_ip_set && from->sin_addr.s_addr == context->handover_peer_ip;
}

/**
 * Processor-side cut-over. Returns true when a dequeued packet must be
 * dropped: it came from the previous device and the handover has closed.
 * Sets *flush when this is the first packet after the cut-over, so the caller
 * empties its reorder buffer and partial frame of previous-device data before
 * using it. Single consumer: only the processor thread calls this.
 */
static inline bool c64_handover_should_drop(struct c64_source *context, bool from_handover, bool *flush)
{
    *flush = false;
    if (from_handover) {
        return !context->handover_peer_ip_set;
    }
    if (context->handover_flush_pending) {
        context->handover_flush_pending = false;
        *flush = true;
    }
    return false;
}

#endif // C64_INGEST_FILTER_H
