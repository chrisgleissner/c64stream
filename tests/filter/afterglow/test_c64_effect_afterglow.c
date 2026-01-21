/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

// Ensure asserts are always enabled in tests
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-effect-afterglow.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

bool c64_debug_logging = false;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                                                                 \
    do {                                                                                                               \
        printf("Running test: %s ... ", #name);                                                                        \
        fflush(stdout);                                                                                                \
        test_##name();                                                                                                 \
        printf("OK\n");                                                                                                \
    } while (0)

static uint32_t make_rgba(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000 | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static void assert_rgb(uint32_t pixel, uint8_t r, uint8_t g, uint8_t b)
{
    assert((pixel & 0xFF) == r);
    assert(((pixel >> 8) & 0xFF) == g);
    assert(((pixel >> 16) & 0xFF) == b);
}

static void run_impulse_curve_test(int curve, uint8_t exp_r, uint8_t exp_g, uint8_t exp_b)
{
    struct c64_afterglow ag;
    c64_afterglow_init(&ag);
    ag.duration_ms = 100;
    ag.curve = curve;

    uint32_t white = make_rgba(255, 255, 255);
    uint32_t black = make_rgba(0, 0, 0);

    const uint32_t *out = c64_afterglow_apply(&ag, &white, 1, 10.0f);
    assert(out != NULL);
    assert_rgb(out[0], 255, 255, 255);

    out = c64_afterglow_apply(&ag, &black, 1, 10.0f);
    assert(out != NULL);
    assert_rgb(out[0], exp_r, exp_g, exp_b);

    c64_afterglow_free(&ag);
}

TEST(impulse_decay)
{
    run_impulse_curve_test(2, 236, 230, 223);
}

TEST(per_channel_decay_ordering)
{
    struct c64_afterglow ag;
    c64_afterglow_init(&ag);
    ag.duration_ms = 100;
    ag.curve = 2;

    uint32_t white = make_rgba(255, 255, 255);
    uint32_t black = make_rgba(0, 0, 0);

    const uint32_t *out = c64_afterglow_apply(&ag, &white, 1, 10.0f);
    assert(out != NULL);
    out = c64_afterglow_apply(&ag, &black, 1, 10.0f);
    assert(out != NULL);

    uint8_t r = (uint8_t)(out[0] & 0xFF);
    uint8_t g = (uint8_t)((out[0] >> 8) & 0xFF);
    uint8_t b = (uint8_t)((out[0] >> 16) & 0xFF);
    assert(r > g);
    assert(g > b);

    c64_afterglow_free(&ag);
}

TEST(curve_modes)
{
    run_impulse_curve_test(0, 236, 229, 221);
    run_impulse_curve_test(1, 219, 208, 195);
    run_impulse_curve_test(2, 236, 230, 223);
    run_impulse_curve_test(3, 245, 242, 238);
}

TEST(dt_clamping_behavior)
{
    struct c64_afterglow ag;
    c64_afterglow_init(&ag);
    ag.duration_ms = 100;
    ag.curve = 2;

    uint32_t white = make_rgba(255, 255, 255);
    uint32_t black = make_rgba(0, 0, 0);

    const uint32_t *out = c64_afterglow_apply(&ag, &white, 1, 10.0f);
    assert(out != NULL);
    out = c64_afterglow_apply(&ag, &black, 1, 0.1f);
    assert(out != NULL);
    assert_rgb(out[0], 253, 252, 251);

    c64_afterglow_reset(&ag);
    out = c64_afterglow_apply(&ag, &white, 1, 10.0f);
    assert(out != NULL);
    out = c64_afterglow_apply(&ag, &black, 1, 200.0f);
    assert(out != NULL);
    assert_rgb(out[0], 121, 93, 67);

    c64_afterglow_free(&ag);
}

TEST(resize_invalidation)
{
    struct c64_afterglow ag;
    c64_afterglow_init(&ag);
    ag.duration_ms = 100;
    ag.curve = 2;

    uint32_t white = make_rgba(255, 255, 255);
    uint32_t black = make_rgba(0, 0, 0);

    const uint32_t *out = c64_afterglow_apply(&ag, &white, 1, 10.0f);
    assert(out != NULL);
    out = c64_afterglow_apply(&ag, &black, 1, 10.0f);
    assert(out != NULL);

    uint32_t resized[2] = {make_rgba(10, 20, 30), make_rgba(40, 50, 60)};
    out = c64_afterglow_apply(&ag, resized, 2, 10.0f);
    assert(out != NULL);
    assert_rgb(out[0], 10, 20, 30);
    assert_rgb(out[1], 40, 50, 60);
    assert(ag.accum_valid);
    assert(ag.accum_bytes == sizeof(resized));

    c64_afterglow_free(&ag);
}

TEST(determinism_across_frames)
{
    struct c64_afterglow ag_a;
    struct c64_afterglow ag_b;
    c64_afterglow_init(&ag_a);
    c64_afterglow_init(&ag_b);

    ag_a.duration_ms = 120;
    ag_a.curve = 2;
    ag_b.duration_ms = 120;
    ag_b.curve = 2;

    uint32_t frames[3][2] = {
        {make_rgba(255, 0, 0), make_rgba(0, 0, 0)},
        {make_rgba(0, 255, 0), make_rgba(0, 0, 0)},
        {make_rgba(0, 0, 255), make_rgba(0, 0, 0)},
    };

    float dts[3] = {16.0f, 16.0f, 32.0f};
    for (size_t i = 0; i < 3; i++) {
        const uint32_t *out_a = c64_afterglow_apply(&ag_a, frames[i], 2, dts[i]);
        const uint32_t *out_b = c64_afterglow_apply(&ag_b, frames[i], 2, dts[i]);
        assert(out_a != NULL);
        assert(out_b != NULL);
        assert(memcmp(out_a, out_b, 2 * sizeof(uint32_t)) == 0);
    }

    c64_afterglow_free(&ag_a);
    c64_afterglow_free(&ag_b);
}

int main(void)
{
    RUN_TEST(impulse_decay);
    RUN_TEST(per_channel_decay_ordering);
    RUN_TEST(curve_modes);
    RUN_TEST(dt_clamping_behavior);
    RUN_TEST(resize_invalidation);
    RUN_TEST(determinism_across_frames);
    return 0;
}
