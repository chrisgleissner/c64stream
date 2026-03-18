/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_EFFECT_AFTERGLOW_H
#define C64_EFFECT_AFTERGLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct c64_afterglow {
    int duration_ms;
    int curve;

    uint32_t *accum;
    size_t accum_bytes;
    bool accum_valid;

    float cached_decay_r;
    float cached_decay_g;
    float cached_decay_b;
    float cached_dt_ms;
    int cached_duration_ms;
    int cached_curve;
    bool decay_cache_valid;
};

void c64_afterglow_init(struct c64_afterglow *ag);
void c64_afterglow_reset(struct c64_afterglow *ag);
void c64_afterglow_free(struct c64_afterglow *ag);

float c64_afterglow_nominal_dt_ms(uint64_t frame_interval_ns, double expected_fps);

const uint32_t *c64_afterglow_apply(struct c64_afterglow *ag, const uint32_t *curr_pixels, size_t pixel_count,
                                    float dt_ms);

#endif // C64_EFFECT_AFTERGLOW_H
