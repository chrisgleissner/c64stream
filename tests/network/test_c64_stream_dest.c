/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST(name) static void name(void)
#define RUN_TEST(name)                                                                                                 \
    do {                                                                                                               \
        printf("Running test: %s ... ", #name);                                                                        \
        name();                                                                                                        \
        printf("OK\n");                                                                                                \
    } while (0)

TEST(builds_ip_port_string)
{
    char out[64];
    assert(c64_build_stream_dest(out, sizeof(out), "192.168.1.64", 21000));
    assert(strcmp(out, "192.168.1.64:21000") == 0);
}

TEST(port_boundaries)
{
    char out[64];
    assert(c64_build_stream_dest(out, sizeof(out), "10.0.0.1", 0));
    assert(strcmp(out, "10.0.0.1:0") == 0);
    assert(c64_build_stream_dest(out, sizeof(out), "10.0.0.1", 65535));
    assert(strcmp(out, "10.0.0.1:65535") == 0);
}

TEST(rejects_empty_and_null)
{
    char out[64];
    assert(!c64_build_stream_dest(out, sizeof(out), "", 21000));
    assert(!c64_build_stream_dest(out, sizeof(out), NULL, 21000));
    assert(!c64_build_stream_dest(NULL, sizeof(out), "1.2.3.4", 21000));
    assert(!c64_build_stream_dest(out, 0, "1.2.3.4", 21000));
}

TEST(rejects_hostnames_and_invalid_ipv4)
{
    char out[64];
    assert(!c64_build_stream_dest(out, sizeof(out), "studio.local", 21000));
    assert(!c64_build_stream_dest(out, sizeof(out), "192.168.1.256", 21000));
    assert(!c64_build_stream_dest(out, sizeof(out), "192.168.1", 21000));
    assert(!c64_build_stream_dest(out, sizeof(out), "192.168.1.1.", 21000));
}

TEST(rejects_truncation)
{
    // Buffer too small to hold the full "255.255.255.255:65535" result.
    char out[8];
    assert(!c64_build_stream_dest(out, sizeof(out), "255.255.255.255", 65535));
    // "1.2.3.4:1" is 9 chars -> needs size 10 (with NUL).
    char fit[10];
    assert(c64_build_stream_dest(fit, sizeof(fit), "1.2.3.4", 1));
    assert(strcmp(fit, "1.2.3.4:1") == 0);
    char too_small[9];
    assert(!c64_build_stream_dest(too_small, sizeof(too_small), "1.2.3.4", 1));
}

TEST(clamps_packet_derived_frame_height)
{
    assert(c64_clamp_frame_height(C64_NTSC_HEIGHT - 1) == C64_NTSC_HEIGHT);
    assert(c64_clamp_frame_height(C64_NTSC_HEIGHT) == C64_NTSC_HEIGHT);
    assert(c64_clamp_frame_height(245) == 245);
    assert(c64_clamp_frame_height(C64_PAL_HEIGHT) == C64_PAL_HEIGHT);
    assert(c64_clamp_frame_height(C64_PAL_HEIGHT + 1) == C64_PAL_HEIGHT);
}

int main(void)
{
    RUN_TEST(builds_ip_port_string);
    RUN_TEST(port_boundaries);
    RUN_TEST(rejects_empty_and_null);
    RUN_TEST(rejects_hostnames_and_invalid_ipv4);
    RUN_TEST(rejects_truncation);
    RUN_TEST(clamps_packet_derived_frame_height);
    return 0;
}
