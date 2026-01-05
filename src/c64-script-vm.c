/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script.h"
#include "c64-script-vm.h"
#include "c64-script-builtins.h"
#include "c64-script-runtime.h"
#include "c64-logging.h"
#include "c64-keyboard.h"
#include "c64-rest-client.h"

#include <ctype.h>
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
#else
#include <strings.h>
#include <sys/wait.h>
#endif
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <util/platform.h>

#define MACRO_LOG_PREFIX "[c64script-vm] "

static double wallclock_now_seconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static bool parse_fixed_digits(const char *s, size_t len, int *out)
{
    int value = 0;
    if (!s || !out || len == 0) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        value = value * 10 + (s[i] - '0');
    }
    *out = value;
    return true;
}

#ifdef _WIN32
static time_t timegm_compat(struct tm *tm_utc)
{
    return _mkgmtime(tm_utc);
}
#else
static time_t timegm_compat(struct tm *tm_utc)
{
    return timegm(tm_utc);
}
#endif

static bool parse_wallclock_target(const char *s, double *out_epoch_seconds)
{
    if (!s || !out_epoch_seconds) {
        return false;
    }

    size_t n = strlen(s);

    // "HH:MM" or "HH:MM:SS" (local time; tomorrow if already passed)
    if (n == 5 || n == 8) {
        int hh = 0, mm = 0, ss = 0;
        if (!parse_fixed_digits(s + 0, 2, &hh) || s[2] != ':' || !parse_fixed_digits(s + 3, 2, &mm)) {
            return false;
        }
        if (n == 8) {
            if (s[5] != ':' || !parse_fixed_digits(s + 6, 2, &ss)) {
                return false;
            }
        }
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
            return false;
        }

        time_t now_t = time(NULL);
        struct tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &now_t);
#else
        localtime_r(&now_t, &local_tm);
#endif
        local_tm.tm_hour = hh;
        local_tm.tm_min = mm;
        local_tm.tm_sec = ss;
        time_t target = mktime(&local_tm);
        if (target == (time_t)-1) {
            return false;
        }

        if ((double)target <= wallclock_now_seconds()) {
            target += 24 * 60 * 60;
        }

        *out_epoch_seconds = (double)target;
        return true;
    }

    // "YYYY-MM-DD HH:MM:SS" (local time)
    if (n == 19 && s[4] == '-' && s[7] == '-' && s[10] == ' ' && s[13] == ':' && s[16] == ':') {
        int year = 0, mon = 0, day = 0, hh = 0, mm = 0, ss = 0;
        if (!parse_fixed_digits(s + 0, 4, &year) || !parse_fixed_digits(s + 5, 2, &mon) ||
            !parse_fixed_digits(s + 8, 2, &day) || !parse_fixed_digits(s + 11, 2, &hh) ||
            !parse_fixed_digits(s + 14, 2, &mm) || !parse_fixed_digits(s + 17, 2, &ss)) {
            return false;
        }

        struct tm tm_local = {0};
        tm_local.tm_year = year - 1900;
        tm_local.tm_mon = mon - 1;
        tm_local.tm_mday = day;
        tm_local.tm_hour = hh;
        tm_local.tm_min = mm;
        tm_local.tm_sec = ss;
        time_t target = mktime(&tm_local);
        if (target == (time_t)-1) {
            return false;
        }
        *out_epoch_seconds = (double)target;
        return true;
    }

    // ISO-8601 "YYYY-MM-DDTHH:MM:SS[.fff][Z|±HH:MM]"
    const char *t_pos = strchr(s, 'T');
    if (!t_pos) {
        return false;
    }

    if ((size_t)(t_pos - s) != 10 || s[4] != '-' || s[7] != '-') {
        return false;
    }

    int year = 0, mon = 0, day = 0;
    if (!parse_fixed_digits(s + 0, 4, &year) || !parse_fixed_digits(s + 5, 2, &mon) ||
        !parse_fixed_digits(s + 8, 2, &day)) {
        return false;
    }

    const char *time_part = t_pos + 1;
    if (strlen(time_part) < 8 || time_part[2] != ':' || time_part[5] != ':') {
        return false;
    }

    int hh = 0, mm = 0, ss = 0;
    if (!parse_fixed_digits(time_part + 0, 2, &hh) || !parse_fixed_digits(time_part + 3, 2, &mm) ||
        !parse_fixed_digits(time_part + 6, 2, &ss)) {
        return false;
    }

    const char *rest = time_part + 8;

    // Optional fractional seconds
    if (*rest == '.') {
        rest++;
        while (*rest >= '0' && *rest <= '9') {
            rest++;
        }
    }

    bool has_tz = false;
    int tz_sign = 1;
    int tz_hh = 0, tz_mm = 0;
    if (*rest == 'Z') {
        has_tz = true;
        tz_sign = 1;
        tz_hh = 0;
        tz_mm = 0;
        rest++;
    } else if (*rest == '+' || *rest == '-') {
        has_tz = true;
        tz_sign = (*rest == '-') ? -1 : 1;
        rest++;
        if (!parse_fixed_digits(rest + 0, 2, &tz_hh) || rest[2] != ':' || !parse_fixed_digits(rest + 3, 2, &tz_mm)) {
            return false;
        }
        rest += 5;
    }

    if (*rest != '\0') {
        return false;
    }

    struct tm tm_val = {0};
    tm_val.tm_year = year - 1900;
    tm_val.tm_mon = mon - 1;
    tm_val.tm_mday = day;
    tm_val.tm_hour = hh;
    tm_val.tm_min = mm;
    tm_val.tm_sec = ss;

    time_t base;
    if (has_tz) {
        base = timegm_compat(&tm_val);
        if (base == (time_t)-1) {
            return false;
        }
        int offset_seconds = tz_sign * ((tz_hh * 60 + tz_mm) * 60);
        *out_epoch_seconds = (double)base - (double)offset_seconds;
        return true;
    }

    base = mktime(&tm_val);
    if (base == (time_t)-1) {
        return false;
    }
    *out_epoch_seconds = (double)base;
    return true;
}

