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
    } else if (value->type == VALUE_ARRAY && value->as.array) {
        // Free array elements
        for (size_t i = 0; i < value->as.array->size; i++) {
            c64script_value_free(&value->as.array->elements[i]);
        }
        free(value->as.array->elements);
        free(value->as.array);
        value->as.array = NULL;
    } else if (value->type == VALUE_MAP && value->as.map) {
        // Free map entries
        for (size_t i = 0; i < value->as.map->count; i++) {
            free(value->as.map->entries[i].key);
            c64script_value_free(&value->as.map->entries[i].value);
        }
        free(value->as.map->entries);
        free(value->as.map);
        value->as.map = NULL;
    }

    value->type = VALUE_NUMBER;
    value->as.number = 0.0;
}

c64script_value_t c64script_value_clone(c64script_value_t value)
{
    if (value.type == VALUE_STRING) {
        return c64script_value_string(value.as.string ? value.as.string : "");
    } else if (value.type == VALUE_ARRAY) {
        // For arrays, we just return a reference (shared ownership for now)
        // Full deep copy would be expensive
        return value;
    } else if (value.type == VALUE_MAP) {
        // For maps, we just return a reference (shared ownership for now)
        return value;
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

    runtime->max_iterations = 0; // 0 = unlimited
    runtime->iteration_count = 0;

    runtime->last_executed_line = 0;
    runtime->next_line_to_execute = 0;
    runtime->source_text = NULL;
    runtime->source_text_size = 0;

    runtime->log_file = NULL;
    runtime->log_filename[0] = '\0';

    runtime->error_msg[0] = '\0';
    runtime->error_line = 0;

    runtime->trace_recording_enabled = false;
    runtime->trace_first_entry = true;
    runtime->trace_file = NULL;
    runtime->trace_filename[0] = '\0';
    runtime->trace_step_count = 0;
    runtime->trace_buffer = NULL;
    runtime->trace_buffer_size = 0;
    runtime->trace_buffer_capacity = 0;

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

    // Close trace file if open
    if (runtime->trace_file) {
        fclose(runtime->trace_file);
        runtime->trace_file = NULL;
    }

    // Free trace buffer
    if (runtime->trace_buffer) {
        free(runtime->trace_buffer);
        runtime->trace_buffer = NULL;
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

    // Check local scope first (if in function)
    if (runtime->scope_stack_size > 0) {
        c64script_scope_t *scope = &runtime->scope_stack[runtime->scope_stack_size - 1];
        for (size_t i = 0; i < scope->local_var_count; i++) {
            if (strcmp(scope->local_vars[i].name, key) == 0) {
                c64script_value_free(&scope->local_vars[i].value);
                scope->local_vars[i].value = stored_value;
                return true;
            }
        }

        // Not found in local scope, create new local variable
        if (scope->local_var_count >= scope->local_var_capacity) {
            size_t new_cap = scope->local_var_capacity == 0 ? 4 : scope->local_var_capacity * 2;
            c64script_variable_t *new_vars = realloc(scope->local_vars, new_cap * sizeof(c64script_variable_t));
            if (!new_vars) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                c64script_value_free(&stored_value);
                return false;
            }
            scope->local_vars = new_vars;
            scope->local_var_capacity = new_cap;
        }

        c64script_variable_t *var = &scope->local_vars[scope->local_var_count++];
        strncpy(var->name, key, sizeof(var->name) - 1);
        var->name[sizeof(var->name) - 1] = '\0';
        var->value = stored_value;
        return true;
    }

    // Global scope: update existing or create new
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

    // Check local scope first (if in function)
    if (runtime->scope_stack_size > 0) {
        c64script_scope_t *scope = &runtime->scope_stack[runtime->scope_stack_size - 1];
        for (size_t i = 0; i < scope->local_var_count; i++) {
            if (strcmp(scope->local_vars[i].name, key) == 0) {
                *out_value = c64script_value_clone(scope->local_vars[i].value);
                if (out_value->type == VALUE_STRING && !out_value->as.string) {
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                    return false;
                }
                return true;
            }
        }
    }

    // Check global scope
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

// ============================================================================
// ARRAY OPERATIONS
// ============================================================================

c64script_value_t c64script_value_array(size_t size, c64script_value_type_t element_type)
{
    c64script_value_t value = {0};
    value.type = VALUE_ARRAY;
    value.as.array = calloc(1, sizeof(c64script_array_t));
    if (!value.as.array) {
        return c64script_value_number(0.0);
    }

    value.as.array->element_type = element_type;
    value.as.array->size = size;
    value.as.array->elements = calloc(size, sizeof(c64script_value_t));
    if (!value.as.array->elements) {
        free(value.as.array);
        value.type = VALUE_NUMBER;
        value.as.number = 0.0;
        return value;
    }

    // Initialize elements to default values
    for (size_t i = 0; i < size; i++) {
        if (element_type == VALUE_STRING) {
            value.as.array->elements[i] = c64script_value_string("");
        } else {
            value.as.array->elements[i] = c64script_value_number(0.0);
        }
    }

    return value;
}

bool c64script_array_get(c64script_array_t *array, size_t index, c64script_value_t *out_value)
{
    if (!array || !out_value) {
        return false;
    }

    if (index >= array->size) {
        return false; // Out of bounds
    }

    *out_value = c64script_value_clone(array->elements[index]);
    return true;
}

bool c64script_array_set(c64script_array_t *array, size_t index, c64script_value_t value)
{
    if (!array) {
        return false;
    }

    if (index >= array->size) {
        return false; // Out of bounds
    }

    c64script_value_free(&array->elements[index]);
    array->elements[index] = c64script_value_clone(value);
    return true;
}

// ============================================================================
// MAP OPERATIONS (Hash Table)
// ============================================================================

// FNV-1a hash function
static uint32_t hash_string(const char *str)
{
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint32_t)(unsigned char)(*str++);
        hash *= 16777619u;
    }
    return hash;
}

