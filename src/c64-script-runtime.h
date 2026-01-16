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
bool c64script_runtime_var_exists(c64script_runtime_t *runtime, const char *name);

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
c64script_value_t c64script_value_array(size_t size, c64script_value_type_t element_type);
c64script_value_t c64script_value_map(c64script_value_type_t value_type);
void c64script_value_free(c64script_value_t *value);
c64script_value_t c64script_value_clone(c64script_value_t value);

/**
 * Array operations
 */
bool c64script_array_get(c64script_array_t *array, size_t index, c64script_value_t *out_value);
bool c64script_array_set(c64script_array_t *array, size_t index, c64script_value_t value);

/**
 * Map operations
 */
bool c64script_map_get(c64script_map_t *map, const char *key, c64script_value_t *out_value);
bool c64script_map_set(c64script_map_t *map, const char *key, c64script_value_t value);
