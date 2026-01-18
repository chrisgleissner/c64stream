/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-keyboard.h"
#include "c64-logging.h"
#include "c64-rest-client.h"
#include "c64-script-builtins.h"
#include "c64-script-runtime.h"
#include "c64-script.h"
#include "c64-script-vm-dispatch-builtins.h"
#include "c64-script-vm-dispatch-config.h"
#include "c64-script-vm-dispatch-drives.h"
#include "c64-script-vm-dispatch-effects.h"
#include "c64-script-vm-dispatch-io.h"
#include "c64-script-vm-dispatch-keyboard.h"
#include "c64-script-vm-dispatch-machine.h"
#include "c64-script-vm-dispatch-memory.h"
#include "c64-script-vm-internal.h"

#include <ctype.h>
#include <limits.h>
#include <obs-module.h>
#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define popen _popen
#define pclose _pclose
#include <malloc.h>
#define alloca _alloca
#else
#include <strings.h>
#include <sys/wait.h>
#include <alloca.h>
#endif
#include <math.h>
#include <time.h>
#include <util/platform.h>
#include <curl/curl.h>

#define MACRO_LOG_PREFIX "[c64script-vm] "

static bool c64script_name_is_string(const char *name)
{
    if (!name) {
        return false;
    }

    size_t len = strlen(name);
    if (len == 0) {
        return false;
    }

    return name[len - 1] == '$';
}

