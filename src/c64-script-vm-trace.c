/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <malloc.h>
#define alloca _alloca
#else
#include <alloca.h>
#endif

void c64script_vm_record_trace_entry(c64script_runtime_t *runtime, int line_num)
{
    if (!runtime->trace_recording_enabled || !runtime->trace_buffer || line_num <= 0) {
        return;
    }

    // Enforce 1k trace step limit (prevents huge traces in repo)
    if (runtime->trace_step_count >= 1000) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Trace step limit exceeded (1000 steps max)");
        runtime->should_stop = true;
        return;
    }
    runtime->trace_step_count++;

    char line_buffer[512];
    const char *src = runtime->source_text;
    if (!src) {
        snprintf(line_buffer, sizeof(line_buffer), "<line %d>", line_num);
    } else {
        // Extract line content
        int current_line = 1;
        const char *line_start = src;

        while (*src && current_line < line_num) {
            if (*src == '\n') {
                current_line++;
                line_start = src + 1;
            }
            src++;
        }

        if (current_line == line_num) {
            const char *line_end = line_start;
            while (*line_end && *line_end != '\n' && *line_end != '\r') {
                line_end++;
            }

            size_t len = line_end - line_start;
            if (len >= sizeof(line_buffer)) {
                len = sizeof(line_buffer) - 1;
            }
            memcpy(line_buffer, line_start, len);
            line_buffer[len] = '\0';

            // Trim
            char *trimmed = line_buffer;
            while (isspace((unsigned char)*trimmed))
                trimmed++;
            char *end = trimmed + strlen(trimmed) - 1;
            while (end > trimmed && isspace((unsigned char)*end))
                *end-- = '\0';
            memmove(line_buffer, trimmed, strlen(trimmed) + 1);
        } else {
            snprintf(line_buffer, sizeof(line_buffer), "<line %d not found>", line_num);
        }
    }

    // Write trace entry to buffer
    char entry[2048];
    int entry_len = 0;

    entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "- line: %d\n", line_num);
    entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "  content: ");

    // Write YAML-escaped string
    entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "\"");
    for (const char *p = line_buffer; *p && entry_len < (int)sizeof(entry) - 10; p++) {
        if (*p == '\"') {
            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "\\\"");
        } else if (*p == '\\') {
            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "\\\\");
        } else if (*p == '\n') {
            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "\\n");
        } else {
            entry[entry_len++] = *p;
        }
    }
    entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "\"\n");

    if (runtime->variable_count > 0) {
        entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "  variables:\n");
        for (size_t i = 0; i < runtime->variable_count && entry_len < (int)sizeof(entry) - 100; i++) {
            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "    %s: ", runtime->variables[i].name);

            // Write value as YAML
            c64script_value_t *val = &runtime->variables[i].value;
            if (val->type == VALUE_NUMBER) {
                entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "%.10g\n", val->as.number);
            } else if (val->type == VALUE_STRING) {
                entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "\"%s\"\n",
                                      val->as.string ? val->as.string : "");
            } else if (val->type == VALUE_ARRAY) {
                // Render array with first 10 elements
                entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "[");
                if (val->as.array) {
                    size_t max_elements = val->as.array->size < 10 ? val->as.array->size : 10;
                    for (size_t j = 0; j < max_elements && entry_len < (int)sizeof(entry) - 50; j++) {
                        if (j > 0) {
                            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, ", ");
                        }
                        c64script_value_t *elem = &val->as.array->elements[j];
                        if (elem->type == VALUE_NUMBER) {
                            entry_len +=
                                snprintf(entry + entry_len, sizeof(entry) - entry_len, "%.10g", elem->as.number);
                        } else if (elem->type == VALUE_STRING) {
                            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "\"%s\"",
                                                  elem->as.string ? elem->as.string : "");
                        } else {
                            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "~");
                        }
                    }
                    if (val->as.array->size > 10) {
                        entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, ", ...");
                    }
                }
                entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "]\n");
            } else if (val->type == VALUE_MAP) {
                // Render map with first 10 entries, sorted alphabetically by key
                entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "{");
                if (val->as.map && val->as.map->count > 0) {
                    // Create sorted index array
                    size_t *sorted_indices = alloca(val->as.map->count * sizeof(size_t));
                    for (size_t j = 0; j < val->as.map->count; j++) {
                        sorted_indices[j] = j;
                    }
                    // Simple bubble sort by key (good enough for small maps)
                    for (size_t j = 0; j < val->as.map->count - 1; j++) {
                        for (size_t k = j + 1; k < val->as.map->count; k++) {
                            if (strcmp(val->as.map->entries[sorted_indices[j]].key,
                                       val->as.map->entries[sorted_indices[k]].key) > 0) {
                                size_t temp = sorted_indices[j];
                                sorted_indices[j] = sorted_indices[k];
                                sorted_indices[k] = temp;
                            }
                        }
                    }
                    size_t max_entries = val->as.map->count < 10 ? val->as.map->count : 10;
                    for (size_t j = 0; j < max_entries && entry_len < (int)sizeof(entry) - 50; j++) {
                        if (j > 0) {
                            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, ", ");
                        }
                        c64script_map_entry_t *entry_ptr = &val->as.map->entries[sorted_indices[j]];
                        entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "%s: ", entry_ptr->key);
                        if (entry_ptr->value.type == VALUE_NUMBER) {
                            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "%.10g",
                                                  entry_ptr->value.as.number);
                        } else if (entry_ptr->value.type == VALUE_STRING) {
                            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "\"%s\"",
                                                  entry_ptr->value.as.string ? entry_ptr->value.as.string : "");
                        } else {
                            entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "~");
                        }
                    }
                    if (val->as.map->count > 10) {
                        entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, ", ...");
                    }
                }
                entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "}\n");
            } else {
                entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "~\n");
            }
        }
    } else {
        entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "  variables: {}\n");
    }

    // Append to trace buffer (expand if needed)
    while (runtime->trace_buffer_size + entry_len + 1 > runtime->trace_buffer_capacity) {
        runtime->trace_buffer_capacity *= 2;
        char *new_buffer = realloc(runtime->trace_buffer, runtime->trace_buffer_capacity);
        if (!new_buffer) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory for trace buffer");
            runtime->should_stop = true;
            return;
        }
        runtime->trace_buffer = new_buffer;
    }

    memcpy(runtime->trace_buffer + runtime->trace_buffer_size, entry, entry_len);
    runtime->trace_buffer_size += entry_len;
    runtime->trace_buffer[runtime->trace_buffer_size] = '\0';
}