static bool is_c64u_path(const char *path, const char **out_c64u_path)
{
    if (!path || !out_c64u_path) {
        return false;
    }

    const char *prefix = "c64u:";
    size_t prefix_len = strlen(prefix);
    if (strlen(path) < prefix_len) {
        return false;
    }
    if (strncasecmp(path, prefix, prefix_len) != 0) {
        return false;
    }

    *out_c64u_path = path + prefix_len;
    return true;
}

static bool load_binary_file(const char *path, uint8_t **out_data, size_t *out_size, char *error_msg, size_t error_size)
{
    if (!path || !out_data || !out_size) {
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to open file: %s", path);
        }
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to seek file");
        }
        return false;
    }

    long fsize = ftell(f);
    if (fsize <= 0 || fsize > (long)(16 * 1024 * 1024)) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "File too large");
        }
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to seek file");
        }
        return false;
    }

    uint8_t *data = malloc((size_t)fsize);
    if (!data) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Out of memory");
        }
        return false;
    }

    size_t read_count = fread(data, 1, (size_t)fsize, f);
    fclose(f);
    if (read_count != (size_t)fsize) {
        free(data);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to read file");
        }
        return false;
    }

    *out_data = data;
    *out_size = (size_t)fsize;
    return true;
}

// Helper: Load text file as string
static bool load_text_file(const char *path, char **out_content, char *error_msg, size_t error_size)
{
    if (!path || !out_content) {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to open file: %s", path);
        }
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to seek file");
        }
        return false;
    }

    long fsize = ftell(f);
    if (fsize < 0 || fsize > (long)(16 * 1024 * 1024)) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, fsize < 0 ? "Failed to get file size" : "File too large");
        }
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to seek file");
        }
        return false;
    }

    char *content = malloc((size_t)fsize + 1);
    if (!content) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Out of memory");
        }
        return false;
    }

    size_t read_count = fread(content, 1, (size_t)fsize, f);
    fclose(f);
    content[read_count] = '\0';

    *out_content = content;
    return true;
}

// Helper: Write file
static bool write_file(const char *path, const char *content, bool truncate, char *error_msg, size_t error_size)
{
    if (!path || !content) {
        return false;
    }

    const char *mode = truncate ? "w" : "a";
    FILE *f = fopen(path, mode);
    if (!f) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to open file: %s", path);
        }
        return false;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    if (written != len) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to write to file");
        }
        return false;
    }

    return true;
}

static const char *file_extension_lower(const char *path)
{
    if (!path) {
        return "";
    }
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) {
        return "";
    }
    return dot + 1;
}

static bool require_number(c64script_runtime_t *runtime, const c64script_value_t *value, const char *what)
{
    if (value->type == VALUE_NUMBER) {
        return true;
    }
    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (%s)", what);
    return false;
}

// ============================================================================
// VM EXECUTION
// ============================================================================

// Helper to escape YAML strings (only if needed)
static void write_yaml_string(FILE *f, const char *str)
{
    // Check if we need quotes (contains special chars, starts with special chars, etc.)
    bool needs_quotes = false;
    if (!str || !*str) {
        fprintf(f, "''");
        return;
    }

    // Simple heuristic: quote if contains : or starts with special chars
    if (strchr(str, ':') || strchr(str, '#') || strchr(str, '\n') || str[0] == '-' || str[0] == '[' || str[0] == '{') {
        needs_quotes = true;
    }

    if (needs_quotes) {
        fputc('\'', f);
        while (*str) {
            if (*str == '\'') {
                fputs("''", f); // Escape single quotes by doubling
            } else {
                fputc(*str, f);
            }
            str++;
        }
        fputc('\'', f);
    } else {
        fputs(str, f);
    }
}

// Helper to write a c64script value as YAML
static void write_value_as_yaml(FILE *f, const c64script_value_t *val)
{
    switch (val->type) {
    case VALUE_NUMBER:
        fprintf(f, "%.17g", val->as.number);
        break;
    case VALUE_STRING:
        write_yaml_string(f, val->as.string);
        break;
    case VALUE_ARRAY:
        fprintf(f, "<array>");
        break;
    case VALUE_MAP:
        fprintf(f, "<map>");
        break;
    default:
        fprintf(f, "<unknown>");
        break;
    }
}

