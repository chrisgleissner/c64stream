/* C64 Stream - C64STR-028 / C64STR-029 regression.
 *
 * All CRT-effect parameters are sanitised at a single choke point before they
 * size textures or scale geometry: values are clamped to their valid range and
 * non-finite (NaN/inf) inputs collapse to a safe fallback. pixel_width/height
 * fall back to 1.0 so an unset value is "no scaling" rather than a degenerate
 * 0 (C64STR-028). */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-effect-clamp.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    /* C64STR-028: the fallback (1.0 for pixel scale) applies to non-finite
     * values; a finite 0.0 is simply clamped up to the 0.5 minimum. */
    assert(c64_clamp_effect_float(0.0f, 0.5f, 4.0f, 1.0f) == 0.5f);
    assert(c64_clamp_effect_float(NAN, 0.5f, 4.0f, 1.0f) == 1.0f);
    assert(c64_clamp_effect_float(INFINITY, 0.5f, 4.0f, 1.0f) == 1.0f);
    assert(c64_clamp_effect_float(-INFINITY, 0.5f, 4.0f, 1.0f) == 1.0f);

    /* C64STR-029: range clamping for the effect parameters. */
    assert(c64_clamp_effect_float(10.0f, 0.5f, 4.0f, 1.0f) == 4.0f); /* pixel scale max */
    assert(c64_clamp_effect_float(-3.0f, 0.5f, 4.0f, 1.0f) == 0.5f); /* pixel scale min */
    assert(c64_clamp_effect_float(2.0f, 0.5f, 4.0f, 1.0f) == 2.0f);  /* in range */
    assert(c64_clamp_effect_float(5.0f, 0.0f, 1.0f, 0.0f) == 1.0f);  /* strength max */
    assert(c64_clamp_effect_float(-1.0f, 0.0f, 1.0f, 0.0f) == 0.0f); /* strength min */
    assert(c64_clamp_effect_float(NAN, 0.0f, 1.0f, 0.0f) == 0.0f);   /* NaN strength -> 0 */

    /* Integer parameters (afterglow duration/curve, tint mode). */
    assert(c64_clamp_effect_int(99999, 0, 3000) == 3000);
    assert(c64_clamp_effect_int(-5, 0, 3000) == 0);
    assert(c64_clamp_effect_int(1500, 0, 3000) == 1500);
    assert(c64_clamp_effect_int(7, 0, 3) == 3);
    assert(c64_clamp_effect_int(-1, 0, 3) == 0);

    printf("test_effect_clamp: PASS\n");
    return 0;
}
