/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-ingest-filter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// Provided by the plugin; redefined here so the test links without plugin-main.c.
bool c64_debug_logging = false;

#define TEST(name) static void name(void)
#define RUN_TEST(name)                                                                                                 \
    do {                                                                                                               \
        printf("Running test: %s ... ", #name);                                                                        \
        name();                                                                                                        \
        printf("OK\n");                                                                                                \
    } while (0)

static struct sockaddr_in make_sender(const char *ip)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr.sin_addr);
    return addr;
}

// Fails open: when no expected peer is known, every packet is accepted. This is
// critical — the filter must never black out a working stream.
TEST(accepts_all_when_expected_peer_unknown)
{
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    assert(!ctx.expected_peer_ip_set); // zero-initialised => not set

    struct sockaddr_in a = make_sender("10.0.0.1");
    struct sockaddr_in b = make_sender("10.0.0.2");
    assert(c64_packet_from_expected_peer(&ctx, &a));
    assert(c64_packet_from_expected_peer(&ctx, &b));
}

TEST(accepts_expected_peer_drops_others)
{
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));

    struct in_addr expected;
    inet_pton(AF_INET, "192.168.1.64", &expected);
    ctx.expected_peer_ip = expected.s_addr;
    ctx.expected_peer_ip_set = true;

    struct sockaddr_in good = make_sender("192.168.1.64");
    struct sockaddr_in rogue = make_sender("192.168.1.99");

    assert(c64_packet_from_expected_peer(&ctx, &good));
    assert(!c64_packet_from_expected_peer(&ctx, &rogue));
}

TEST(accepts_verified_alternate_peer)
{
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    struct in_addr primary;
    struct in_addr alternate;
    inet_pton(AF_INET, "192.168.1.15", &primary);
    inet_pton(AF_INET, "192.168.1.167", &alternate);
    ctx.expected_peer_ip = primary.s_addr;
    ctx.expected_peer_ip_set = true;
    ctx.expected_peer_alt_ip = alternate.s_addr;
    ctx.expected_peer_alt_ip_set = true;

    struct sockaddr_in peer = make_sender("192.168.1.167");
    struct sockaddr_in rogue = make_sender("192.168.1.99");
    assert(c64_packet_from_expected_peer(&ctx, &peer));
    assert(!c64_packet_from_expected_peer(&ctx, &rogue));
}

TEST(null_safe_accepts)
{
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.expected_peer_ip_set = true;
    struct sockaddr_in good = make_sender("192.168.1.64");
    // Defensive: a NULL context or address must not drop packets.
    assert(c64_packet_from_expected_peer(NULL, &good));
    assert(c64_packet_from_expected_peer(&ctx, NULL));
}

// Device switch handover: the previous device's picture stays live until the
// new device's first packet, then the previous device is dropped for good.
TEST(handover_admits_previous_device_until_new_device_arrives)
{
    static struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    const struct sockaddr_in old_device = make_sender("192.168.1.13");
    const struct sockaddr_in new_device = make_sender("192.168.1.146");
    const struct sockaddr_in stranger = make_sender("192.168.1.99");
    ctx.expected_peer_ip = new_device.sin_addr.s_addr;
    ctx.expected_peer_ip_set = true;
    ctx.handover_peer_ip = old_device.sin_addr.s_addr;
    ctx.handover_peer_ip_set = true;

    assert(c64_packet_admit(&ctx, &old_device));
    assert(c64_packet_admit(&ctx, &old_device));
    assert(ctx.handover_peer_ip_set);
    assert(!c64_packet_admit(&ctx, &stranger)); // anyone else is still dropped
    assert(ctx.rejected_peer_ip == stranger.sin_addr.s_addr);

    assert(c64_packet_admit(&ctx, &new_device)); // first packet of the new device
    assert(!ctx.handover_peer_ip_set);
    assert(!c64_packet_admit(&ctx, &old_device)); // the two streams never interleave
    assert(ctx.rejected_peer_ip == old_device.sin_addr.s_addr);
    assert(c64_packet_admit(&ctx, &new_device));
}

// While the new host is still unresolved the filter fails open; the handover
// must not be closed by a packet that only passed because of that.
TEST(handover_kept_while_expected_peer_unknown)
{
    static struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    const struct sockaddr_in old_device = make_sender("192.168.1.13");
    const struct sockaddr_in other = make_sender("192.168.1.146");
    ctx.handover_peer_ip = old_device.sin_addr.s_addr;
    ctx.handover_peer_ip_set = true;
    assert(c64_packet_admit(&ctx, &other));
    assert(ctx.handover_peer_ip_set);
}

// A sender verified as the same device (learned peer) counts as the new device.
TEST(learned_peer_admitted_and_closes_handover)
{
    static struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    const struct sockaddr_in wifi = make_sender("192.168.1.129");
    const struct sockaddr_in wired = make_sender("192.168.1.146");
    const struct sockaddr_in old_device = make_sender("192.168.1.13");
    ctx.expected_peer_ip = wifi.sin_addr.s_addr;
    ctx.expected_peer_ip_set = true;
    assert(!c64_packet_admit(&ctx, &wired));
    ctx.learned_peer_ip = wired.sin_addr.s_addr;
    ctx.learned_peer_ip_set = true;
    ctx.handover_peer_ip = old_device.sin_addr.s_addr;
    ctx.handover_peer_ip_set = true;
    assert(c64_packet_admit(&ctx, &wired));
    assert(!ctx.handover_peer_ip_set);
}

// Processor side: previous-device packets still queued when the new device's
// first packet arrives are dropped, and the first new packet requests a flush
// of what was already reordered or partially assembled.
TEST(handover_cutover_drops_queued_previous_device_packets)
{
    static struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    const struct sockaddr_in old_device = make_sender("192.168.1.13");
    const struct sockaddr_in new_device = make_sender("192.168.1.146");
    ctx.expected_peer_ip = new_device.sin_addr.s_addr;
    ctx.expected_peer_ip_set = true;
    ctx.handover_peer_ip = old_device.sin_addr.s_addr;
    ctx.handover_peer_ip_set = true;

    // Receive side: two old packets queued (tagged), then the new device arrives.
    const bool old_tag = c64_packet_from_handover(&ctx, &old_device);
    assert(old_tag && c64_packet_admit(&ctx, &old_device));
    const bool new_tag = c64_packet_from_handover(&ctx, &new_device);
    assert(!new_tag && c64_packet_admit(&ctx, &new_device));
    assert(ctx.handover_flush_pending);

    // Processor side, in queue order: the old packet is dropped, the new one
    // flushes once and is kept, later new packets are kept without a flush.
    bool flush = true;
    assert(c64_handover_should_drop(&ctx, old_tag, &flush) && !flush);
    assert(!c64_handover_should_drop(&ctx, new_tag, &flush) && flush);
    assert(!c64_handover_should_drop(&ctx, false, &flush) && !flush);

    // While the handover is still open, tagged packets are kept.
    ctx.handover_peer_ip_set = true;
    assert(!c64_handover_should_drop(&ctx, true, &flush) && !flush);
}

int main(void)
{
    RUN_TEST(accepts_all_when_expected_peer_unknown);
    RUN_TEST(accepts_expected_peer_drops_others);
    RUN_TEST(accepts_verified_alternate_peer);
    RUN_TEST(null_safe_accepts);
    RUN_TEST(handover_admits_previous_device_until_new_device_arrives);
    RUN_TEST(handover_kept_while_expected_peer_unknown);
    RUN_TEST(learned_peer_admitted_and_closes_handover);
    RUN_TEST(handover_cutover_drops_queued_previous_device_packets);
    return 0;
}