static double wallclock_now_seconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static bool execute_instruction(c64script_runtime_t *runtime, const c64script_instruction_t *instr,
                                bool *skip_line_update)
{
    c64script_value_t a, b, result;

    if (skip_line_update) {
        *skip_line_update = false;
    }

    switch (instr->opcode) {
    case OP_NOP:
        break;

    case OP_PUSH_CONST:
        if (instr->operand >= runtime->constant_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid constant index");
            return false;
        }
        if (!c64script_runtime_push(runtime, c64script_value_clone(runtime->constants[instr->operand])))
            return false;
        break;

    case OP_PUSH_VAR: {
        if (instr->operand >= runtime->constant_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid constant index for variable name");
            return false;
        }
        const char *varname = runtime->constants[instr->operand].as.string;
        c64script_value_t value;
        if (!c64script_runtime_get_var(runtime, varname, &value))
            return false;
        if (!c64script_runtime_push(runtime, value))
            return false;
        break;
    }

    case OP_POP_VAR: {
        if (instr->operand >= runtime->constant_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid constant index for variable name");
            return false;
        }
        const char *varname = runtime->constants[instr->operand].as.string;
        c64script_value_t value;
        if (!c64script_runtime_pop(runtime, &value))
            return false;
        bool ok = c64script_runtime_set_var(runtime, varname, value);
        c64script_value_free(&value);
        if (!ok)
            return false;
        break;
    }

    // Array operations
    case OP_DIM_ARRAY: {
        // Stack: [size]
        // Create array with given size
        if (instr->operand >= runtime->constant_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid constant index for array name");
            return false;
        }
        const char *arrayname = runtime->constants[instr->operand].as.string;
        c64script_value_t size_val;
        if (!c64script_runtime_pop(runtime, &size_val))
            return false;

        if (size_val.type != VALUE_NUMBER) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Array size must be a number");
            c64script_value_free(&size_val);
            return false;
        }

        size_t size = (size_t)size_val.as.number;
        c64script_value_free(&size_val);

        if (size == 0) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Array size must be greater than 0");
            return false;
        }

        c64script_value_type_t element_type = c64script_name_is_string(arrayname) ? VALUE_STRING : VALUE_NUMBER;
        c64script_value_t array = c64script_value_array(size, element_type);
        if (!c64script_runtime_set_var(runtime, arrayname, array)) {
            c64script_value_free(&array);
            return false;
        }
        c64script_value_free(&array);
        break;
    }

    case OP_ARRAY_GET: {
        // Stack: [index]
        // Get element from array
        if (instr->operand >= runtime->constant_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid constant index for array name");
            return false;
        }
        const char *arrayname = runtime->constants[instr->operand].as.string;
        c64script_value_t index_val;
        if (!c64script_runtime_pop(runtime, &index_val))
            return false;

        if (index_val.type != VALUE_NUMBER) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Array index must be a number");
            c64script_value_free(&index_val);
            return false;
        }

        size_t index = (size_t)index_val.as.number;
        c64script_value_free(&index_val);

        c64script_value_t array_var;
        if (!c64script_runtime_get_var(runtime, arrayname, &array_var)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' not found", arrayname);
            return false;
        }

        if (array_var.type != VALUE_ARRAY) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' is not an array", arrayname);
            c64script_value_free(&array_var);
            return false;
        }

        c64script_value_t element;
        if (!c64script_array_get(array_var.as.array, index, &element)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Array index out of bounds");
            c64script_value_free(&array_var);
            return false;
        }
        c64script_value_free(&array_var);

        if (!c64script_runtime_push(runtime, element)) {
            c64script_value_free(&element);
            return false;
        }
        break;
    }

    case OP_ARRAY_SET: {
        // Stack: [value, index]
        // Set element in array
        if (instr->operand >= runtime->constant_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid constant index for array name");
            return false;
        }
        const char *arrayname = runtime->constants[instr->operand].as.string;
        c64script_value_t index_val;
        if (!c64script_runtime_pop(runtime, &index_val))
            return false;

        c64script_value_t value_val;
        if (!c64script_runtime_pop(runtime, &value_val)) {
            c64script_value_free(&index_val);
            return false;
        }

        if (index_val.type != VALUE_NUMBER) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Array index must be a number");
            c64script_value_free(&index_val);
            c64script_value_free(&value_val);
            return false;
        }

        size_t index = (size_t)index_val.as.number;
        c64script_value_free(&index_val);

        c64script_value_t array_var;
        if (!c64script_runtime_get_var(runtime, arrayname, &array_var)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' not found", arrayname);
            c64script_value_free(&value_val);
            return false;
        }

        if (array_var.type != VALUE_ARRAY) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' is not an array", arrayname);
            c64script_value_free(&array_var);
            c64script_value_free(&value_val);
            return false;
        }

        c64script_value_type_t element_type = array_var.as.array->element_type;
        if (element_type == VALUE_STRING) {
            if (value_val.type == VALUE_NUMBER) {
                char str_buf[64];
                if (!c64script_builtin_str(value_val.as.number, str_buf, sizeof(str_buf))) {
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "STR failed");
                    c64script_value_free(&array_var);
                    c64script_value_free(&value_val);
                    return false;
                }
                c64script_value_free(&value_val);
                value_val = c64script_value_string(str_buf);
            } else if (value_val.type != VALUE_STRING) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (ARRAY SET)");
                c64script_value_free(&array_var);
                c64script_value_free(&value_val);
                return false;
            }
        } else if (element_type == VALUE_NUMBER && value_val.type != VALUE_NUMBER) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (ARRAY SET)");
            c64script_value_free(&array_var);
            c64script_value_free(&value_val);
            return false;
        }

        if (!c64script_array_set(array_var.as.array, index, value_val)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Array index out of bounds");
            c64script_value_free(&array_var);
            c64script_value_free(&value_val);
            return false;
        }

        // Set the modified array back
        bool ok = c64script_runtime_set_var(runtime, arrayname, array_var);
        c64script_value_free(&array_var);
        c64script_value_free(&value_val);
        if (!ok)
            return false;
        break;
    }

    // Map operations
    case OP_MAP_GET: {
        // Stack: [key]
        // Get value from map
        if (instr->operand >= runtime->constant_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid constant index for map name");
            return false;
        }
        const char *mapname = runtime->constants[instr->operand].as.string;

        c64script_value_t key_val;
        if (!c64script_runtime_pop(runtime, &key_val))
            return false;

        if (key_val.type != VALUE_STRING) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Map key must be a string");
            c64script_value_free(&key_val);
            return false;
        }

        c64script_value_t map_var;
        if (!c64script_runtime_get_var(runtime, mapname, &map_var)) {
            // Auto-create empty map if it doesn't exist
            c64script_value_type_t map_value_type = c64script_name_is_string(mapname) ? VALUE_STRING : VALUE_NUMBER;
            map_var = c64script_value_map(map_value_type);
            if (!c64script_runtime_set_var(runtime, mapname, map_var)) {
                c64script_value_free(&map_var);
                c64script_value_free(&key_val);
                return false;
            }
        }

        if (map_var.type != VALUE_MAP) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' is not a map", mapname);
            c64script_value_free(&map_var);
            c64script_value_free(&key_val);
            return false;
        }

        c64script_value_t value;
        if (!c64script_map_get(map_var.as.map, key_val.as.string, &value)) {
            // Key not found - return default value (0 or "")
            value.type = map_var.as.map->value_type;
            if (value.type == VALUE_NUMBER) {
                value.as.number = 0.0;
            } else {
                value.as.string = strdup("");
            }
        }

        c64script_value_free(&map_var);
        c64script_value_free(&key_val);

        if (!c64script_runtime_push(runtime, value)) {
            c64script_value_free(&value);
            return false;
        }
        break;
    }

    case OP_MAP_SET: {
        // Stack: [value, key]
        // Set value in map
        if (instr->operand >= runtime->constant_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid constant index for map name");
            return false;
        }
        const char *mapname = runtime->constants[instr->operand].as.string;

        c64script_value_t key_val;
        if (!c64script_runtime_pop(runtime, &key_val))
            return false;

        c64script_value_t value_val;
        if (!c64script_runtime_pop(runtime, &value_val)) {
            c64script_value_free(&key_val);
            return false;
        }

        if (key_val.type != VALUE_STRING) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Map key must be a string");
            c64script_value_free(&key_val);
            c64script_value_free(&value_val);
            return false;
        }

        c64script_value_t map_var;
        bool var_exists = c64script_runtime_var_exists(runtime, mapname);
        if (!var_exists) {
            c64script_value_type_t map_value_type = c64script_name_is_string(mapname) ? VALUE_STRING : VALUE_NUMBER;
            blog(LOG_DEBUG, "[C64Script] Auto-creating map '%s' with value type %d", mapname, map_value_type);
            map_var = c64script_value_map(map_value_type);
            if (map_var.type != VALUE_MAP) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to create map (allocation failure)");
                c64script_value_free(&map_var);
                c64script_value_free(&key_val);
                c64script_value_free(&value_val);
                return false;
            }
        } else {
            if (!c64script_runtime_get_var(runtime, mapname, &map_var)) {
                c64script_value_free(&key_val);
                c64script_value_free(&value_val);
                return false;
            }
        }

        if (map_var.type != VALUE_MAP) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' is not a map (type=%d)", mapname,
                     map_var.type);
            c64script_value_free(&map_var);
            c64script_value_free(&key_val);
            c64script_value_free(&value_val);
            return false;
        }

        if (map_var.as.map->value_type == VALUE_STRING) {
            if (value_val.type == VALUE_NUMBER) {
                char str_buf[64];
                if (!c64script_builtin_str(value_val.as.number, str_buf, sizeof(str_buf))) {
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "STR failed");
                    c64script_value_free(&map_var);
                    c64script_value_free(&key_val);
                    c64script_value_free(&value_val);
                    return false;
                }
                c64script_value_free(&value_val);
                value_val = c64script_value_string(str_buf);
            } else if (value_val.type != VALUE_STRING) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (MAP SET)");
                c64script_value_free(&map_var);
                c64script_value_free(&key_val);
                c64script_value_free(&value_val);
                return false;
            }
        } else if (map_var.as.map->value_type == VALUE_NUMBER && value_val.type != VALUE_NUMBER) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (MAP SET)");
            c64script_value_free(&map_var);
            c64script_value_free(&key_val);
            c64script_value_free(&value_val);
            return false;
        }

        if (!c64script_map_set(map_var.as.map, key_val.as.string, value_val)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to set map value");
            c64script_value_free(&map_var);
            c64script_value_free(&key_val);
            c64script_value_free(&value_val);
            return false;
        }

        // Set the modified map back
        bool ok = c64script_runtime_set_var(runtime, mapname, map_var);
        c64script_value_free(&map_var);
        c64script_value_free(&key_val);
        c64script_value_free(&value_val);
        if (!ok)
            return false;
        break;
    }

    case OP_POP:
        if (!c64script_runtime_pop(runtime, &a))
            return false;
        c64script_value_free(&a);
        break;

    // Arithmetic
    case OP_ADD:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;

        // Handle string concatenation
        if (a.type == VALUE_STRING || b.type == VALUE_STRING) {
            // Convert both to strings if needed
            char *a_str = NULL;
            char *b_str = NULL;
            bool a_needs_free = false;
            bool b_needs_free = false;

            if (a.type == VALUE_STRING) {
                a_str = a.as.string;
            } else if (a.type == VALUE_NUMBER) {
                a_str = malloc(64);
                if (!a_str) {
                    c64script_value_free(&a);
                    c64script_value_free(&b);
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                    return false;
                }
                c64script_builtin_str(a.as.number, a_str, 64);
                a_needs_free = true;
            } else {
                c64script_value_free(&a);
                c64script_value_free(&b);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (ADD)");
                return false;
            }

            if (b.type == VALUE_STRING) {
                b_str = b.as.string;
            } else if (b.type == VALUE_NUMBER) {
                b_str = malloc(64);
                if (!b_str) {
                    if (a_needs_free)
                        free(a_str);
                    c64script_value_free(&a);
                    c64script_value_free(&b);
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                    return false;
                }
                c64script_builtin_str(b.as.number, b_str, 64);
                b_needs_free = true;
            } else {
                if (a_needs_free)
                    free(a_str);
                c64script_value_free(&a);
                c64script_value_free(&b);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (ADD)");
                return false;
            }

            // Concatenate strings
            size_t len = strlen(a_str) + strlen(b_str) + 1;
            char *concat = malloc(len);
            if (!concat) {
                if (a_needs_free)
                    free(a_str);
                if (b_needs_free)
                    free(b_str);
                c64script_value_free(&a);
                c64script_value_free(&b);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                return false;
            }
            strcpy(concat, a_str);
            strcat(concat, b_str);

            if (a_needs_free)
                free(a_str);
            if (b_needs_free)
                free(b_str);
            c64script_value_free(&a);
            c64script_value_free(&b);

            result = c64script_value_string(concat);
            free(concat);
            if (!c64script_runtime_push(runtime, result)) {
                c64script_value_free(&result);
                return false;
            }
        } else {
            // Handle number addition
            if (!require_number(runtime, &a, "ADD") || !require_number(runtime, &b, "ADD")) {
                c64script_value_free(&a);
                c64script_value_free(&b);
                return false;
            }
            result.type = VALUE_NUMBER;
            result.as.number = a.as.number + b.as.number;
            c64script_value_free(&a);
            c64script_value_free(&b);
            if (!c64script_runtime_push(runtime, result))
                return false;
        }
        break;

    case OP_SUBTRACT:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        if (!require_number(runtime, &a, "SUBTRACT") || !require_number(runtime, &b, "SUBTRACT")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = a.as.number - b.as.number;
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_MULTIPLY:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        if (!require_number(runtime, &a, "MULTIPLY") || !require_number(runtime, &b, "MULTIPLY")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = a.as.number * b.as.number;
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_DIVIDE:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        if (!require_number(runtime, &a, "DIVIDE") || !require_number(runtime, &b, "DIVIDE")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        if (b.as.number == 0.0) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Division by zero");
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = a.as.number / b.as.number;
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_NEGATE:
        if (!c64script_runtime_pop(runtime, &a))
            return false;
        if (!require_number(runtime, &a, "NEGATE")) {
            c64script_value_free(&a);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = -a.as.number;
        c64script_value_free(&a);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    // Relational
    case OP_EQ:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        int cmp_eq = 0;
        if (!compare_values(runtime, &a, &b, &cmp_eq, "EQ")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = (cmp_eq == 0) ? 1.0 : 0.0;
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_NE:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        int cmp_ne = 0;
        if (!compare_values(runtime, &a, &b, &cmp_ne, "NE")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = (cmp_ne != 0) ? 1.0 : 0.0;
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_LT:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        int cmp_lt = 0;
        if (!compare_values(runtime, &a, &b, &cmp_lt, "LT")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = (cmp_lt < 0) ? 1.0 : 0.0;
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_LE:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        int cmp_le = 0;
        if (!compare_values(runtime, &a, &b, &cmp_le, "LE")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = (cmp_le <= 0) ? 1.0 : 0.0;
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_GT:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        int cmp_gt = 0;
        if (!compare_values(runtime, &a, &b, &cmp_gt, "GT")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = (cmp_gt > 0) ? 1.0 : 0.0;
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_GE:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        int cmp_ge = 0;
        if (!compare_values(runtime, &a, &b, &cmp_ge, "GE")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = (cmp_ge >= 0) ? 1.0 : 0.0;
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    // Boolean (bitwise on truncated integers)
    case OP_NOT:
        if (!c64script_runtime_pop(runtime, &a))
            return false;
        if (!require_number(runtime, &a, "NOT")) {
            c64script_value_free(&a);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = (double)(~((int)a.as.number));
        c64script_value_free(&a);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_AND:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        if (!require_number(runtime, &a, "AND") || !require_number(runtime, &b, "AND")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = (double)(((int)a.as.number) & ((int)b.as.number));
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_XOR:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        if (!require_number(runtime, &a, "XOR") || !require_number(runtime, &b, "XOR")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = (double)(((int)a.as.number) ^ ((int)b.as.number));
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_OR:
        if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
            return false;
        if (!require_number(runtime, &a, "OR") || !require_number(runtime, &b, "OR")) {
            c64script_value_free(&a);
            c64script_value_free(&b);
            return false;
        }
        result.type = VALUE_NUMBER;
        result.as.number = (double)(((int)a.as.number) | ((int)b.as.number));
        c64script_value_free(&a);
        c64script_value_free(&b);
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    // Control flow
    case OP_JUMP:
        runtime->ip = instr->operand;
        break;

    case OP_JUMP_IF_FALSE:
        if (!c64script_runtime_pop(runtime, &a))
            return false;
        if (!require_number(runtime, &a, "JUMP_IF_FALSE")) {
            c64script_value_free(&a);
            return false;
        }
        if (a.as.number == 0.0) {
            runtime->ip = instr->operand;
        }
        c64script_value_free(&a);
        break;

    case OP_CALL: {
        // Stack should have: param_count, param1, param2, ..., paramN
        // Pop param count first
        c64script_value_t param_count_val;
        if (!c64script_runtime_pop(runtime, &param_count_val))
            return false;
        if (param_count_val.type != VALUE_NUMBER) {
            c64script_value_free(&param_count_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (GOSUB param count)");
            return false;
        }

        size_t param_count = (size_t)param_count_val.as.number;
        c64script_value_free(&param_count_val);

        // Pop parameters and store as PARAM1, PARAM2, etc.
        for (size_t i = param_count; i > 0; i--) {
            c64script_value_t param_val;
            if (!c64script_runtime_pop(runtime, &param_val))
                return false;

            char param_name[32];
            snprintf(param_name, sizeof(param_name), "PARAM%zu", i);
            c64script_runtime_set_var(runtime, param_name, param_val);
            c64script_value_free(&param_val);
        }

        // Push return address and param count
        if (runtime->gosub_stack_size >= C64SCRIPT_MAX_GOSUB_DEPTH) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "GOSUB stack overflow");
            return false;
        }
        runtime->gosub_stack[runtime->gosub_stack_size].return_ip = runtime->ip;
        runtime->gosub_stack[runtime->gosub_stack_size].param_count = param_count;
        runtime->gosub_stack_size++;

        runtime->ip = instr->operand;
        break;
    }

    case OP_RETURN:
    case OP_RETURN_VALUE: {
        bool has_return_value = (instr->opcode == OP_RETURN_VALUE) || (instr->operand != 0);
        // Check if we're in a function scope or GOSUB
        if (runtime->scope_stack_size > 0) {
            // Function return
            c64script_scope_t *scope = &runtime->scope_stack[runtime->scope_stack_size - 1];
            // Get return value if present (operand = 1 means yes, 0 means no)
            c64script_value_t return_val = {.type = VALUE_NUMBER, .as.number = 0.0};
            if (has_return_value) {
                if (!c64script_runtime_pop(runtime, &return_val))
                    return false;
            }

            // Clean up local variables
            for (size_t i = 0; i < scope->local_var_count; i++) {
                c64script_value_free(&scope->local_vars[i].value);
            }
            free(scope->local_vars);

            // Pop scope
            size_t return_ip = scope->return_ip;
            runtime->scope_stack_size--;

            // Push return value onto stack
            if (!c64script_runtime_push(runtime, return_val)) {
                c64script_value_free(&return_val);
                return false;
            }

            // Return to caller
            runtime->ip = return_ip;
            if (skip_line_update) {
                *skip_line_update = true;
            }
            return true;
        } else if (runtime->gosub_stack_size > 0) {
            // GOSUB return
            if (has_return_value) {
                c64script_value_t return_val;
                if (!c64script_runtime_pop(runtime, &return_val))
                    return false;
                c64script_runtime_set_var(runtime, "RESULT", return_val);
                c64script_value_free(&return_val);
            }

            // Clean up parameters
            size_t param_count = runtime->gosub_stack[runtime->gosub_stack_size - 1].param_count;
            for (size_t i = 1; i <= param_count; i++) {
                char param_name[32];
                snprintf(param_name, sizeof(param_name), "PARAM%zu", i);
                // Remove the variable by setting it to undefined (number 0)
                c64script_value_t undef = {.type = VALUE_NUMBER, .as.number = 0.0};
                c64script_runtime_set_var(runtime, param_name, undef);
            }

            // Return to caller
            runtime->ip = runtime->gosub_stack[--runtime->gosub_stack_size].return_ip;
            break;
        } else {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RETURN without GOSUB or function call");
            return false;
        }
    }

    case OP_ENTER_SCOPE:
    case OP_EXIT_SCOPE:
        // Compatibility no-ops: current function scoping is managed by OP_CALL_FUNCTION/OP_RETURN.
        break;

    case OP_STOP:
    case OP_HALT:
        runtime->should_stop = true;
        break;

    // Loop opcodes
    case OP_FOR_INIT: {
        // Stack has: step, end, current
        // operand: variable name constant pool index
        c64script_value_t step, end, current;
        if (!c64script_runtime_pop(runtime, &step) || !c64script_runtime_pop(runtime, &end) ||
            !c64script_runtime_pop(runtime, &current))
            return false;
        if (!require_number(runtime, &step, "FOR") || !require_number(runtime, &end, "FOR") ||
            !require_number(runtime, &current, "FOR")) {
            c64script_value_free(&step);
            c64script_value_free(&end);
            c64script_value_free(&current);
            return false;
        }

        if (runtime->for_stack_size >= C64SCRIPT_MAX_FOR_NESTING) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "FOR loop nesting too deep");
            c64script_value_free(&step);
            c64script_value_free(&end);
            c64script_value_free(&current);
            return false;
        }

        // Get variable name from constant pool
        if (instr->operand >= runtime->constant_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid constant index");
            return false;
        }
        const char *var_name = runtime->constants[instr->operand].as.string;

        // Push FOR state
        c64script_for_state_t *state = &runtime->for_stack[runtime->for_stack_size++];
        state->variable = var_name;
        state->end_value = end.as.number;
        state->step_value = step.as.number;
        state->loop_start_ip = runtime->ip; // Next instruction after FOR_INIT
        c64script_value_free(&step);
        c64script_value_free(&end);
        c64script_value_free(&current);
        break;
    }

    case OP_FOR_CHECK: {
        // Check if FOR loop should continue
        // operand: jump address if loop is done
        if (runtime->for_stack_size == 0) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "FOR_CHECK without FOR_INIT");
            return false;
        }

        c64script_for_state_t *state = &runtime->for_stack[runtime->for_stack_size - 1];

        // Get current value of loop variable
        c64script_value_t current;
        if (!c64script_runtime_get_var(runtime, state->variable, &current)) {
            return false;
        }
        if (!require_number(runtime, &current, "FOR")) {
            c64script_value_free(&current);
            return false;
        }

        // Check if loop should continue
        bool done;
        if (state->step_value > 0) {
            done = current.as.number > state->end_value;
        } else {
            done = current.as.number < state->end_value;
        }

        if (done) {
            // Pop FOR state and jump to end
            runtime->for_stack_size--;
            runtime->ip = instr->operand;
        }
        c64script_value_free(&current);
        break;
    }

    case OP_FOR_INCR: {
        // Increment loop variable
        // operand: variable name constant pool index
        if (runtime->for_stack_size == 0) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "FOR_INCR without FOR_INIT");
            return false;
        }

        c64script_for_state_t *state = &runtime->for_stack[runtime->for_stack_size - 1];

        // Get current value
        c64script_value_t current;
        if (!c64script_runtime_get_var(runtime, state->variable, &current)) {
            return false;
        }
        if (!require_number(runtime, &current, "FOR")) {
            c64script_value_free(&current);
            return false;
        }

        // Increment by step
        current.as.number += state->step_value;

        // Store back
        bool ok = c64script_runtime_set_var(runtime, state->variable, current);
        c64script_value_free(&current);
        if (!ok)
            return false;
        break;
    }

    case OP_WHILE_CHECK: {
        // Check WHILE condition
        // operand: jump address if condition is false
        c64script_value_t condition;
        if (!c64script_runtime_pop(runtime, &condition))
            return false;
        if (!require_number(runtime, &condition, "WHILE")) {
            c64script_value_free(&condition);
            return false;
        }

        // If condition is false (0), jump to end
        if (condition.as.number == 0.0) {
            runtime->ip = instr->operand;
        }
        c64script_value_free(&condition);
        break;
    }

    case OP_WAIT: {
        c64script_value_t duration;
        if (!c64script_runtime_pop(runtime, &duration))
            return false;
        if (!require_number(runtime, &duration, "WAIT")) {
            c64script_value_free(&duration);
            return false;
        }

        double v = duration.as.number;
        c64script_value_free(&duration);
        if (v < 0.0) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
            return false;
        }

        // Skip waiting in step mode, when paused, or during trace recording (test mode)
        if (runtime->is_paused || runtime->step_mode || runtime->step_skip_waits || runtime->trace_recording_enabled) {
            break;
        }

        double multiplier = wait_unit_multiplier((c64script_wait_unit_t)instr->operand);
        uint64_t remaining_ms = (uint64_t)(v * multiplier);
        while (remaining_ms > 0 && !runtime->should_stop) {
            uint64_t step = remaining_ms > 50 ? 50 : remaining_ms;
            os_sleep_ms((uint32_t)step);
            remaining_ms -= step;
        }
        break;
    }

    case OP_WAIT_MEM: {
        const uint32_t wait_mem_has_value = (1u << 8);
        const uint32_t wait_mem_has_poll = (1u << 9);
        const uint32_t wait_mem_unit_mask = 0xFFu;

        bool has_value = (instr->operand & wait_mem_has_value) != 0;
        bool has_poll = (instr->operand & wait_mem_has_poll) != 0;
        c64script_wait_unit_t poll_unit = (c64script_wait_unit_t)(instr->operand & wait_mem_unit_mask);
        c64script_value_t poll_val = {0};
        c64script_value_t value_val = {0};
        c64script_value_t mask_val = {0};
        c64script_value_t addr_val = {0};

        if (has_poll) {
            if (!c64script_runtime_pop(runtime, &poll_val))
                return false;
        }
        if (has_value) {
            if (!c64script_runtime_pop(runtime, &value_val)) {
                c64script_value_free(&poll_val);
                return false;
            }
        }
        if (!c64script_runtime_pop(runtime, &mask_val)) {
            c64script_value_free(&poll_val);
            c64script_value_free(&value_val);
            return false;
        }
        if (!c64script_runtime_pop(runtime, &addr_val)) {
            c64script_value_free(&poll_val);
            c64script_value_free(&value_val);
            c64script_value_free(&mask_val);
            return false;
        }

        uint16_t address = 0;
        uint8_t mask = 0;
        uint8_t value = 0;
        uint32_t poll_ms = 500;

        if (!number_to_uint16(runtime, &addr_val, &address, "WAIT")) {
            c64script_value_free(&poll_val);
            c64script_value_free(&value_val);
            c64script_value_free(&mask_val);
            c64script_value_free(&addr_val);
            return false;
        }
        if (!number_to_uint8(runtime, &mask_val, &mask, "WAIT")) {
            c64script_value_free(&poll_val);
            c64script_value_free(&value_val);
            c64script_value_free(&mask_val);
            c64script_value_free(&addr_val);
            return false;
        }
        if (has_value) {
            if (!number_to_uint8(runtime, &value_val, &value, "WAIT")) {
                c64script_value_free(&poll_val);
                c64script_value_free(&value_val);
                c64script_value_free(&mask_val);
                c64script_value_free(&addr_val);
                return false;
            }
        } else {
            value = mask;
        }

        if (has_poll) {
            if (!require_number(runtime, &poll_val, "WAIT")) {
                c64script_value_free(&poll_val);
                c64script_value_free(&value_val);
                c64script_value_free(&mask_val);
                c64script_value_free(&addr_val);
                return false;
            }
            double poll_seconds = poll_val.as.number;
            if (poll_seconds < 0.0) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                c64script_value_free(&poll_val);
                c64script_value_free(&value_val);
                c64script_value_free(&mask_val);
                c64script_value_free(&addr_val);
                return false;
            }
            poll_ms = (uint32_t)(poll_seconds * wait_unit_multiplier(poll_unit));
        }

        c64script_value_free(&poll_val);
        c64script_value_free(&value_val);
        c64script_value_free(&mask_val);
        c64script_value_free(&addr_val);

        if (poll_ms == 0) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
            return false;
        }

        // Skip waiting in step mode, when paused, or during trace recording (test mode)
        if (runtime->is_paused || runtime->step_mode || runtime->step_skip_waits || runtime->trace_recording_enabled) {
            break;
        }

        if (!runtime->rest_client) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }

        uint8_t buf[1] = {0};
        while (!runtime->should_stop) {
            int read_count =
                c64_rest_read_memory((c64_rest_client_t *)runtime->rest_client, address, 1, buf, sizeof(buf));
            if (read_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "WAIT failed: %s",
                         c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
                return false;
            }

            if ((buf[0] & mask) == value) {
                break;
            }

            uint32_t remaining_ms = poll_ms;
            while (remaining_ms > 0 && !runtime->should_stop) {
                uint32_t step = remaining_ms > 50 ? 50 : remaining_ms;
                os_sleep_ms(step);
                remaining_ms -= step;
            }
        }
        break;
    }

    case OP_WAIT_UNTIL: {
        c64script_value_t target;
        if (!c64script_runtime_pop(runtime, &target))
            return false;

        double target_epoch = 0.0;
        if (target.type == VALUE_NUMBER) {
            target_epoch = target.as.number;
        } else if (target.type == VALUE_STRING) {
            if (!parse_wallclock_target(target.as.string, &target_epoch)) {
                c64script_value_free(&target);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                return false;
            }
        } else {
            c64script_value_free(&target);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (WAIT UNTIL)");
            return false;
        }
        c64script_value_free(&target);

        // Skip waiting in step mode, when paused, or during trace recording (test mode)
        if (runtime->is_paused || runtime->step_mode || runtime->step_skip_waits || runtime->trace_recording_enabled) {
            break;
        }

        while (!runtime->should_stop) {
            double now_epoch = wallclock_now_seconds();
            if (target_epoch <= now_epoch) {
                break;
            }

            double remaining_s = target_epoch - now_epoch;
            uint32_t step_ms = 50;
            if (remaining_s < 0.050) {
                step_ms = 1;
            }
            os_sleep_ms(step_ms);
        }
        break;
    }

    case OP_CALL_PEEK:
        return c64script_dispatch_memory(runtime, instr);

    case OP_CALL_STR:
    case OP_CALL_BUILTIN:
        return c64script_dispatch_builtins(runtime, instr);

    // Plugin actions - these will be connected to REST API in Phase 6
    case OP_PUSH_NUM:
        // Push immediate number onto stack (used by some plugin actions)
        result.type = VALUE_NUMBER;
        result.as.number = (double)instr->operand;
        if (!c64script_runtime_push(runtime, result))
            return false;
        break;

    case OP_EFFECT:
    case OP_EFFECTPARAM:
    case OP_PALETTE:
    case OP_PALETTECOLOR:
        return c64script_dispatch_effects(runtime, instr);

    case OP_PLAYSID:
    case OP_RUNPRG:
        return c64script_dispatch_machine(runtime, instr);

    case OP_RUNLOCAL:
        return c64script_dispatch_io(runtime, instr);

    case OP_MOUNTDISK:
        return c64script_dispatch_machine(runtime, instr);

    case OP_RESET:
    case OP_REBOOT:
    case OP_PAUSE:
    case OP_RESUME:
    case OP_POWEROFF:
        return c64script_dispatch_machine(runtime, instr);

    case OP_CFG_SET:
    case OP_CFG_SAVE:
    case OP_CFG_LOAD:
    case OP_CFG_RESET:
    case OP_CFG_GET:
    case OP_CFG_ITEM:
    case OP_CFG_OPTIONS:
    case OP_SID_MODEL:
    case OP_SID_ENABLE:
    case OP_SID_VOL:
    case OP_SID_FILTER_CURVE:
    case OP_SID_RESONANCE:
    case OP_SID_COMBINED:
    case OP_SID_DIGIS:
    case OP_VIC_MODE:
    case OP_CPU_SPEED:
        return c64script_dispatch_config(runtime, instr);

    case OP_DRIVE_GET:
    case OP_DRIVE_MOUNT:
    case OP_DRIVE_UNMOUNT:
    case OP_DRIVE_RESET:
    case OP_DRIVE_ON:
    case OP_DRIVE_OFF:
    case OP_DRIVE_ROM:
    case OP_DRIVE_MODE:
    case OP_DRIVE_BUS_ID:
    case OP_LOAD:
    case OP_RUN:
    case OP_SYS:
        return c64script_dispatch_drives(runtime, instr);

    case OP_RECORDSTART:
    case OP_RECORDSTOP:
        return c64script_dispatch_machine(runtime, instr);

    case OP_TYPE:
    case OP_KEY:
    case OP_AUTOSTART:
        return c64script_dispatch_keyboard(runtime, instr);

    case OP_POKE_SINGLE:
    case OP_POKE_ARRAY:
        return c64script_dispatch_memory(runtime, instr);

    case OP_LOG:
    case OP_PRINT: {
        // LOG/PRINT message - Print to log or console
        c64script_value_t message;
        if (!c64script_runtime_pop(runtime, &message))
            return false;

        if (instr->opcode == OP_PRINT) {
            if (message.type == VALUE_STRING) {
                blog(LOG_INFO, "[C64Script] %s", message.as.string);
            } else {
                blog(LOG_INFO, "[C64Script] %.15g", message.as.number);
            }
        } else {
            if (!runtime->log_file) {
                char default_name[1024];
                const char *log_path = runtime->log_filename;
                if (!log_path || log_path[0] == '\0') {
                    if (runtime->script_basename[0] != '\0') {
                        const char *sep = "";
                        size_t dir_len = strlen(runtime->script_dir);
                        if (dir_len > 0) {
                            char last = runtime->script_dir[dir_len - 1];
                            if (last != '/' && last != '\\') {
                                sep = "/";
                            }
                            snprintf(default_name, sizeof(default_name), "%s%s%s.log", runtime->script_dir, sep,
                                     runtime->script_basename);
                        } else {
                            snprintf(default_name, sizeof(default_name), "%s.log", runtime->script_basename);
                        }
                        log_path = default_name;
                    } else {
                        log_path = "c64script.log";
                    }
                }
                runtime->log_file = fopen(log_path, "a");
                if (runtime->log_file) {
                    strncpy(runtime->log_filename, log_path, sizeof(runtime->log_filename) - 1);
                    runtime->log_filename[sizeof(runtime->log_filename) - 1] = '\0';
                }
            }
            if (!runtime->log_file) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to open log file");
                c64script_value_free(&message);
                return false;
            }

            if (message.type == VALUE_STRING) {
                fprintf(runtime->log_file, "%s\n", message.as.string ? message.as.string : "");
            } else {
                fprintf(runtime->log_file, "%.15g\n", message.as.number);
            }
            fflush(runtime->log_file);
        }
        c64script_value_free(&message);
        break;
    }

    case OP_LOGFILE: {
        // LOGFILE filename - Open log file (operand: 1=truncate, 0=append)
        c64script_value_t filename;
        if (!c64script_runtime_pop(runtime, &filename))
            return false;
        if (filename.type != VALUE_STRING) {
            c64script_value_free(&filename);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (LOGFILE)");
            return false;
        }

        // Close existing log file if open
        if (runtime->log_file) {
            fclose(runtime->log_file);
            runtime->log_file = NULL;
        }

        char resolved_path[1024];
        const char *path = filename.as.string ? filename.as.string : "";
        if (!c64script_resolve_script_path(runtime, path, resolved_path, sizeof(resolved_path))) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Path too long");
            c64script_value_free(&filename);
            return false;
        }

        // Open new log file
        const char *mode_str = (instr->operand != 0) ? "w" : "a";
        runtime->log_file = fopen(resolved_path, mode_str);
        if (!runtime->log_file) {
            const char *prefix = "Failed to open log file: ";
            size_t max_len = sizeof(runtime->error_msg) - strlen(prefix) - 1;
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s%.*s", prefix, (int)max_len, resolved_path);
            c64script_value_free(&filename);
            return false;
        } else {
            strncpy(runtime->log_filename, resolved_path, sizeof(runtime->log_filename) - 1);
            runtime->log_filename[sizeof(runtime->log_filename) - 1] = '\0';
        }
        c64script_value_free(&filename);
        break;
    }

    case OP_TRON:
        if (!runtime->log_file) {
            char default_name[1024];
            const char *log_path = runtime->log_filename;
            if (!log_path || log_path[0] == '\0') {
                if (runtime->script_basename[0] != '\0') {
                    const char *sep = "";
                    size_t dir_len = strlen(runtime->script_dir);
                    if (dir_len > 0) {
                        char last = runtime->script_dir[dir_len - 1];
                        if (last != '/' && last != '\\') {
                            sep = "/";
                        }
                        snprintf(default_name, sizeof(default_name), "%s%s%s.log", runtime->script_dir, sep,
                                 runtime->script_basename);
                    } else {
                        snprintf(default_name, sizeof(default_name), "%s.log", runtime->script_basename);
                    }
                    log_path = default_name;
                } else {
                    log_path = "c64script.log";
                }
            }
            runtime->log_file = fopen(log_path, "a");
            if (runtime->log_file) {
                strncpy(runtime->log_filename, log_path, sizeof(runtime->log_filename) - 1);
                runtime->log_filename[sizeof(runtime->log_filename) - 1] = '\0';
            } else {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to open log file");
                return false;
            }
        }
        runtime->trace_enabled = true;
        blog(LOG_INFO, "TRON: Tracing enabled");
        break;

    case OP_TROFF:
        runtime->trace_enabled = false;
        blog(LOG_INFO, "TROFF: Tracing disabled");
        break;

    case OP_READFILE:
    case OP_WRITEFILE_APPEND:
    case OP_WRITEFILE_TRUNCATE:
    case OP_HTTP:
        return c64script_dispatch_io(runtime, instr);

    // Function operations (not yet implemented)
    case OP_CALL_FUNCTION: {
        // Operand: high 16 bits = function index, low 16 bits = arg count
        uint32_t func_idx = instr->operand >> 16;
        uint32_t arg_count = instr->operand & 0xFFFF;

        if (func_idx >= runtime->function_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid function index: %u", func_idx);
            return false;
        }

        c64script_function_def_t *func = &runtime->functions[func_idx];

        // Check argument count
        if (arg_count != func->param_count) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Function %s expects %zu arguments, got %u",
                     func->name, func->param_count, arg_count);
            return false;
        }

        // Create new scope
        if (runtime->scope_stack_size >= runtime->scope_stack_capacity) {
            size_t new_cap = runtime->scope_stack_capacity == 0 ? 8 : runtime->scope_stack_capacity * 2;
            c64script_scope_t *new_stack = realloc(runtime->scope_stack, new_cap * sizeof(c64script_scope_t));
            if (!new_stack) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory for function call");
                return false;
            }
            runtime->scope_stack = new_stack;
            runtime->scope_stack_capacity = new_cap;
        }

        c64script_scope_t *scope = &runtime->scope_stack[runtime->scope_stack_size++];
        scope->local_vars = NULL;
        scope->local_var_count = 0;
        scope->local_var_capacity = 0;
        scope->saved_var_count = runtime->variable_count;
        scope->return_ip = runtime->ip + 1;

        // Pop arguments from stack and create local parameter variables
        // Arguments are in reverse order on stack (last arg on top)
        for (int i = (int)arg_count - 1; i >= 0; i--) {
            if (runtime->stack_size == 0) {
                runtime->scope_stack_size--;
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Stack underflow in function call");
                return false;
            }

            c64script_value_t arg = runtime->stack[--runtime->stack_size];

            // Create local variable for parameter
            if (scope->local_var_count >= scope->local_var_capacity) {
                size_t new_cap = scope->local_var_capacity == 0 ? 4 : scope->local_var_capacity * 2;
                c64script_variable_t *new_vars = realloc(scope->local_vars, new_cap * sizeof(c64script_variable_t));
                if (!new_vars) {
                    c64script_value_free(&arg);
                    runtime->scope_stack_size--;
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory for local variables");
                    return false;
                }
                scope->local_vars = new_vars;
                scope->local_var_capacity = new_cap;
            }

            c64script_variable_t *var = &scope->local_vars[scope->local_var_count++];
            strncpy(var->name, func->param_names[i], sizeof(var->name) - 1);
            var->name[sizeof(var->name) - 1] = '\0';
            var->value = arg;
        }

        // Jump to function body
        runtime->ip = func->bytecode_address;
        if (skip_line_update) {
            *skip_line_update = true;
        }
        return true;
    }

    default:
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Unknown opcode: %d", instr->opcode);
        return false;
    }

    return true;
}

bool c64script_vm_execute_instruction(c64script_runtime_t *runtime, const c64script_instruction_t *instr,
                                      bool *skip_line_update)
{
    return execute_instruction(runtime, instr, skip_line_update);
}
