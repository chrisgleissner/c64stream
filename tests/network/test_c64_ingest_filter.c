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

int main(void)
{
    RUN_TEST(accepts_all_when_expected_peer_unknown);
    RUN_TEST(accepts_expected_peer_drops_others);
    RUN_TEST(null_safe_accepts);
    return 0;
}
