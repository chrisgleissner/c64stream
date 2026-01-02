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

// VIC-II color palette (16 colors) in BGRA format for OBS (default/reference)
extern const uint32_t vic_colors[16];

// Current active palette (used by LUT, modifiable by palette system)
extern uint32_t c64_current_palette[16];

// Color pair LUT (exported for palette system to rebuild)
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
