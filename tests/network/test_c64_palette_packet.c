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

int main(void)
{
    uint8_t packet[C64_PALETTE_PACKET_SIZE] = {0};
    uint16_t generation = 0;
    uint32_t palette[16];

    packet[0] = 0x34;
    packet[1] = 0x12;
    packet[2] = 0xCD; // Frame is intentionally unspecified for software packets.
    packet[3] = 0xAB;
    packet[4] = 239;
    packet[6] = 0x80;
    packet[7] = 0x01;
    packet[8] = 1;
    packet[9] = 4;
    packet[10] = 1;
    for (size_t i = 0; i < 16; i++) {
        packet[C64_VIDEO_HEADER_SIZE + i * 3] = (uint8_t)i;
        packet[C64_VIDEO_HEADER_SIZE + i * 3 + 1] = (uint8_t)(i + 16);
        packet[C64_VIDEO_HEADER_SIZE + i * 3 + 2] = (uint8_t)(i + 32);
    }

    assert(c64_parse_palette_packet(packet, sizeof(packet), &generation, palette));
    assert(generation == 0x1234);
    assert(palette[0] == 0xFF201000u);
    assert(palette[15] == 0xFF2F1F0Fu);

    const size_t format_offsets[] = {4, 5, 6, 7, 8, 9, 10, 11};
    for (size_t i = 0; i < sizeof(format_offsets) / sizeof(format_offsets[0]); i++) {
        const size_t offset = format_offsets[i];
        packet[offset] ^= 0x01;
        assert(!c64_parse_palette_packet(packet, sizeof(packet), &generation, palette));
        packet[offset] ^= 0x01;
    }
    assert(!c64_parse_palette_packet(packet, sizeof(packet) - 1, &generation, palette));
    assert(!c64_parse_palette_packet(packet, sizeof(packet) + 1, &generation, palette));
    assert(!c64_parse_palette_packet(NULL, sizeof(packet), &generation, palette));
    assert(!c64_parse_palette_packet(packet, sizeof(packet), NULL, palette));
    assert(!c64_parse_palette_packet(packet, sizeof(packet), &generation, NULL));

    assert(c64_palette_generation_is_newer(11, 10));
    assert(!c64_palette_generation_is_newer(10, 10));
    assert(!c64_palette_generation_is_newer(9, 10));
    assert(c64_palette_generation_is_newer(0, 0xFFFF));
    assert(c64_palette_generation_is_newer(1, 0xFFFF));
    assert(!c64_palette_generation_is_newer(0xFFFF, 0));
    assert(!c64_palette_generation_is_newer(0x8000, 0));

    struct c64_palette_state state = {0};
    assert(c64_palette_state_accept(&state, 10, palette));
    assert(state.colors_valid && state.ordering_valid && state.generation == 10);
    assert(!c64_palette_state_accept(&state, 10, palette));
    assert(!c64_palette_state_accept(&state, 9, palette));
    state.generation = 0xFFFF;
    assert(c64_palette_state_accept(&state, 0, palette));
    state.ordering_valid = false;
    assert(c64_palette_state_accept(&state, 0, palette));

    puts("test_c64_palette_packet: PASS");
    return 0;
}
