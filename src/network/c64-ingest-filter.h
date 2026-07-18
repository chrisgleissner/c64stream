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
           (context->expected_peer_alt_ip_set && from->sin_addr.s_addr == context->expected_peer_alt_ip);
}

#endif // C64_INGEST_FILTER_H
