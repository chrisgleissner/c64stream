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
#else
#include <strings.h>
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

bool c64script_execute(c64script_runtime_t *runtime)
{
    return c64script_vm_execute(runtime);
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

    while (runtime->ip < runtime->bytecode_size && !runtime->should_stop) {
        c64script_instruction_t *instr = &runtime->bytecode[runtime->ip];
        runtime->error_line = instr->source_line;

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

        case OP_CALL:
            // Push return address
            if (runtime->gosub_stack_size >= C64SCRIPT_MAX_GOSUB_DEPTH) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "GOSUB stack overflow");
                return false;
            }
            runtime->gosub_stack[runtime->gosub_stack_size++].return_ip = runtime->ip;
            runtime->ip = instr->operand;
            break;

        case OP_RETURN:
            if (runtime->gosub_stack_size == 0) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RETURN without GOSUB");
                return false;
            }
            runtime->ip = runtime->gosub_stack[--runtime->gosub_stack_size].return_ip;
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
                c64script_value_free(&preset);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "OBS source not available");
                return false;
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
                c64script_value_free(&param);
                c64script_value_free(&value);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "OBS source not available");
                return false;
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
                c64script_value_free(&palette);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "OBS source not available");
                return false;
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
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "OBS frontend API not enabled");
            return false;
#endif
            break;

        case OP_RECORDSTOP:
#ifdef ENABLE_FRONTEND_API
            obs_frontend_recording_stop();
#else
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "OBS frontend API not enabled");
            return false;
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

        default:
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Unknown opcode: %d", instr->opcode);
            return false;
        }
    }

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
