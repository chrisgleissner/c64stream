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
#include <ctype.h>

#define MACRO_LOG_PREFIX "[c64script-runtime] "

static void normalize_identifier(const char *name, char out_name[64])
{
    size_t out_len = 0;

    if (!name) {
        out_name[0] = '\0';
        return;
    }

    for (size_t i = 0; name[i] != '\0' && out_len < 63; i++) {
        out_name[out_len++] = (char)toupper((unsigned char)name[i]);
    }
    out_name[out_len] = '\0';
}

c64script_value_t c64script_value_number(double num)
{
    c64script_value_t value = {0};
    value.type = VALUE_NUMBER;
    value.as.number = num;
    return value;
}

c64script_value_t c64script_value_string(const char *str)
{
    c64script_value_t value = {0};
    value.type = VALUE_STRING;
    value.as.string = str ? strdup(str) : strdup("");
    return value;
}

void c64script_value_free(c64script_value_t *value)
{
    if (!value) {
        return;
    }

    if (value->type == VALUE_STRING) {
        free(value->as.string);
        value->as.string = NULL;
    }

    value->type = VALUE_NUMBER;
    value->as.number = 0.0;
}

c64script_value_t c64script_value_clone(c64script_value_t value)
{
    if (value.type == VALUE_STRING) {
        return c64script_value_string(value.as.string ? value.as.string : "");
    }
    return c64script_value_number(value.as.number);
}

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
    runtime->should_pause = false;
    runtime->is_paused = false;
    runtime->step_mode = false;
    runtime->trace_enabled = false;

    runtime->last_executed_line = 0;
    runtime->next_line_to_execute = 0;
    runtime->source_text = NULL;
    runtime->source_text_size = 0;

    runtime->log_file = NULL;
    runtime->log_filename[0] = '\0';

    runtime->error_msg[0] = '\0';
    runtime->error_line = 0;

    runtime->source_data = NULL;
    runtime->obs_source = NULL;
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

    // Free source text
    if (runtime->source_text) {
        free(runtime->source_text);
        runtime->source_text = NULL;
    }

    // Free bytecode
    if (runtime->bytecode) {
        free(runtime->bytecode);
        runtime->bytecode = NULL;
    }

    // Free constants (strings need special handling)
    if (runtime->constants) {
        for (size_t i = 0; i < runtime->constant_count; i++) {
            c64script_value_free(&runtime->constants[i]);
        }
        free(runtime->constants);
        runtime->constants = NULL;
    }

    // Free variables (strings need special handling)
    if (runtime->variables) {
        for (size_t i = 0; i < runtime->variable_count; i++) {
            c64script_value_free(&runtime->variables[i].value);
        }
        free(runtime->variables);
        runtime->variables = NULL;
    }

    // Free stack (strings need special handling)
    if (runtime->stack) {
        for (size_t i = 0; i < runtime->stack_size; i++) {
            c64script_value_free(&runtime->stack[i]);
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

    char key[64];
    normalize_identifier(name, key);

    c64script_value_t stored_value = c64script_value_clone(value);
    if (stored_value.type == VALUE_STRING && !stored_value.as.string) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
        return false;
    }

    for (size_t i = 0; i < runtime->variable_count; i++) {
        if (strcmp(runtime->variables[i].name, key) == 0) {
            c64script_value_free(&runtime->variables[i].value);
            runtime->variables[i].value = stored_value;
            return true;
        }
    }

    if (runtime->variable_count >= runtime->variable_capacity) {
        size_t new_cap = runtime->variable_capacity == 0 ? 64 : runtime->variable_capacity * 2;
        if (new_cap > C64SCRIPT_MAX_VARIABLES) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Too many variables");
            c64script_value_free(&stored_value);
            return false;
        }

        c64script_variable_t *new_vars = realloc(runtime->variables, new_cap * sizeof(c64script_variable_t));
        if (!new_vars) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
            c64script_value_free(&stored_value);
            return false;
        }
        runtime->variables = new_vars;
        runtime->variable_capacity = new_cap;
    }

    c64script_variable_t *slot = &runtime->variables[runtime->variable_count++];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->name, key, sizeof(slot->name) - 1);
    slot->value = stored_value;
    return true;
}

bool c64script_runtime_get_var(c64script_runtime_t *runtime, const char *name, c64script_value_t *out_value)
{
    if (!runtime || !name || !out_value) {
        return false;
    }

    char key[64];
    normalize_identifier(name, key);

    for (size_t i = 0; i < runtime->variable_count; i++) {
        if (strcmp(runtime->variables[i].name, key) == 0) {
            *out_value = c64script_value_clone(runtime->variables[i].value);
            if (out_value->type == VALUE_STRING && !out_value->as.string) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                return false;
            }
            return true;
        }
    }

    *out_value = c64script_value_number(0.0);
    return true;
}

bool c64script_runtime_push(c64script_runtime_t *runtime, c64script_value_t value)
{
    if (!runtime) {
        return false;
    }

    if (runtime->stack_size >= C64SCRIPT_MAX_STACK_DEPTH) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Stack overflow");
        return false;
    }

    if (runtime->stack_size >= runtime->stack_capacity) {
        size_t new_cap = runtime->stack_capacity == 0 ? 64 : runtime->stack_capacity * 2;
        if (new_cap > C64SCRIPT_MAX_STACK_DEPTH) {
            new_cap = C64SCRIPT_MAX_STACK_DEPTH;
        }
        c64script_value_t *new_stack = realloc(runtime->stack, new_cap * sizeof(c64script_value_t));
        if (!new_stack) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
            return false;
        }
        runtime->stack = new_stack;
        runtime->stack_capacity = new_cap;
    }

    runtime->stack[runtime->stack_size++] = value;
    return true;
}

bool c64script_runtime_pop(c64script_runtime_t *runtime, c64script_value_t *out_value)
{
    if (!runtime || !out_value) {
        return false;
    }

    if (runtime->stack_size == 0) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Stack underflow");
        return false;
    }

    *out_value = runtime->stack[--runtime->stack_size];
    return true;
}