c64script_value_t c64script_value_map(c64script_value_type_t value_type)
{
    c64script_value_t value = {0};
    value.type = VALUE_MAP;
    value.as.map = calloc(1, sizeof(c64script_map_t));
    if (!value.as.map) {
        return c64script_value_number(0.0);
    }

    value.as.map->value_type = value_type;
    value.as.map->count = 0;
    value.as.map->capacity = 16; // Initial capacity
    value.as.map->entries = calloc(16, sizeof(c64script_map_entry_t));
    if (!value.as.map->entries) {
        free(value.as.map);
        value.type = VALUE_NUMBER;
        value.as.number = 0.0;
        return value;
    }

    return value;
}

bool c64script_map_get(c64script_map_t *map, const char *key, c64script_value_t *out_value)
{
    if (!map || !key || !out_value) {
        return false;
    }

    uint32_t hash = hash_string(key);

    // Linear search (simple for now)
    for (size_t i = 0; i < map->count; i++) {
        if (map->entries[i].hash == hash && strcmp(map->entries[i].key, key) == 0) {
            *out_value = c64script_value_clone(map->entries[i].value);
            return true;
        }
    }

    // Key not found, return default value
    if (map->value_type == VALUE_STRING) {
        *out_value = c64script_value_string("");
    } else {
        *out_value = c64script_value_number(0.0);
    }
    return true;
}

bool c64script_map_set(c64script_map_t *map, const char *key, c64script_value_t value)
{
    if (!map || !key) {
        return false;
    }

    uint32_t hash = hash_string(key);

    // Check if key already exists
    for (size_t i = 0; i < map->count; i++) {
        if (map->entries[i].hash == hash && strcmp(map->entries[i].key, key) == 0) {
            // Update existing entry
            c64script_value_free(&map->entries[i].value);
            map->entries[i].value = c64script_value_clone(value);
            return true;
        }
    }

    // Add new entry - grow if needed
    if (map->count >= map->capacity) {
        size_t new_capacity = map->capacity * 2;
        c64script_map_entry_t *new_entries = realloc(map->entries, new_capacity * sizeof(c64script_map_entry_t));
        if (!new_entries) {
            return false; // Out of memory
        }
        map->entries = new_entries;
        map->capacity = new_capacity;
    }

    // Insert new entry
    map->entries[map->count].key = strdup(key);
    if (!map->entries[map->count].key) {
        return false; // Out of memory
    }
    map->entries[map->count].hash = hash;
    map->entries[map->count].value = c64script_value_clone(value);
    map->count++;

    return true;
}