// Record trace entry for current line
static void record_trace_entry(c64script_runtime_t *runtime, int line_num)
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
        if (*p == '"') {
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
                entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "[array]\n");
            } else if (val->type == VALUE_MAP) {
                entry_len += snprintf(entry + entry_len, sizeof(entry) - entry_len, "{map}\n");
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

bool c64script_execute(c64script_runtime_t *runtime)
{
    bool result = c64script_vm_execute(runtime);

    // Finalize trace recording with status and error (if any)
    if (runtime && runtime->trace_recording_enabled) {
        c64script_finalize_trace_recording(runtime, result, result ? NULL : runtime->error_msg);
    }

    return result;
}

bool c64script_vm_execute(c64script_runtime_t *runtime)
{
    if (!runtime) {
        blog(LOG_ERROR, "NULL runtime provided");
        return false;
    }

    if (!runtime->bytecode || runtime->bytecode_size == 0) {
        blog(LOG_ERROR, "No bytecode to execute");
        return false;
    }

    runtime->ip = 0;
    runtime->should_stop = false;
    runtime->last_executed_line = 0;
    runtime->next_line_to_execute = runtime->bytecode_size > 0 ? runtime->bytecode[0].source_line : 0;
    runtime->iteration_count = 0;

    while (runtime->ip < runtime->bytecode_size && !runtime->should_stop) {
        c64script_instruction_t *instr = &runtime->bytecode[runtime->ip];
        int current_line = instr->source_line;

        // Check iteration limit (for testing to prevent infinite loops)
        if (runtime->max_iterations > 0 && ++runtime->iteration_count >= runtime->max_iterations) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Iteration limit exceeded (%llu iterations)",
                     (unsigned long long)runtime->max_iterations);
            runtime->should_stop = true;
            return false;
        }

        // Check for pause at source line boundaries
        // Only pause when the line number changes (new source line)
        if (runtime->should_pause && current_line != runtime->last_executed_line && current_line > 0) {
            runtime->is_paused = true;
            runtime->should_pause = false; // Clear pause request

            // Wait until resumed or stopped
            while (runtime->is_paused && !runtime->should_stop) {
                os_sleep_ms(10); // Small sleep to avoid busy wait

                // Check if step mode is activated
                if (runtime->step_mode) {
                    runtime->step_mode = false;
                    break; // Execute one line then pause again
                }
            }

            if (runtime->should_stop) {
                break;
            }
        }

        runtime->error_line = instr->source_line;

        // Record trace entry BEFORE instruction execution (to show pre-execution variable state)
        if (runtime->trace_recording_enabled && current_line != runtime->last_executed_line && current_line > 0) {
            record_trace_entry(runtime, current_line);
        }

        if (runtime->trace_enabled) {
            blog(LOG_INFO, "[TRACE] IP=%zu OP=%d line=%d", runtime->ip, instr->opcode, instr->source_line);
        }

        runtime->ip++; // Advance IP (jumps will override)

        c64script_value_t a, b, result;

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

            c64script_value_t array = c64script_value_array(size, VALUE_NUMBER); // Default to number type
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
                map_var = c64script_value_map(VALUE_NUMBER); // Default to number type
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
            if (!c64script_runtime_get_var(runtime, mapname, &map_var)) {
                // Auto-create map if it doesn't exist (use value type from first value)
                map_var = c64script_value_map(value_val.type);
            }

            if (map_var.type != VALUE_MAP) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' is not a map", mapname);
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
            if (!require_number(runtime, &a, "EQ") || !require_number(runtime, &b, "EQ")) {
                c64script_value_free(&a);
                c64script_value_free(&b);
                return false;
            }
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number == b.as.number) ? 1.0 : 0.0;
            c64script_value_free(&a);
            c64script_value_free(&b);
            if (!c64script_runtime_push(runtime, result))
                return false;
            break;

        case OP_NE:
            if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
                return false;
            if (!require_number(runtime, &a, "NE") || !require_number(runtime, &b, "NE")) {
                c64script_value_free(&a);
                c64script_value_free(&b);
                return false;
            }
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number != b.as.number) ? 1.0 : 0.0;
            c64script_value_free(&a);
            c64script_value_free(&b);
            if (!c64script_runtime_push(runtime, result))
                return false;
            break;

        case OP_LT:
            if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
                return false;
            if (!require_number(runtime, &a, "LT") || !require_number(runtime, &b, "LT")) {
                c64script_value_free(&a);
                c64script_value_free(&b);
                return false;
            }
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number < b.as.number) ? 1.0 : 0.0;
            c64script_value_free(&a);
            c64script_value_free(&b);
            if (!c64script_runtime_push(runtime, result))
                return false;
            break;

        case OP_LE:
            if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
                return false;
            if (!require_number(runtime, &a, "LE") || !require_number(runtime, &b, "LE")) {
                c64script_value_free(&a);
                c64script_value_free(&b);
                return false;
            }
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number <= b.as.number) ? 1.0 : 0.0;
            c64script_value_free(&a);
            c64script_value_free(&b);
            if (!c64script_runtime_push(runtime, result))
                return false;
            break;

        case OP_GT:
            if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
                return false;
            if (!require_number(runtime, &a, "GT") || !require_number(runtime, &b, "GT")) {
                c64script_value_free(&a);
                c64script_value_free(&b);
                return false;
            }
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number > b.as.number) ? 1.0 : 0.0;
            c64script_value_free(&a);
            c64script_value_free(&b);
            if (!c64script_runtime_push(runtime, result))
                return false;
            break;

        case OP_GE:
            if (!c64script_runtime_pop(runtime, &b) || !c64script_runtime_pop(runtime, &a))
                return false;
            if (!require_number(runtime, &a, "GE") || !require_number(runtime, &b, "GE")) {
                c64script_value_free(&a);
                c64script_value_free(&b);
                return false;
            }
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number >= b.as.number) ? 1.0 : 0.0;
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

        case OP_RETURN: {
            // Check if we're in a function scope or GOSUB
            if (runtime->scope_stack_size > 0) {
                // Function return
                c64script_scope_t *scope = &runtime->scope_stack[runtime->scope_stack_size - 1];

                // Get return value if present (operand = 1 means yes, 0 means no)
                c64script_value_t return_val = {.type = VALUE_NUMBER, .as.number = 0.0};
                if (instr->operand != 0) {
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
                continue; // Skip ip increment
            } else if (runtime->gosub_stack_size > 0) {
                // GOSUB return
                bool has_return_value = (instr->operand != 0);
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

            // Skip waiting in step mode or when paused to avoid blocking debugging
            if (runtime->is_paused || runtime->step_mode) {
                break;
            }

            double multiplier = 1000.0;
            if (instr->operand == C64SCRIPT_WAIT_UNIT_MS) {
                multiplier = 1.0;
            } else if (instr->operand == C64SCRIPT_WAIT_UNIT_S) {
                multiplier = 1000.0;
            } else if (instr->operand == C64SCRIPT_WAIT_UNIT_M) {
                multiplier = 60000.0;
            }

            uint64_t remaining_ms = (uint64_t)(v * multiplier);
            while (remaining_ms > 0 && !runtime->should_stop) {
                uint64_t step = remaining_ms > 50 ? 50 : remaining_ms;
                os_sleep_ms((uint32_t)step);
                remaining_ms -= step;
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

            // Skip waiting in step mode or when paused to avoid blocking debugging
            if (runtime->is_paused || runtime->step_mode) {
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

        // Built-in function calls (PEEK)
        case OP_CALL_PEEK: {
            // Pop address from stack
            c64script_value_t addr_val;
            if (!c64script_runtime_pop(runtime, &addr_val))
                return false;
            if (!require_number(runtime, &addr_val, "PEEK")) {
                c64script_value_free(&addr_val);
                return false;
            }

            uint16_t address = (uint16_t)addr_val.as.number;

            // Call PEEK built-in (currently returns 0 as placeholder)
            double result;
            if (!c64script_builtin_peek(runtime, address, &result)) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "PEEK failed");
                c64script_value_free(&addr_val);
                return false;
            }

            // Push result onto stack
            c64script_value_t result_val = {.type = VALUE_NUMBER, .as.number = result};
            c64script_value_free(&addr_val);
            if (!c64script_runtime_push(runtime, result_val))
                return false;
            break;
        }

        case OP_CALL_STR: {
            // Pop number from stack
            c64script_value_t num_val;
            if (!c64script_runtime_pop(runtime, &num_val))
                return false;
            if (!require_number(runtime, &num_val, "STR")) {
                c64script_value_free(&num_val);
                return false;
            }

            // Convert number to string
            char str_buf[64];
            if (!c64script_builtin_str(num_val.as.number, str_buf, sizeof(str_buf))) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "STR failed");
                c64script_value_free(&num_val);
                return false;
            }

            // Push string result onto stack
            c64script_value_t result_val = c64script_value_string(str_buf);
            c64script_value_free(&num_val);
            if (!c64script_runtime_push(runtime, result_val)) {
                c64script_value_free(&result_val);
                return false;
            }
            break;
        }

        case OP_CALL_BUILTIN:
            blog(LOG_WARNING, "Generic built-in function calls not yet implemented");
            break;

        // Plugin actions - these will be connected to REST API in Phase 6
        case OP_PUSH_NUM:
            // Push immediate number onto stack (used by some plugin actions)
            result.type = VALUE_NUMBER;
            result.as.number = (double)instr->operand;
            if (!c64script_runtime_push(runtime, result))
                return false;
            break;

        case OP_EFFECT: {
            // EFFECT preset_name - Apply effect preset
            c64script_value_t preset;
            if (!c64script_runtime_pop(runtime, &preset))
                return false;
            if (preset.type != VALUE_STRING) {
                c64script_value_free(&preset);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (EFFECT)");
                return false;
            }
            if (!runtime->obs_source) {
                // OBS source not available - log warning but continue execution
                blog(LOG_WARNING, "[c64script] EFFECT: OBS source not available, skipping");
                c64script_value_free(&preset);
                break;
            }

            obs_source_t *source = (obs_source_t *)runtime->obs_source;
            obs_data_t *settings = obs_source_get_settings(source);
            if (!settings) {
                c64script_value_free(&preset);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to get source settings");
                return false;
            }
            obs_data_set_string(settings, "crt_preset", preset.as.string ? preset.as.string : "");
            obs_source_update(source, settings);
            obs_data_release(settings);
            c64script_value_free(&preset);
            break;
        }

        case OP_EFFECTPARAM: {
            // EFFECTPARAM param_name value - Set effect parameter
            c64script_value_t value, param;
            if (!c64script_runtime_pop(runtime, &value) || !c64script_runtime_pop(runtime, &param))
                return false;
            if (param.type != VALUE_STRING || value.type != VALUE_NUMBER) {
                c64script_value_free(&param);
                c64script_value_free(&value);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (EFFECTPARAM)");
                return false;
            }
            if (!runtime->obs_source) {
                // OBS source not available - log warning but continue execution
                blog(LOG_WARNING, "[c64script] EFFECTPARAM: OBS source not available, skipping");
                c64script_value_free(&param);
                c64script_value_free(&value);
                break;
            }

            obs_source_t *source = (obs_source_t *)runtime->obs_source;
            obs_data_t *settings = obs_source_get_settings(source);
            if (!settings) {
                c64script_value_free(&param);
                c64script_value_free(&value);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to get source settings");
                return false;
            }
            obs_data_set_double(settings, param.as.string ? param.as.string : "", value.as.number);
            obs_source_update(source, settings);
            obs_data_release(settings);
            c64script_value_free(&param);
            c64script_value_free(&value);
            break;
        }

        case OP_PALETTE: {
            // PALETTE palette_name - Load palette
            c64script_value_t palette;
            if (!c64script_runtime_pop(runtime, &palette))
                return false;
            if (palette.type != VALUE_STRING) {
                c64script_value_free(&palette);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (PALETTE)");
                return false;
            }
            if (!runtime->obs_source) {
                // OBS source not available - log warning but continue execution
                blog(LOG_WARNING, "[c64script] PALETTE: OBS source not available, skipping");
                c64script_value_free(&palette);
                break;
            }

            obs_source_t *source = (obs_source_t *)runtime->obs_source;
            obs_data_t *settings = obs_source_get_settings(source);
            if (!settings) {
                c64script_value_free(&palette);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to get source settings");
                return false;
            }
            obs_data_set_string(settings, "palette", palette.as.string ? palette.as.string : "");
            obs_source_update(source, settings);
            obs_data_release(settings);
            c64script_value_free(&palette);
            break;
        }

        case OP_PALETTECOLOR: {
            // PALETTECOLOR index, r, g, b - Set individual palette color
            c64script_value_t b_val, g_val, r_val, index_val;
            if (!c64script_runtime_pop(runtime, &b_val))
                return false;
            if (!c64script_runtime_pop(runtime, &g_val)) {
                c64script_value_free(&b_val);
                return false;
            }
            if (!c64script_runtime_pop(runtime, &r_val)) {
                c64script_value_free(&b_val);
                c64script_value_free(&g_val);
                return false;
            }
            if (!c64script_runtime_pop(runtime, &index_val)) {
                c64script_value_free(&b_val);
                c64script_value_free(&g_val);
                c64script_value_free(&r_val);
                return false;
            }

            if (index_val.type != VALUE_NUMBER || r_val.type != VALUE_NUMBER || g_val.type != VALUE_NUMBER ||
                b_val.type != VALUE_NUMBER) {
                c64script_value_free(&index_val);
                c64script_value_free(&r_val);
                c64script_value_free(&g_val);
                c64script_value_free(&b_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (PALETTECOLOR)");
                return false;
            }

            int index = (int)index_val.as.number;
            int r = (int)r_val.as.number;
            int g = (int)g_val.as.number;
            int b = (int)b_val.as.number;

            c64script_value_free(&index_val);
            c64script_value_free(&r_val);
            c64script_value_free(&g_val);
            c64script_value_free(&b_val);

            if (index < 0 || index > 15) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY (palette index)");
                return false;
            }
            if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY (RGB values)");
                return false;
            }

            if (!runtime->obs_source) {
                // OBS source not available - log warning but continue execution
                blog(LOG_WARNING, "[c64script] PALETTECOLOR: OBS source not available, skipping");
                break;
            }

            obs_source_t *source = (obs_source_t *)runtime->obs_source;
            obs_data_t *settings = obs_source_get_settings(source);
            if (!settings) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to get source settings");
                return false;
            }

            // Set the custom color in OBS settings
            // Format: "custom_color_N" with RGB value as a 32-bit integer
            char color_key[32];
            snprintf(color_key, sizeof(color_key), "custom_color_%d", index);
            uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            obs_data_set_int(settings, color_key, (int64_t)rgb);
            obs_source_update(source, settings);
            obs_data_release(settings);
            break;
        }

        case OP_PLAYSID: {
            // PLAYSID sid_file [SONGNR song_number]
            c64script_value_t song_nr, sid_file;
            if (!c64script_runtime_pop(runtime, &song_nr) || !c64script_runtime_pop(runtime, &sid_file))
                return false;
            if (!runtime->rest_client) {
                c64script_value_free(&sid_file);
                c64script_value_free(&song_nr);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
                return false;
            }
            if (sid_file.type != VALUE_STRING || song_nr.type != VALUE_NUMBER) {
                c64script_value_free(&sid_file);
                c64script_value_free(&song_nr);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (PLAYSID)");
                return false;
            }

            const char *c64u_path = NULL;
            bool ok = false;
            if (is_c64u_path(sid_file.as.string, &c64u_path)) {
                ok = c64_rest_play_sid_path((c64_rest_client_t *)runtime->rest_client, c64u_path,
                                            (int)song_nr.as.number);
            } else {
                uint8_t *data = NULL;
                size_t size = 0;
                char err[256] = {0};
                if (!load_binary_file(sid_file.as.string, &data, &size, err, sizeof(err))) {
                    c64script_value_free(&sid_file);
                    c64script_value_free(&song_nr);
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to load SID");
                    return false;
                }
                ok = c64_rest_play_sid((c64_rest_client_t *)runtime->rest_client, data, size, (int)song_nr.as.number);
                free(data);
            }
            if (!ok) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "PLAYSID failed: %s",
                         c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
                c64script_value_free(&sid_file);
                c64script_value_free(&song_nr);
                return false;
            }
            c64script_value_free(&sid_file);
            c64script_value_free(&song_nr);
            break;
        }

        case OP_RUNPRG: {
            // RUNPRG prg_file - Run a PRG file
            c64script_value_t prg_file;
            if (!c64script_runtime_pop(runtime, &prg_file))
                return false;
            if (!runtime->rest_client) {
                c64script_value_free(&prg_file);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
                return false;
            }
            if (prg_file.type != VALUE_STRING) {
                c64script_value_free(&prg_file);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (RUNPRG)");
                return false;
            }

            const char *c64u_path = NULL;
            bool ok = false;
            if (is_c64u_path(prg_file.as.string, &c64u_path)) {
                ok = c64_rest_run_prg_path((c64_rest_client_t *)runtime->rest_client, c64u_path);
            } else {
                uint8_t *data = NULL;
                size_t size = 0;
                char err[256] = {0};
                if (!load_binary_file(prg_file.as.string, &data, &size, err, sizeof(err))) {
                    c64script_value_free(&prg_file);
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to load PRG");
                    return false;
                }
                ok = c64_rest_run_prg((c64_rest_client_t *)runtime->rest_client, data, size);
                free(data);
            }
            if (!ok) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RUNPRG failed: %s",
                         c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
                c64script_value_free(&prg_file);
                return false;
            }
            c64script_value_free(&prg_file);
            break;
        }

        case OP_RUNLOCAL: {
            // RUNLOCAL path args status_var output_var
            // Pop in reverse order: output_var, status_var, args, path
            c64script_value_t output_var_val, status_var_val, args_val, path_val;
            if (!c64script_runtime_pop(runtime, &output_var_val))
                return false;
            if (!c64script_runtime_pop(runtime, &status_var_val))
                return false;
            if (!c64script_runtime_pop(runtime, &args_val))
                return false;
            if (!c64script_runtime_pop(runtime, &path_val))
                return false;

            // Validate types
            if (path_val.type != VALUE_STRING || args_val.type != VALUE_STRING || status_var_val.type != VALUE_STRING ||
                output_var_val.type != VALUE_STRING) {
                c64script_value_free(&path_val);
                c64script_value_free(&args_val);
                c64script_value_free(&status_var_val);
                c64script_value_free(&output_var_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (RUNLOCAL)");
                return false;
            }

            // Build command
            char cmd[2048];
            if (args_val.as.string[0] == '\0') {
                snprintf(cmd, sizeof(cmd), "%s 2>&1", path_val.as.string);
            } else {
                snprintf(cmd, sizeof(cmd), "%s %s 2>&1", path_val.as.string, args_val.as.string);
            }

            // Execute and capture output
            FILE *pipe = popen(cmd, "r");
            if (!pipe) {
                c64script_value_free(&path_val);
                c64script_value_free(&args_val);
                c64script_value_free(&status_var_val);
                c64script_value_free(&output_var_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to execute: %s", path_val.as.string);
                return false;
            }

            // Capture output (up to 1 MB)
            const size_t max_output = 1024 * 1024;
            char *output = malloc(max_output);
            if (!output) {
                pclose(pipe);
                c64script_value_free(&path_val);
                c64script_value_free(&args_val);
                c64script_value_free(&status_var_val);
                c64script_value_free(&output_var_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                return false;
            }

            size_t total_read = 0;
            while (total_read < max_output - 1) {
                size_t nread = fread(output + total_read, 1, max_output - 1 - total_read, pipe);
                if (nread == 0)
                    break;
                total_read += nread;
            }
            output[total_read] = '\0';

            int exit_code = pclose(pipe);
#ifndef _WIN32
            // On Unix, exit_code is the status from waitpid, need to extract actual exit code
            if (WIFEXITED(exit_code)) {
                exit_code = WEXITSTATUS(exit_code);
            } else {
                exit_code = -1; // Process didn't exit normally
            }
#endif

            // Store status if requested
            if (status_var_val.as.string[0] != '\0') {
                c64script_value_t status = {.type = VALUE_NUMBER, .as.number = (double)exit_code};
                c64script_runtime_set_var(runtime, status_var_val.as.string, status);
            }

            // Store output if requested
            if (output_var_val.as.string[0] != '\0') {
                c64script_value_t output_value = {.type = VALUE_STRING, .as.string = output};
                c64script_runtime_set_var(runtime, output_var_val.as.string, output_value);
            }

            free(output);
            c64script_value_free(&path_val);
            c64script_value_free(&args_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&output_var_val);
            break;
        }

        case OP_MOUNTDISK: {
            // MOUNTDISK disk_file - Mount a disk image
            c64script_value_t disk_file;
            if (!c64script_runtime_pop(runtime, &disk_file))
                return false;
            if (!runtime->rest_client) {
                c64script_value_free(&disk_file);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
                return false;
            }
            if (disk_file.type != VALUE_STRING) {
                c64script_value_free(&disk_file);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (MOUNTDISK)");
                return false;
            }

            const char *c64u_path = NULL;
            bool ok = false;
            if (is_c64u_path(disk_file.as.string, &c64u_path)) {
                ok = c64_rest_mount_disk_path((c64_rest_client_t *)runtime->rest_client, 'a', c64u_path);
            } else {
                const char *ext = file_extension_lower(disk_file.as.string);
                const char *type = NULL;
                if (strcasecmp(ext, "d64") == 0) {
                    type = "d64";
                } else if (strcasecmp(ext, "d81") == 0) {
                    type = "d81";
                } else {
                    c64script_value_free(&disk_file);
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Unsupported disk type");
                    return false;
                }

                uint8_t *data = NULL;
                size_t size = 0;
                char err[256] = {0};
                if (!load_binary_file(disk_file.as.string, &data, &size, err, sizeof(err))) {
                    c64script_value_free(&disk_file);
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s",
                             err[0] ? err : "Failed to load disk");
                    return false;
                }
                ok = c64_rest_mount_disk((c64_rest_client_t *)runtime->rest_client, 'a', type, "readonly", data, size);
                free(data);
            }
            if (!ok) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "MOUNTDISK failed: %s",
                         c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
                c64script_value_free(&disk_file);
                return false;
            }
            c64script_value_free(&disk_file);
            break;
        }

        case OP_AUTOSTART:
            if (!runtime->keyboard) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Keyboard not available");
                return false;
            }
            {
                c64_output_t output = {0};
                output.mode = C64_OUTPUT_TEXT;
                snprintf(output.data.text, sizeof(output.data.text), "%s", "LOAD\"*\",8,1\rRUN\r");
                c64_keyboard_queue_output((c64_keyboard_t *)runtime->keyboard, &output);
            }
            break;

        case OP_RESET:
            if (!runtime->rest_client) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
                return false;
            }
            if (!c64_rest_reset((c64_rest_client_t *)runtime->rest_client)) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RESET failed: %s",
                         c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
                return false;
            }
            break;

        case OP_REBOOT:
            if (!runtime->rest_client) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
                return false;
            }
            if (!c64_rest_reboot((c64_rest_client_t *)runtime->rest_client)) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REBOOT failed: %s",
                         c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
                return false;
            }
            break;

        case OP_RECORDSTART:
