/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-effect-geometry.h"

void c64_effect_get_scanline_scaling_info(float scan_line_distance, uint32_t *total_pixels, uint32_t *scanline_pixels)
{
    if (!total_pixels || !scanline_pixels) {
        return;
    }

    if (scan_line_distance <= 0.25f) { // Tight
        *total_pixels = 5;             // spacing (Scanline, Gap): S1S1S1S1G1 S2S2S2S2G2 ...
        *scanline_pixels = 4;
    } else if (scan_line_distance <= 0.5f) { // Normal
        *total_pixels = 3;                   // spacing (Scanline, Gap): S1S1G1 S2S2G2 ...
        *scanline_pixels = 2;
    } else if (scan_line_distance <= 1.0f) { // Wide
        *total_pixels = 4;                   // spacing (Scanline, Gap): S1S1G1G1 S2S2G2G2 ...
        *scanline_pixels = 2;
    } else {               // Extra Wide (2.0f)
        *total_pixels = 3; // spacing (Scanline, Gap, Gap): S1G1G1 S2G2G2 ...
        *scanline_pixels = 1;
    }
}

uint32_t c64_effect_scale_dimension(uint32_t base, float pixel_scale, float scan_line_distance)
{
    float scale = pixel_scale;
    if (scan_line_distance > 0.0f) {
        uint32_t total_pixels = 0;
        uint32_t scanline_pixels = 0;
        c64_effect_get_scanline_scaling_info(scan_line_distance, &total_pixels, &scanline_pixels);
        scale *= (float)total_pixels;
    }

    return (uint32_t)((float)base * scale);
}

void c64_effect_geometry_init(struct c64_effect_geometry *geometry, uint32_t logical_width, uint32_t logical_height,
                              float pixel_width, float pixel_height, float scan_line_distance, bool preserve_size)
{
    if (!geometry) {
        return;
    }

    geometry->logical_width = logical_width;
    geometry->logical_height = logical_height;
    geometry->preserve_size = preserve_size;

    geometry->virtual_width = c64_effect_scale_dimension(logical_width, pixel_width, scan_line_distance);
    geometry->virtual_height = c64_effect_scale_dimension(logical_height, pixel_height, scan_line_distance);

    if (preserve_size) {
        geometry->reported_width = logical_width;
        geometry->reported_height = logical_height;
        geometry->draw_width = logical_width;
        geometry->draw_height = logical_height;
    } else {
        geometry->reported_width = geometry->virtual_width;
        geometry->reported_height = geometry->virtual_height;
        geometry->draw_width = geometry->virtual_width;
        geometry->draw_height = geometry->virtual_height;
    }
}

bool c64_effect_settings_resolve_preserve_size(obs_data_t *settings, const char *const *saved_setting_keys,
                                               size_t saved_setting_key_count)
{
    bool preserve_size = false;

    if (!settings) {
        return preserve_size;
    }

    if (obs_data_has_user_value(settings, "preserve_size")) {
        preserve_size = obs_data_get_bool(settings, "preserve_size");
    }

    UNUSED_PARAMETER(saved_setting_keys);
    UNUSED_PARAMETER(saved_setting_key_count);
    return preserve_size;
}
