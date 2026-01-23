/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_STREAM_EFFECTS_H
#define C64_STREAM_EFFECTS_H

#include <graphics/graphics.h>
#include <obs-module.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "c64-effect-afterglow.h"

struct c64_stream_effects {
    obs_source_t *source;

    float scan_line_distance;
    float scan_line_strength;
    float pixel_width;
    float pixel_height;
    float blur_strength;
    float bloom_strength;
    bool bloom_enable;
    bool afterglow_enable;
    int tint_mode;
    float tint_strength;
    bool tint_enable;

    struct c64_afterglow afterglow;

    bool cpu_scanlines_enabled;
    uint64_t last_frame_timestamp_ns;
    uint64_t last_input_frame_timestamp_ns;
    uint64_t last_processed_frame_time_ns;
    uint64_t last_processed_wall_time_ns;
    uint64_t synthetic_frame_time_ns;
    uint64_t afterglow_last_tick_ns;
    float afterglow_dt_ms;
    uint64_t frame_interval_ns;
    float expected_fps;
    uint32_t input_width;
    uint32_t input_height;

    uint32_t *cpu_input;
    uint32_t *cpu_output;
    size_t cpu_bytes;

    gs_texrender_t *input_texrender;
    gs_stagesurf_t *stage_surface;
    uint32_t stage_width;
    uint32_t stage_height;

    gs_texture_t *output_texture;
    uint32_t output_texture_width;
    uint32_t output_texture_height;

    gs_effect_t *crt_effect;
    gs_samplerstate_t *point_sampler;
};

const char *c64_stream_effects_get_name(void *unused);
void *c64_stream_effects_create(obs_data_t *settings, obs_source_t *source);
void c64_stream_effects_destroy(void *data);
void c64_stream_effects_update(void *data, obs_data_t *settings);
obs_properties_t *c64_stream_effects_properties(void *data);
void c64_stream_effects_defaults(obs_data_t *settings);
void c64_stream_effects_video_render(void *data, gs_effect_t *effect);
uint32_t c64_stream_effects_get_width(void *data);
uint32_t c64_stream_effects_get_height(void *data);

void c64_stream_effects_process_frame(struct c64_stream_effects *state, const uint32_t *input_rgba, uint32_t width,
                                      uint32_t height, uint64_t timestamp_ns, uint32_t *output_rgba);

#endif // C64_STREAM_EFFECTS_H
