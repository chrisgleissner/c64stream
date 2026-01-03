/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include "c64-script.h"

/**
 * Runtime context management
 *
 * Manages variables, stacks, and execution state.
 */

/**
 * Create a runtime context
 */
c64script_runtime_t *c64script_runtime_create(void);

/**
 * Destroy a runtime context
 */
void c64script_runtime_destroy(c64script_runtime_t *runtime);

/**
 * Variable operations
 */
bool c64script_runtime_set_var(c64script_runtime_t *runtime, const char *name, c64script_value_t value);
bool c64script_runtime_get_var(c64script_runtime_t *runtime, const char *name, c64script_value_t *out_value);

/**
 * Stack operations
 */
bool c64script_runtime_push(c64script_runtime_t *runtime, c64script_value_t value);
bool c64script_runtime_pop(c64script_runtime_t *runtime, c64script_value_t *out_value);

/**
 * Value operations
 */
c64script_value_t c64script_value_number(double num);
c64script_value_t c64script_value_string(const char *str);
void c64script_value_free(c64script_value_t *value);
c64script_value_t c64script_value_clone(c64script_value_t value);
