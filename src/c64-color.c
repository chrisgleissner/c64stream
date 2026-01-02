/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-color.h"
#include "c64-logging.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Current active palette (initialized to default, updated by palette system)
uint32_t c64_current_palette[16];

// Pre-computed lookup table for pixel pairs
// 64-bit writes to individual entries are atomic on x86_64 (8-byte aligned)
// No locks needed: palette writes directly, rendering reads directly
uint64_t c64_color_pair_lut[256];
bool c64_color_lut_initialized = false;

void c64_init_color_conversion_lut(void)
{
    if (c64_color_lut_initialized) {
        return; // Already initialized
    }

    // Initialize c64_current_palette to default on first call
    memcpy(c64_current_palette, c64_default_palette, sizeof(c64_current_palette));
    C64_LOG_INFO("🎨 COLOR: Initialized default palette from build-time generated header (source: "
                 "data/palettes/default.vpl)");

    // Pre-compute all 256 possible 4-bit color pair combinations
    for (int i = 0; i < 256; i++) {
        uint8_t color1 = i & 0x0F;        // Lower 4 bits
        uint8_t color2 = (i >> 4) & 0x0F; // Upper 4 bits

        // Pack two 32-bit colors into a single 64-bit value for efficient memory writes
        // This allows writing both pixels with a single 64-bit store operation
        uint64_t packed_colors = ((uint64_t)c64_current_palette[color2] << 32) | c64_current_palette[color1];
        c64_color_pair_lut[i] = packed_colors;
    }

    c64_color_lut_initialized = true;
    C64_LOG_INFO("🎨 COLOR: Color conversion lookup table initialized (256 entries)");
}

void c64_convert_pixels_optimized(const uint8_t *src, uint32_t *dst, int pixel_pairs)
{
    // Ensure LUT is initialized
    if (!c64_color_lut_initialized) {
        c64_init_color_conversion_lut();
    }

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
        dst64[0] = c64_color_pair_lut[pp0];
        dst64[1] = c64_color_pair_lut[pp1];
        dst64[2] = c64_color_pair_lut[pp2];
        dst64[3] = c64_color_pair_lut[pp3];
    }

    // Handle remaining pixel pairs
    for (; i < pixel_pairs; i++) {
        uint8_t pixel_pair = src[i];
        *(uint64_t *)(dst + i * 2) = c64_color_pair_lut[pixel_pair];
    }
}
