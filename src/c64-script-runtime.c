/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script.h"
#include "c64-script-runtime.h"
#include "c64-logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MACRO_LOG_PREFIX "[c64script-runtime] "

// Runtime context stub - Phase 5 implementation
// This manages variable storage and execution state

c64script_runtime_t *c64script_runtime_create(void)
{
    c64script_runtime_t *runtime = calloc(1, sizeof(c64script_runtime_t));
    if (!runtime) {
        blog(LOG_ERROR, "Failed to allocate runtime");
        return NULL;
    }

    // Initialize all fields to safe defaults
    runtime->bytecode = NULL;
    runtime->bytecode_size = 0;
    runtime->ip = 0;

    runtime->constants = NULL;
    runtime->constant_count = 0;

    runtime->variables = NULL;
    runtime->variable_count = 0;
    runtime->variable_capacity = 0;

    runtime->stack = NULL;
    runtime->stack_size = 0;
    runtime->stack_capacity = 0;

    runtime->for_stack_size = 0;
    runtime->while_stack_size = 0;
    runtime->gosub_stack_size = 0;

    runtime->should_stop = false;
    runtime->trace_enabled = false;

    runtime->log_file = NULL;
    runtime->log_filename[0] = '\0';

    runtime->error_msg[0] = '\0';
    runtime->error_line = -1;

    runtime->source_data = NULL;
    runtime->rest_client = NULL;
    runtime->keyboard = NULL;

    return runtime;
}

void c64script_runtime_destroy(c64script_runtime_t *runtime)
{
    if (!runtime)
        return;

    // Close log file if open
    if (runtime->log_file) {
        fclose(runtime->log_file);
        runtime->log_file = NULL;
    }

    // Free bytecode
    if (runtime->bytecode) {
        free(runtime->bytecode);
        runtime->bytecode = NULL;
    }

    // Free constants (strings need special handling)
    if (runtime->constants) {
        for (size_t i = 0; i < runtime->constant_count; i++) {
            if (runtime->constants[i].type == VALUE_STRING && runtime->constants[i].as.string) {
                free(runtime->constants[i].as.string);
            }
        }
        free(runtime->constants);
        runtime->constants = NULL;
    }

    // Free variables (strings need special handling)
    if (runtime->variables) {
        for (size_t i = 0; i < runtime->variable_count; i++) {
            if (runtime->variables[i].value.type == VALUE_STRING && runtime->variables[i].value.as.string) {
                free(runtime->variables[i].value.as.string);
            }
        }
        free(runtime->variables);
        runtime->variables = NULL;
    }

    // Free stack (strings need special handling)
    if (runtime->stack) {
        for (size_t i = 0; i < runtime->stack_size; i++) {
            if (runtime->stack[i].type == VALUE_STRING && runtime->stack[i].as.string) {
                free(runtime->stack[i].as.string);
            }
        }
        free(runtime->stack);
        runtime->stack = NULL;
    }

    free(runtime);
}

bool c64script_runtime_set_var(c64script_runtime_t *runtime, const char *name, c64script_value_t value)
{
    if (!runtime || !name) {
        return false;
    }

    // TODO: Implement variable storage (Phase 5)
    (void)value;
    return true;
}

bool c64script_runtime_get_var(c64script_runtime_t *runtime, const char *name, c64script_value_t *out_value)
{
    if (!runtime || !name || !out_value) {
        return false;
    }

    // TODO: Implement variable retrieval (Phase 5)
    out_value->type = VALUE_NUMBER;
    out_value->as.number = 0.0;
    return true;
}

bool c64script_runtime_push(c64script_runtime_t *runtime, c64script_value_t value)
{
    if (!runtime) {
        return false;
    }

    // TODO: Implement stack push (Phase 4C)
    (void)value;
    return true;
}

bool c64script_runtime_pop(c64script_runtime_t *runtime, c64script_value_t *out_value)
{
    if (!runtime || !out_value) {
        return false;
    }

    // TODO: Implement stack pop (Phase 4C)
    out_value->type = VALUE_NUMBER;
    out_value->as.number = 0.0;
    return true;
}
