/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_EFFECT_GEOMETRY_H
#define C64_EFFECT_GEOMETRY_H

#include <obs-module.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct c64_effect_geometry {
    uint32_t logical_width;
    uint32_t logical_height;
    uint32_t virtual_width;
    uint32_t virtual_height;
    uint32_t reported_width;
    uint32_t reported_height;
    uint32_t draw_width;
    uint32_t draw_height;
    bool preserve_size;
};

void c64_effect_get_scanline_scaling_info(float scan_line_distance, uint32_t *total_pixels, uint32_t *scanline_pixels);
uint32_t c64_effect_scale_dimension(uint32_t base, float pixel_scale, float scan_line_distance);
void c64_effect_geometry_init(struct c64_effect_geometry *geometry, uint32_t logical_width, uint32_t logical_height,
                              float pixel_width, float pixel_height, float scan_line_distance, bool preserve_size);
bool c64_effect_settings_resolve_preserve_size(obs_data_t *settings, const char *const *saved_setting_keys,
                                               size_t saved_setting_key_count);

#endif // C64_EFFECT_GEOMETRY_H
