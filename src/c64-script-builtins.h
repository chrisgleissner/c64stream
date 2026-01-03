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
