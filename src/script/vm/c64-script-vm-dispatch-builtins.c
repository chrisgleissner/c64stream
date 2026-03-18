/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-dispatch-builtins.h"

#include "c64-script-builtins.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool c64script_dispatch_builtins(c64script_runtime_t *runtime, const c64script_instruction_t *instr)
{
    switch (instr->opcode) {
    case OP_CALL_STR: {
        c64script_value_t num_val;
        if (!c64script_runtime_pop(runtime, &num_val))
            return false;
        if (!require_number(runtime, &num_val, "STR")) {
            c64script_value_free(&num_val);
            return false;
        }

        char str_buf[64];
        if (!c64script_builtin_str(num_val.as.number, str_buf, sizeof(str_buf))) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "STR failed");
            c64script_value_free(&num_val);
            return false;
        }

        c64script_value_t result_val = c64script_value_string(str_buf);
        c64script_value_free(&num_val);
        if (!c64script_runtime_push(runtime, result_val)) {
            c64script_value_free(&result_val);
            return false;
        }
        break;
    }

    case OP_CALL_BUILTIN: {
        uint16_t builtin_id = (uint16_t)(instr->operand >> 16);
        uint16_t arg_count = (uint16_t)(instr->operand & 0xffff);
        c64script_value_t args[3] = {{0}};
        c64script_value_t result_val = {.type = VALUE_NUMBER, .as.number = 0.0};
        bool has_result = false;

        if (arg_count > 3) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid builtin arg count");
            return false;
        }

        for (size_t i = arg_count; i > 0; i--) {
            if (!c64script_runtime_pop(runtime, &args[i - 1])) {
                for (size_t j = i; j <= arg_count; j++) {
                    c64script_value_free(&args[j - 1]);
                }
                return false;
            }
        }

        switch (builtin_id) {
        case C64SCRIPT_BUILTIN_LEN: {
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "LEN expects 1 argument");
                goto builtin_fail;
            }
            if (!require_string(runtime, &args[0], "LEN")) {
                goto builtin_fail;
            }
            const char *s = args[0].as.string ? args[0].as.string : "";
            result_val.type = VALUE_NUMBER;
            result_val.as.number = (double)strlen(s);
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_LEFT: {
            if (arg_count != 2) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "LEFT$ expects 2 arguments");
                goto builtin_fail;
            }
            if (!require_string(runtime, &args[0], "LEFT$")) {
                goto builtin_fail;
            }
            int count = 0;
            if (!number_to_int(runtime, &args[1], &count, "LEFT$")) {
                goto builtin_fail;
            }
            if (count < 0) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                goto builtin_fail;
            }
            const char *s = args[0].as.string ? args[0].as.string : "";
            size_t len = strlen(s);
            size_t out_len = (size_t)count;
            if (out_len > len) {
                out_len = len;
            }
            char *out = calloc(out_len + 1, 1);
            if (!out) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                goto builtin_fail;
            }
            memcpy(out, s, out_len);
            result_val.type = VALUE_STRING;
            result_val.as.string = out;
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_RIGHT: {
            if (arg_count != 2) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RIGHT$ expects 2 arguments");
                goto builtin_fail;
            }
            if (!require_string(runtime, &args[0], "RIGHT$")) {
                goto builtin_fail;
            }
            int count = 0;
            if (!number_to_int(runtime, &args[1], &count, "RIGHT$")) {
                goto builtin_fail;
            }
            if (count < 0) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                goto builtin_fail;
            }
            const char *s = args[0].as.string ? args[0].as.string : "";
            size_t len = strlen(s);
            size_t out_len = (size_t)count;
            if (out_len > len) {
                out_len = len;
            }
            const char *start = s + (len - out_len);
            char *out = calloc(out_len + 1, 1);
            if (!out) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                goto builtin_fail;
            }
            memcpy(out, start, out_len);
            result_val.type = VALUE_STRING;
            result_val.as.string = out;
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_MID: {
            if (arg_count != 3) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "MID$ expects 3 arguments");
                goto builtin_fail;
            }
            if (!require_string(runtime, &args[0], "MID$")) {
                goto builtin_fail;
            }
            int start = 0;
            int count = 0;
            if (!number_to_int(runtime, &args[1], &start, "MID$") ||
                !number_to_int(runtime, &args[2], &count, "MID$")) {
                goto builtin_fail;
            }
            if (start < 1 || count < 0) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                goto builtin_fail;
            }
            const char *s = args[0].as.string ? args[0].as.string : "";
            size_t len = strlen(s);
            size_t start_index = (size_t)(start - 1);
            if (start_index >= len || count == 0) {
                result_val.type = VALUE_STRING;
                result_val.as.string = strdup("");
                if (!result_val.as.string) {
                    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                    goto builtin_fail;
                }
                has_result = true;
                break;
            }
            size_t out_len = (size_t)count;
            if (start_index + out_len > len) {
                out_len = len - start_index;
            }
            char *out = calloc(out_len + 1, 1);
            if (!out) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                goto builtin_fail;
            }
            memcpy(out, s + start_index, out_len);
            result_val.type = VALUE_STRING;
            result_val.as.string = out;
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_CHR: {
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "CHR$ expects 1 argument");
                goto builtin_fail;
            }
            int code = 0;
            if (!number_to_int(runtime, &args[0], &code, "CHR$")) {
                goto builtin_fail;
            }
            if (code < 0 || code > 255) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                goto builtin_fail;
            }
            char *out = calloc(2, 1);
            if (!out) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                goto builtin_fail;
            }
            out[0] = (char)(unsigned char)code;
            result_val.type = VALUE_STRING;
            result_val.as.string = out;
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_ASC: {
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ASC expects 1 argument");
                goto builtin_fail;
            }
            if (!require_string(runtime, &args[0], "ASC")) {
                goto builtin_fail;
            }
            const char *s = args[0].as.string ? args[0].as.string : "";
            unsigned char c = s[0] ? (unsigned char)s[0] : 0;
            result_val.type = VALUE_NUMBER;
            result_val.as.number = (double)c;
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_VAL: {
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "VAL expects 1 argument");
                goto builtin_fail;
            }
            if (!require_string(runtime, &args[0], "VAL")) {
                goto builtin_fail;
            }
            const char *s = args[0].as.string ? args[0].as.string : "";
            char *endptr = NULL;
            double val = strtod(s, &endptr);
            if (endptr == s) {
                val = 0.0;
            }
            result_val.type = VALUE_NUMBER;
            result_val.as.number = val;
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_ABS: {
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ABS expects 1 argument");
                goto builtin_fail;
            }
            if (!require_number(runtime, &args[0], "ABS")) {
                goto builtin_fail;
            }
            result_val.type = VALUE_NUMBER;
            result_val.as.number = fabs(args[0].as.number);
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_INT: {
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "INT expects 1 argument");
                goto builtin_fail;
            }
            if (!require_number(runtime, &args[0], "INT")) {
                goto builtin_fail;
            }
            result_val.type = VALUE_NUMBER;
            result_val.as.number = trunc(args[0].as.number);
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_RND: {
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RND expects 1 argument");
                goto builtin_fail;
            }
            if (!require_number(runtime, &args[0], "RND")) {
                goto builtin_fail;
            }
            double max = args[0].as.number;
            if (max < 0.0) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                goto builtin_fail;
            }
            double base = 0.0;
            if (!c64script_builtin_rnd(runtime, &base)) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RND failed");
                goto builtin_fail;
            }
            result_val.type = VALUE_NUMBER;
            result_val.as.number = base * max;
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_SIN:
        case C64SCRIPT_BUILTIN_COS:
        case C64SCRIPT_BUILTIN_TAN: {
            const char *name = builtin_id == C64SCRIPT_BUILTIN_SIN   ? "SIN"
                               : builtin_id == C64SCRIPT_BUILTIN_COS ? "COS"
                                                                     : "TAN";
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s expects 1 argument", name);
                goto builtin_fail;
            }
            if (!require_number(runtime, &args[0], name)) {
                goto builtin_fail;
            }
            double input = args[0].as.number;
            result_val.type = VALUE_NUMBER;
            if (builtin_id == C64SCRIPT_BUILTIN_SIN) {
                result_val.as.number = sin(input);
            } else if (builtin_id == C64SCRIPT_BUILTIN_COS) {
                result_val.as.number = cos(input);
            } else {
                result_val.as.number = tan(input);
            }
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_SQRT: {
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "SQRT expects 1 argument");
                goto builtin_fail;
            }
            if (!require_number(runtime, &args[0], "SQRT")) {
                goto builtin_fail;
            }
            if (args[0].as.number < 0.0) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                goto builtin_fail;
            }
            result_val.type = VALUE_NUMBER;
            result_val.as.number = sqrt(args[0].as.number);
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_LOG: {
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "LOG expects 1 argument");
                goto builtin_fail;
            }
            if (!require_number(runtime, &args[0], "LOG")) {
                goto builtin_fail;
            }
            if (args[0].as.number <= 0.0) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
                goto builtin_fail;
            }
            result_val.type = VALUE_NUMBER;
            result_val.as.number = log(args[0].as.number);
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_EXP: {
            if (arg_count != 1) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "EXP expects 1 argument");
                goto builtin_fail;
            }
            if (!require_number(runtime, &args[0], "EXP")) {
                goto builtin_fail;
            }
            result_val.type = VALUE_NUMBER;
            result_val.as.number = exp(args[0].as.number);
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_TIME: {
            if (arg_count != 0) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TIME$ expects 0 arguments");
                goto builtin_fail;
            }
            char buf[32];
            if (!format_current_time(runtime, buf, sizeof(buf))) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TIME$ failed");
                goto builtin_fail;
            }
            result_val = c64script_value_string(buf);
            has_result = true;
            break;
        }

        case C64SCRIPT_BUILTIN_ENV: {
            if (arg_count != 1 && arg_count != 2) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ENV expects 1 or 2 string arguments");
                goto builtin_fail;
            }
            if (args[0].type != VALUE_STRING || (arg_count == 2 && args[1].type != VALUE_STRING)) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ENV expects 1 or 2 string arguments");
                goto builtin_fail;
            }
            const char *env_name = args[0].as.string;
            const char *env_val = env_name ? getenv(env_name) : NULL;
            if (env_val != NULL) {
                result_val = c64script_value_string(env_val);
            } else if (arg_count == 2) {
                const char *default_val = args[1].as.string ? args[1].as.string : "";
                result_val = c64script_value_string(default_val);
            } else {
                result_val = c64script_value_string("");
            }
            has_result = true;
            break;
        }

        default:
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "UNDEF'D FUNCTION");
            goto builtin_fail;
        }

        for (size_t i = 0; i < arg_count; i++) {
            c64script_value_free(&args[i]);
        }

        if (has_result) {
            if (!c64script_runtime_push(runtime, result_val)) {
                c64script_value_free(&result_val);
                return false;
            }
        }
        break;

    builtin_fail:
        for (size_t i = 0; i < arg_count; i++) {
            c64script_value_free(&args[i]);
        }
        if (has_result) {
            c64script_value_free(&result_val);
        }
        return false;
    }

    default:
        return false;
    }

    return true;
}
