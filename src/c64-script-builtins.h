/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include "c64-script.h"

/**
 * Built-in functions
 *
 * PEEK, and future extensions.
 */

/**
 * PEEK(address) - Read memory via REST DMA
 * Returns numeric value or -1 on error
 */
bool c64script_builtin_peek(c64script_runtime_t *runtime, uint16_t address, double *out_value);

/**
 * RND() - Random number generator [0.0, 1.0)
 * Returns random double value
 */
bool c64script_builtin_rnd(c64script_runtime_t *runtime, double *out_value);

/**
 * RANDOMIZE seed - Seed the random number generator
 */
void c64script_builtin_randomize(unsigned int seed);

/**
 * ABS(value) - Absolute value
 */
bool c64script_builtin_abs(double value, double *out_value);

/**
 * SQR(value) - Square root
 */
bool c64script_builtin_sqr(double value, double *out_value);

/**
 * INT(value) - Truncate toward zero
 */
bool c64script_builtin_int(double value, double *out_value);

/**
 * STR$(value) / STR(value) - Convert number to string
 * Returns formatted string representation of number
 */
bool c64script_builtin_str(double value, char *out_string, size_t out_size);
