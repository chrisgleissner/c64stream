/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_EFFECT_CLAMP_H
#define C64_EFFECT_CLAMP_H

#include <math.h>

/*
 * C64STR-028 / C64STR-029: single choke point for CRT-effect parameter
 * sanitisation. Every effect value flowing in from OBS settings, imports, or a
 * C64Script OP_EFFECTPARAM is clamped to its valid range here, and non-finite
 * values (NaN/inf) collapse to a safe fallback so they can never size a texture
 * or scale geometry. pixel_width/height fall back to 1.0 so an unset value
 * behaves as "no scaling" rather than a degenerate 0.
 */

/** Clamp a float to [minimum, maximum]; non-finite values become @p fallback. */
static inline float c64_clamp_effect_float(float value, float minimum, float maximum, float fallback)
{
    if (!isfinite(value)) {
        return fallback;
    }
    return fmaxf(minimum, fminf(value, maximum));
}

/** Clamp an int to [minimum, maximum]. */
static inline int c64_clamp_effect_int(int value, int minimum, int maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

#endif // C64_EFFECT_CLAMP_H
