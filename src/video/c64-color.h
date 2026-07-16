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

// Default VIC-II color palette (16 colors) in BGRA format for OBS Studio in case default.vpl is not found
// If updating default.vpl, manually update these values
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

// C64STR-014: per-source color-conversion state.
//
// Palette selection and the pixel-pair lookup table used to be process-wide
// globals, which coupled every OBS source instance: the last source to select
// a palette (or edit a color) rewrote the single shared LUT, so all sources
// rendered with that one palette and edits bled across instances.  Each source
// now owns a `struct c64_color_lut`; the render thread reads its own LUT and no
// global mutable palette state remains in the conversion path.
//
// Concurrency: palette edits (UI thread) can overlap frame conversion (video
// thread).  The struct itself is a plain POD; the owner (c64_source) serialises
// writers and per-frame reads with its palette_mutex.  A reader takes one
// snapshot of the whole table under the lock (once per frame, not per pixel)
// and converts against that local copy, so it never observes a half-rebuilt
// table and the hot per-pixel loop stays lock-free.
struct c64_color_lut {
    uint32_t palette[16]; // BGRA colors backing the current LUT
    uint64_t lut[256];    // Pixel-pair lookup entries (two packed BGRA pixels)
};

/**
 * @brief Build a 256-entry pixel-pair lookup table from a 16-colour palette.
 *
 * Pure helper: packs two BGRA colours into each 64-bit entry so pixel
 * conversion can emit two pixels per store.  Does not touch any shared state.
 *
 * @param lut Destination array of 256 uint64_t entries
 * @param palette Source array of 16 BGRA colours
 */
void c64_color_build_lut(uint64_t *lut, const uint32_t *palette);

/**
 * @brief Initialise a per-source color LUT from a palette (build in place).
 *
 * Caller must hold the owning source's palette lock (or otherwise guarantee no
 * concurrent reader) when the source is already live.
 */
void c64_color_lut_init(struct c64_color_lut *lut, const uint32_t *palette);

/**
 * @brief Rebuild a per-source color LUT from a new palette.
 *
 * Rebuilds @p lut in place.  Caller must hold the owning source's palette lock
 * so a concurrent snapshot never sees a partially-written table.
 */
void c64_color_lut_update(struct c64_color_lut *lut, const uint32_t *palette);

/**
 * @brief Copy the current lookup table into a caller-owned buffer.
 *
 * Taken once per frame under the owning source's palette lock; the caller then
 * releases the lock and runs the per-pixel conversion against @p out.
 *
 * @param lut Source per-source LUT
 * @param out Destination array of 256 uint64_t entries
 */
void c64_color_lut_snapshot(const struct c64_color_lut *lut, uint64_t *out);

/**
 * @brief Convert C64 pixel data to RGBA using a caller-provided lookup table
 *
 * Converts C64 pixel pairs (4 bits per pixel) to 32-bit RGBA values using
 * the given pre-computed lookup table. Each source byte contains 2 pixels, and
 * each pixel is converted to a 32-bit RGBA value.
 *
 * @param lut 256-entry pixel-pair lookup table (per-source, see c64_color_lut)
 * @param src Source pixel data (4-bit pairs)
 * @param dst Destination RGBA buffer (32-bit per pixel)
 * @param pixel_pairs Number of pixel pairs to convert (bytes to process)
 *
 * Performance: Processes 8 pixels per loop iteration using 64-bit packed writes
 * for optimal cache efficiency in high-frequency video processing (3400+ packets/sec).
 */
void c64_convert_pixels_optimized(const uint64_t *lut, const uint8_t *src, uint32_t *dst, int pixel_pairs);

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
