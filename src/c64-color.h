/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_COLOR_H
#define C64_COLOR_H

#include <stdint.h>
#include <stdbool.h>

// Default VIC-II color palette (16 colors) in BGRA format for OBS Studio
// Source: data/palettes/default.vpl
// If updating default.vpl, manually update these values (happens ~once/year)
static const uint32_t c64_default_palette[16] = {
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
    0xFFB2B2B2, // 15: Light Grey
};

// Current active palette (used by LUT, modifiable by palette system)
extern uint32_t c64_current_palette[16];

// Pre-computed lookup table for pixel pairs
// Individual uint64_t writes are atomic on x86_64 (naturally 8-byte aligned)
// Lock-free: palette updates write directly, rendering reads directly
// Worst case: single frame sees partial old/new palette during update
extern uint64_t c64_color_pair_lut[256];
extern bool c64_color_lut_initialized;

/**
 * @brief Initialize the color conversion lookup table
 *
 * Pre-computes all 256 possible 4-bit color pair combinations into a lookup table
 * for optimized pixel conversion. This function is thread-safe and can be called
 * multiple times - subsequent calls are ignored.
 *
 * The lookup table packs two 32-bit RGBA colors into 64-bit values for efficient
 * memory operations during pixel conversion.
 */
void c64_init_color_conversion_lut(void);

/**
 * @brief Convert C64 pixel data to RGBA using optimized lookup table
 *
 * Converts C64 pixel pairs (4 bits per pixel) to 32-bit RGBA values using
 * a pre-computed lookup table. Each source byte contains 2 pixels, and each
 * pixel is converted to a 32-bit RGBA value.
 *
 * @param src Source pixel data (4-bit pairs)
 * @param dst Destination RGBA buffer (32-bit per pixel)
 * @param pixel_pairs Number of pixel pairs to convert (bytes to process)
 *
 * Performance: Processes 8 pixels per loop iteration using 64-bit packed writes
 * for optimal cache efficiency in high-frequency video processing (3400+ packets/sec).
 */
void c64_convert_pixels_optimized(const uint8_t *src, uint32_t *dst, int pixel_pairs);

/**
 * @brief Convert internal color format to OBS color format
 *
 * Our internal format is 0xFFBBGGRR (ABGR), same as OBS, so this is effectively
 * a no-op that just ensures alpha is 0xFF.
 *
 * @param color Internal color value (0xFFBBGGRR)
 * @return OBS color format (0xFFBBGGRR)
 */
static inline uint32_t c64_bgra_to_obs_color(uint32_t color)
{
    // Internal format is already 0xFFBBGGRR (same as OBS), just ensure alpha is set
    return (color & 0x00FFFFFF) | 0xFF000000;
}

#endif // C64_COLOR_H