#ifdef ENABLE_FRONTEND_API
            obs_frontend_recording_start();
#else
            // Frontend API not enabled - log warning but continue execution
            blog(LOG_WARNING, "[c64script] RECORDSTART: OBS frontend API not enabled, skipping");
#endif
            break;

        case OP_RECORDSTOP:
#ifdef ENABLE_FRONTEND_API
            obs_frontend_recording_stop();
#else
            // Frontend API not enabled - log warning but continue execution
            blog(LOG_WARNING, "[c64script] RECORDSTOP: OBS frontend API not enabled, skipping");
#endif
            break;

        case OP_TYPE: {
            // TYPE text - Type text via keyboard injection
            c64script_value_t text;
            if (!c64script_runtime_pop(runtime, &text))
                return false;
            if (text.type != VALUE_STRING) {
                c64script_value_free(&text);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (TYPE)");
                return false;
            }

            if (!runtime->keyboard) {
                c64script_value_free(&text);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Keyboard not available");
                return false;
            }

            const char *s = text.as.string ? text.as.string : "";
            while (*s) {
                c64_output_t output = {0};
                output.mode = C64_OUTPUT_TEXT;
                size_t chunk = strlen(s);
                if (chunk >= sizeof(output.data.text)) {
                    chunk = sizeof(output.data.text) - 1;
                }
                memcpy(output.data.text, s, chunk);
                output.data.text[chunk] = '\0';
                c64_keyboard_queue_output((c64_keyboard_t *)runtime->keyboard, &output);
                s += chunk;
            }
            c64script_value_free(&text);
            break;
        }

        case OP_KEY: {
            // KEY key_name - Press a key via keyboard injection
            c64script_value_t key;
            if (!c64script_runtime_pop(runtime, &key))
                return false;
            if (!runtime->keyboard) {
                c64script_value_free(&key);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Keyboard not available");
                return false;
            }

            c64_output_t output = {0};
            if (key.type == VALUE_STRING) {
                output.mode = C64_OUTPUT_SYMBOLIC;
                const char *in = key.as.string ? key.as.string : "";
                size_t len = strlen(in);
                if (len >= sizeof(output.data.symbol)) {
                    len = sizeof(output.data.symbol) - 1;
                }
                for (size_t i = 0; i < len; i++) {
                    output.data.symbol[i] = (char)toupper((unsigned char)in[i]);
                }
                output.data.symbol[len] = '\0';
            } else if (key.type == VALUE_NUMBER) {
                output.mode = C64_OUTPUT_PETSCII;
                output.data.petscii = (uint8_t)((int)key.as.number & 0xFF);
            } else {
                c64script_value_free(&key);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (KEY)");
                return false;
            }

            c64_keyboard_queue_output((c64_keyboard_t *)runtime->keyboard, &output);
            c64script_value_free(&key);
            break;
        }

        case OP_POKE_SINGLE: {
            // POKE address value - Write single byte via REST DMA
            c64script_value_t value, address;
            if (!c64script_runtime_pop(runtime, &value) || !c64script_runtime_pop(runtime, &address))
                return false;
            if (!runtime->rest_client) {
                c64script_value_free(&address);
                c64script_value_free(&value);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
                return false;
            }
            if (!require_number(runtime, &address, "POKE") || !require_number(runtime, &value, "POKE")) {
                c64script_value_free(&address);
                c64script_value_free(&value);
                return false;
            }
            if (address.as.number < 0.0 || address.as.number > 65535.0) {
                c64script_value_free(&address);
                c64script_value_free(&value);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                return false;
            }
            uint16_t addr = (uint16_t)(uint32_t)address.as.number;
            uint8_t byte = (uint8_t)((int)value.as.number & 0xFF);
            bool ok = c64_rest_write_memory((c64_rest_client_t *)runtime->rest_client, addr, &byte, 1);
            if (!ok) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "POKE failed: %s",
                         c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
                c64script_value_free(&address);
                c64script_value_free(&value);
                return false;
            }
            c64script_value_free(&address);
            c64script_value_free(&value);
            break;
        }

        case OP_POKE_ARRAY: {
            // POKE address [value1, value2, ...] - Write multiple bytes via REST DMA
            // operand: number of values
            uint32_t count = instr->operand;
            if (count > runtime->stack_size) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "POKE_ARRAY: not enough values on stack");
                return false;
            }

            // Pop values (in reverse order)
            c64script_value_t *values = malloc(count * sizeof(c64script_value_t));
            for (int i = count - 1; i >= 0; i--) {
                if (!c64script_runtime_pop(runtime, &values[i])) {
                    free(values);
                    return false;
                }
            }

            c64script_value_t address;
            if (!c64script_runtime_pop(runtime, &address)) {
                for (uint32_t i = 0; i < count; i++) {
                    c64script_value_free(&values[i]);
                }
                free(values);
                return false;
            }

            if (!runtime->rest_client) {
                c64script_value_free(&address);
                for (uint32_t i = 0; i < count; i++) {
                    c64script_value_free(&values[i]);
                }
                free(values);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
                return false;
            }

            if (!require_number(runtime, &address, "POKE")) {
                c64script_value_free(&address);
                for (uint32_t i = 0; i < count; i++) {
                    c64script_value_free(&values[i]);
                }
                free(values);
                return false;
            }

            if (address.as.number < 0.0 || address.as.number > 65535.0) {
                c64script_value_free(&address);
                for (uint32_t i = 0; i < count; i++) {
                    c64script_value_free(&values[i]);
                }
                free(values);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                return false;
            }
            uint16_t base_addr = (uint16_t)(uint32_t)address.as.number;
            uint8_t buf[128];
            uint32_t offset = 0;

            while (offset < count) {
                uint32_t chunk = count - offset;
                if (chunk > sizeof(buf)) {
                    chunk = (uint32_t)sizeof(buf);
                }
                for (uint32_t i = 0; i < chunk; i++) {
                    if (!require_number(runtime, &values[offset + i], "POKE")) {
                        c64script_value_free(&address);
                        for (uint32_t j = 0; j < count; j++) {
                            c64script_value_free(&values[j]);
                        }
                        free(values);
                        return false;
                    }
                    buf[i] = (uint8_t)((int)values[offset + i].as.number & 0xFF);
                }

                bool ok = c64_rest_write_memory((c64_rest_client_t *)runtime->rest_client,
                                                (uint16_t)(base_addr + offset), buf, chunk);
                if (!ok) {
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "POKE failed: %s",
                             c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
                    c64script_value_free(&address);
                    for (uint32_t j = 0; j < count; j++) {
                        c64script_value_free(&values[j]);
                    }
                    free(values);
                    return false;
                }

                offset += chunk;
            }
            c64script_value_free(&address);
            for (uint32_t i = 0; i < count; i++) {
                c64script_value_free(&values[i]);
            }
            free(values);
            break;
        }

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
                    const char *default_name = runtime->log_filename[0] ? runtime->log_filename : "c64script.log";
                    runtime->log_file = fopen(default_name, "a");
                    if (runtime->log_file) {
                        strncpy(runtime->log_filename, default_name, sizeof(runtime->log_filename) - 1);
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

            // Open new log file
            const char *mode_str = (instr->operand != 0) ? "w" : "a";
            runtime->log_file = fopen(filename.as.string ? filename.as.string : "", mode_str);
            if (!runtime->log_file) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to open log file: %s",
                         filename.as.string ? filename.as.string : "");
                c64script_value_free(&filename);
                return false;
            } else {
                strncpy(runtime->log_filename, filename.as.string ? filename.as.string : "",
                        sizeof(runtime->log_filename) - 1);
            }
            c64script_value_free(&filename);
            break;
        }

        case OP_TRON:
            runtime->trace_enabled = true;
            blog(LOG_INFO, "TRON: Tracing enabled");
            break;

        case OP_TROFF:
            runtime->trace_enabled = false;
            blog(LOG_INFO, "TROFF: Tracing disabled");
            break;

        case OP_READFILE: {
            // READFILE variable, path - Read file content into variable
            c64script_value_t path_val;
            c64script_value_t var_name_val;
            if (!c64script_runtime_pop(runtime, &path_val))
                return false;
            if (!c64script_runtime_pop(runtime, &var_name_val))
                return false;

            if (var_name_val.type != VALUE_STRING) {
                c64script_value_free(&var_name_val);
                c64script_value_free(&path_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (READFILE variable name)");
                return false;
            }

            if (path_val.type != VALUE_STRING) {
                c64script_value_free(&var_name_val);
                c64script_value_free(&path_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (READFILE path)");
                return false;
            }

            char *content = NULL;
            char err[256] = {0};
            if (!load_text_file(path_val.as.string, &content, err, sizeof(err))) {
                c64script_value_free(&var_name_val);
                c64script_value_free(&path_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to read file");
                return false;
            }

            c64script_value_t content_val = {.type = VALUE_STRING, .as.string = content};
            c64script_runtime_set_var(runtime, var_name_val.as.string, content_val);

            c64script_value_free(&content_val);
            c64script_value_free(&var_name_val);
            c64script_value_free(&path_val);
            break;
        }

        case OP_WRITEFILE_APPEND:
        case OP_WRITEFILE_TRUNCATE: {
            // WRITEFILE path, content [TRUNCATE|APPEND]
            bool truncate = (instr->opcode == OP_WRITEFILE_TRUNCATE);
            c64script_value_t content_val;
            c64script_value_t path_val;
            if (!c64script_runtime_pop(runtime, &content_val))
                return false;
            if (!c64script_runtime_pop(runtime, &path_val))
                return false;

            if (path_val.type != VALUE_STRING) {
                c64script_value_free(&path_val);
                c64script_value_free(&content_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (WRITEFILE path)");
                return false;
            }

            if (content_val.type != VALUE_STRING) {
                c64script_value_free(&path_val);
                c64script_value_free(&content_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (WRITEFILE content)");
                return false;
            }

            char err[256] = {0};
            if (!write_file(path_val.as.string, content_val.as.string, truncate, err, sizeof(err))) {
                c64script_value_free(&path_val);
                c64script_value_free(&content_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to write file");
                return false;
            }

            c64script_value_free(&path_val);
            c64script_value_free(&content_val);
            break;
        }

        case OP_HTTP: {
            // HTTP method (from operand), url, headers, body, status_var, response_var
            // Pop in reverse order: response_var, status_var, body, headers, url
            c64script_value_t response_var_val, status_var_val, body_val, headers_val, url_val;

            if (!c64script_runtime_pop(runtime, &response_var_val))
                return false;
            if (!c64script_runtime_pop(runtime, &status_var_val))
                return false;
            if (!c64script_runtime_pop(runtime, &body_val))
                return false;
            if (!c64script_runtime_pop(runtime, &headers_val))
                return false;
            if (!c64script_runtime_pop(runtime, &url_val))
                return false;

            // Validate types
            if (url_val.type != VALUE_STRING) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (HTTP url)");
                c64script_value_free(&url_val);
                c64script_value_free(&headers_val);
                c64script_value_free(&body_val);
                c64script_value_free(&status_var_val);
                c64script_value_free(&response_var_val);
                return false;
            }

            // Perform HTTP request (simplified - no actual curl call, just placeholder)
            // In a real implementation, this would use libcurl like c64-rest-client.c does
            int status_code = 200;          // Placeholder
            const char *response_text = ""; // Placeholder

            // TODO: Implement actual HTTP request with libcurl:
            // 1. Initialize curl handle
            // 2. Set method based on instr->operand (0=GET, 1=POST, 2=PUT, 3=DELETE, 4=PATCH)
            // 3. Set URL
            // 4. Set headers if headers_val is non-empty string
            // 5. Set body if body_val is non-empty string
            // 6. Perform request
            // 7. Capture status code and response body
            // For now, return placeholder values

            // Store status code if status_var is provided
            if (status_var_val.type == VALUE_STRING && status_var_val.as.string[0] != '\0') {
                c64script_value_t status_value;
                status_value.type = VALUE_NUMBER;
                status_value.as.number = (double)status_code;
                c64script_runtime_set_var(runtime, status_var_val.as.string, status_value);
            }

            // Store response if response_var is provided
            if (response_var_val.type == VALUE_STRING && response_var_val.as.string[0] != '\0') {
                c64script_value_t response_value;
                response_value.type = VALUE_STRING;
                response_value.as.string = strdup(response_text);
                c64script_runtime_set_var(runtime, response_var_val.as.string, response_value);
            }

            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            break;
        }

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
            continue; // Skip ip increment
        }

        case OP_RETURN_VALUE:
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RETURN statement not yet implemented");
            return false;

        case OP_ENTER_SCOPE:
        case OP_EXIT_SCOPE:
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Function scopes not yet implemented");
            return false;

        default:
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Unknown opcode: %d", instr->opcode);
            return false;
        }

        // Update last executed line after instruction completes
        if (current_line > 0 && current_line != runtime->last_executed_line) {
            runtime->last_executed_line = current_line;
        }

        // Update next line to execute for the next iteration
        if (runtime->ip < runtime->bytecode_size) {
            runtime->next_line_to_execute = runtime->bytecode[runtime->ip].source_line;
        } else {
            runtime->next_line_to_execute = 0; // Script completed
        }
    }

    // Note: trace finalization is handled by c64script_execute wrapper

    return true;
}

bool c64script_vm_step(c64script_runtime_t *runtime)
{
    if (!runtime) {
        blog(LOG_ERROR, "NULL runtime provided");
        return false;
    }

    // TODO: Implement single-step execution
    blog(LOG_WARNING, "Single-step execution not yet fully implemented");
    return true;
}
