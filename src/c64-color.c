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

// VIC-II color palette (16 colors) in BGRA format for OBS Studio
// Colors converted from C64 Ultimate grab.py RGB values to BGRA with full alpha
const uint32_t vic_colors[16] = {
    0xFF000000, // 0: Black
    0xFFF7F7F7, // 1: White
    0xFF342F8D, // 2: Red
    0xFFCDD46A, // 3: Cyan
    0xFFA43598, // 4: Purple
    0xFF42B44C, // 5: Green
    0xFFB1292C, // 6: Blue
    0xFF5DEFEF, // 7: Yellow
    0xFF204E98, // 8: Orange
    0xFF00385B, // 9: Brown
    0xFF6D67D1, // 10: Pink
    0xFF4A4A4A, // 11: Dark Grey
    0xFF7B7B7B, // 12: Medium Grey
    0xFF93EF9F, // 13: Light Green
    0xFFEF6A6D, // 14: Light Blue
    0xFFB2B2B2  // 15: Light Grey
};

// Pre-computed lookup table for pixel pairs
static uint64_t color_pair_lut[256];
static bool color_lut_initialized = false;

void c64_init_color_conversion_lut(void)
{
    if (color_lut_initialized) {
        return; // Already initialized
    }

    // Pre-compute all 256 possible 4-bit color pair combinations
    for (int i = 0; i < 256; i++) {
        uint8_t color1 = i & 0x0F;        // Lower 4 bits
        uint8_t color2 = (i >> 4) & 0x0F; // Upper 4 bits

        // Pack two 32-bit colors into a single 64-bit value for efficient memory writes
        // This allows writing both pixels with a single 64-bit store operation
        uint64_t packed_colors = ((uint64_t)vic_colors[color2] << 32) | vic_colors[color1];
        color_pair_lut[i] = packed_colors;
    }

    color_lut_initialized = true;
    C64_LOG_INFO("🎨 Color conversion lookup table initialized (256 entries)");
}

void c64_convert_pixels_optimized(const uint8_t *src, uint32_t *dst, int pixel_pairs)
{
    // Ensure LUT is initialized
    if (!color_lut_initialized) {
        c64_init_color_conversion_lut();
    }

    // Process pixel pairs using optimized lookup table
    // Each src byte contains 2 pixels (4 bits each)
    // Each dst position gets 2 consecutive 32-bit RGBA values

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
        dst64[0] = color_pair_lut[pp0];
        dst64[1] = color_pair_lut[pp1];
        dst64[2] = color_pair_lut[pp2];
        dst64[3] = color_pair_lut[pp3];
    }

    // Handle remaining pixel pairs
    for (; i < pixel_pairs; i++) {
        uint8_t pixel_pair = src[i];
        *(uint64_t *)(dst + i * 2) = color_pair_lut[pixel_pair];
    }
}