bool c64script_enable_trace_recording(c64script_runtime_t *runtime, const char *filename)
{
    if (!runtime || !filename) {
        return false;
    }

    strncpy(runtime->trace_filename, filename, sizeof(runtime->trace_filename) - 1);
    runtime->trace_filename[sizeof(runtime->trace_filename) - 1] = '\0';
    runtime->trace_recording_enabled = true;
    runtime->trace_first_entry = true;
    runtime->trace_step_count = 0;

    // Initialize trace buffer (will collect entries during execution)
    runtime->trace_buffer_capacity = 64 * 1024; // 64KB initial
    runtime->trace_buffer = malloc(runtime->trace_buffer_capacity);
    if (!runtime->trace_buffer) {
        runtime->trace_recording_enabled = false;
        return false;
    }
    runtime->trace_buffer_size = 0;

    return true;
}

void c64script_finalize_trace_recording(c64script_runtime_t *runtime, bool success, const char *error_msg)
{
    if (!runtime || !runtime->trace_recording_enabled) {
        return;
    }

    // Open file for writing
    FILE *f = fopen(runtime->trace_filename, "w");
    if (!f) {
        if (runtime->trace_buffer) {
            free(runtime->trace_buffer);
            runtime->trace_buffer = NULL;
        }
        runtime->trace_recording_enabled = false;
        return;
    }

    // Write header with status/error
    fprintf(f, "# Execution trace\n");
    const char *script_name = strrchr(runtime->trace_filename, '/');
    script_name = script_name ? script_name + 1 : runtime->trace_filename;
    // Remove .expected-trace.yaml suffix if present
    char clean_name[256];
    strncpy(clean_name, script_name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';
    char *dot = strstr(clean_name, ".expected-trace.yaml");
    if (dot) {
        // Replace with .c64script
        strcpy(dot, ".c64script");
    }

    fprintf(f, "script: \"%s\"\n", clean_name);
    fprintf(f, "status: %s\n", success ? "success" : "failure");

    if (error_msg && error_msg[0]) {
        fprintf(f, "error:\n");
        fprintf(f, "  line: %d\n", runtime->error_line > 0 ? runtime->error_line : 0);
        fprintf(f, "  message: \"");
        for (const char *p = error_msg; *p; p++) {
            if (*p == '"')
                fputs("\\\"", f);
            else if (*p == '\n')
                fputs("\\n", f);
            else if (*p == '\\')
                fputs("\\\\", f);
            else
                fputc(*p, f);
        }
        fprintf(f, "\"\n");
    } else {
        fprintf(f, "error: ~\n");
    }

    // Write program listing
    if (runtime->source_text) {
        // First pass: count total lines to determine padding
        const char *src_count = runtime->source_text;
        int max_line = 1;
        while (*src_count) {
            if (*src_count == '\n') {
                max_line++;
            }
            src_count++;
        }

        // Calculate padding width (number of digits)
        int padding_width = 1;
        int temp = max_line;
        while (temp >= 10) {
            padding_width++;
            temp /= 10;
        }

        fprintf(f, "program: |\n");
        const char *src = runtime->source_text;
        int line_num = 1;
        const char *line_start = src;

        while (*src) {
            if (*src == '\n' || *src == '\r') {
                fprintf(f, "  %0*d: ", padding_width, line_num);
                fwrite(line_start, 1, src - line_start, f);
                fprintf(f, "\n");

                if (*src == '\r' && *(src + 1) == '\n') {
                    src++;
                }
                src++;
                line_start = src;
                line_num++;
            } else {
                src++;
            }
        }

        if (line_start < src) {
            fprintf(f, "  %0*d: ", padding_width, line_num);
            fwrite(line_start, 1, src - line_start, f);
            fprintf(f, "\n");
        }
    }

    // Write trace entries from buffer
    fprintf(f, "trace:\n");
    if (runtime->trace_buffer && runtime->trace_buffer_size > 0) {
        fwrite(runtime->trace_buffer, 1, runtime->trace_buffer_size, f);
    }

    fclose(f);

    // Clean up
    if (runtime->trace_buffer) {
        free(runtime->trace_buffer);
        runtime->trace_buffer = NULL;
    }
    runtime->trace_recording_enabled = false;
}
