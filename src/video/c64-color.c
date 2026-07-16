/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-color.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// C64STR-014: no process-wide palette or LUT state.  Every source owns its own
// `struct c64_color_lut`; the helpers below build and publish it per instance.

void c64_color_build_lut(uint64_t *lut, const uint32_t *palette)
{
    // Pre-compute all 256 possible 4-bit color pair combinations.
    for (int i = 0; i < 256; i++) {
        uint8_t color1 = i & 0x0F;        // Lower 4 bits
        uint8_t color2 = (i >> 4) & 0x0F; // Upper 4 bits

        // Pack two 32-bit colors into a single 64-bit value for efficient memory writes
        // This allows writing both pixels with a single 64-bit store operation
        lut[i] = ((uint64_t)palette[color2] << 32) | palette[color1];
    }
}

void c64_color_lut_init(struct c64_color_lut *lut, const uint32_t *palette)
{
    memcpy(lut->palette, palette, sizeof(lut->palette));
    c64_color_build_lut(lut->lut, palette);
}

void c64_color_lut_update(struct c64_color_lut *lut, const uint32_t *palette)
{
    memcpy(lut->palette, palette, sizeof(lut->palette));
    c64_color_build_lut(lut->lut, palette);
}

void c64_color_lut_snapshot(const struct c64_color_lut *lut, uint64_t *out)
{
    memcpy(out, lut->lut, sizeof(lut->lut));
}

void c64_convert_pixels_optimized(const uint64_t *lut, const uint8_t *src, uint32_t *dst, int pixel_pairs)
{
    // Process pixel pairs using optimized lookup table
    // Each src byte contains 2 pixels (4 bits each)
    // Each dst position gets 2 consecutive 32-bit RGBA values
    // LUT reads are lock-free: 64-bit reads are atomic on x86_64

    int i = 0;

    // Unrolled loop: process 4 pixel pairs (8 pixels) per iteration
    // This improves instruction-level parallelism and reduces loop overhead
    for (; i + 4 <= pixel_pairs; i += 4) {
        // Pre-fetch LUT entries (helps CPU pipelining)
        uint8_t pp0 = src[i];
        uint8_t pp1 = src[i + 1];
        uint8_t pp2 = src[i + 2];
        uint8_t pp3 = src[i + 3];

        // Lookup and store in batches
        uint64_t *dst64 = (uint64_t *)(dst + i * 2);
        dst64[0] = lut[pp0];
        dst64[1] = lut[pp1];
        dst64[2] = lut[pp2];
        dst64[3] = lut[pp3];
    }

    // Handle remaining pixel pairs
    for (; i < pixel_pairs; i++) {
        uint8_t pixel_pair = src[i];
        *(uint64_t *)(dst + i * 2) = lut[pixel_pair];
    }
}
