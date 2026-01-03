/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script.h"
#include "c64-script-builtins.h"
#include "c64-logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MACRO_LOG_PREFIX "[c64script-builtins] "

// Random number generator state (per runtime context)
static unsigned int rnd_seed = 1;

// ============================================================================
// Built-in Functions
// ============================================================================

bool c64script_builtin_peek(c64script_runtime_t *runtime, uint16_t address, double *out_value)
{
    if (!runtime || !out_value) {
        return false;
    }

    // TODO: Implement PEEK - read memory from Ultimate device via REST API
    // For now, return 0 as placeholder
    (void)address;
    *out_value = 0.0;
    blog(LOG_WARNING, "PEEK not yet implemented - returning 0");
    return true;
}

bool c64script_builtin_rnd(c64script_runtime_t *runtime, double *out_value)
{
    if (!runtime || !out_value) {
        return false;
    }

    // Simple LCG (Linear Congruential Generator) for now
    // This matches classic BASIC RND behavior
    rnd_seed = (rnd_seed * 1103515245 + 12345) & 0x7fffffff;
    *out_value = (double)rnd_seed / (double)0x7fffffff;
    return true;
}

void c64script_builtin_randomize(unsigned int seed)
{
    rnd_seed = seed;
}

bool c64script_builtin_abs(double value, double *out_value)
{
    if (!out_value) {
        return false;
    }
    *out_value = fabs(value);
    return true;
}

bool c64script_builtin_sqr(double value, double *out_value)
{
    if (!out_value) {
        return false;
    }
    *out_value = sqrt(value);
    return true;
}

bool c64script_builtin_int(double value, double *out_value)
{
    if (!out_value) {
        return false;
    }
    *out_value = floor(value);
    return true;
}
