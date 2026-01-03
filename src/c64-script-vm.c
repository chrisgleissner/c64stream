/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script.h"
#include "c64-script-vm.h"
#include "c64-script-builtins.h"
#include "c64-logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MACRO_LOG_PREFIX "[c64script-vm] "

// ============================================================================
// STACK HELPERS
// ============================================================================

static bool push(c64script_runtime_t *runtime, c64script_value_t value)
{
    if (runtime->stack_size >= runtime->stack_capacity) {
        // Grow stack
        size_t new_cap = runtime->stack_capacity == 0 ? 64 : runtime->stack_capacity * 2;
        c64script_value_t *new_stack = realloc(runtime->stack, new_cap * sizeof(c64script_value_t));
        if (!new_stack) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Stack overflow");
            return false;
        }
        runtime->stack = new_stack;
        runtime->stack_capacity = new_cap;
    }
    runtime->stack[runtime->stack_size++] = value;
    return true;
}

static bool pop(c64script_runtime_t *runtime, c64script_value_t *out_value)
{
    if (runtime->stack_size == 0) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Stack underflow");
        return false;
    }
    *out_value = runtime->stack[--runtime->stack_size];
    return true;
}

// ============================================================================
// VARIABLE HELPERS
// ============================================================================

static bool set_variable(c64script_runtime_t *runtime, const char *name, c64script_value_t value)
{
    // Search for existing variable
    for (size_t i = 0; i < runtime->variable_count; i++) {
        if (strcmp(runtime->variables[i].name, name) == 0) {
            // Free old string if needed
            if (runtime->variables[i].value.type == VALUE_STRING && runtime->variables[i].value.as.string) {
                free(runtime->variables[i].value.as.string);
            }
            // Copy new value (strings need duplication)
            if (value.type == VALUE_STRING) {
                runtime->variables[i].value.type = VALUE_STRING;
                runtime->variables[i].value.as.string = strdup(value.as.string);
            } else {
                runtime->variables[i].value = value;
            }
            return true;
        }
    }

    // Add new variable
    if (runtime->variable_count >= runtime->variable_capacity) {
        size_t new_cap = runtime->variable_capacity == 0 ? 64 : runtime->variable_capacity * 2;
        c64script_variable_t *new_vars = realloc(runtime->variables, new_cap * sizeof(c64script_variable_t));
        if (!new_vars) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory for variables");
            return false;
        }
        runtime->variables = new_vars;
        runtime->variable_capacity = new_cap;
    }

    strncpy(runtime->variables[runtime->variable_count].name, name, 63);
    runtime->variables[runtime->variable_count].name[63] = '\0';
    if (value.type == VALUE_STRING) {
        runtime->variables[runtime->variable_count].value.type = VALUE_STRING;
        runtime->variables[runtime->variable_count].value.as.string = strdup(value.as.string);
    } else {
        runtime->variables[runtime->variable_count].value = value;
    }
    runtime->variable_count++;
    return true;
}

static bool get_variable(c64script_runtime_t *runtime, const char *name, c64script_value_t *out_value)
{
    for (size_t i = 0; i < runtime->variable_count; i++) {
        if (strcmp(runtime->variables[i].name, name) == 0) {
            *out_value = runtime->variables[i].value;
            return true;
        }
    }
    // Variable not found - return 0
    out_value->type = VALUE_NUMBER;
    out_value->as.number = 0.0;
    return true;
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
            if (!push(runtime, runtime->constants[instr->operand]))
                return false;
            break;

        case OP_PUSH_VAR: {
            if (instr->operand >= runtime->constant_count) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid constant index for variable name");
                return false;
            }
            const char *varname = runtime->constants[instr->operand].as.string;
            c64script_value_t value;
            if (!get_variable(runtime, varname, &value))
                return false;
            if (!push(runtime, value))
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
            if (!pop(runtime, &value))
                return false;
            if (!set_variable(runtime, varname, value))
                return false;
            break;
        }

        case OP_POP:
            if (!pop(runtime, &a))
                return false;
            break;

        // Arithmetic
        case OP_ADD:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = a.as.number + b.as.number;
            if (!push(runtime, result))
                return false;
            break;

        case OP_SUBTRACT:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = a.as.number - b.as.number;
            if (!push(runtime, result))
                return false;
            break;

        case OP_MULTIPLY:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = a.as.number * b.as.number;
            if (!push(runtime, result))
                return false;
            break;

        case OP_DIVIDE:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            if (b.as.number == 0.0) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Division by zero");
                return false;
            }
            result.type = VALUE_NUMBER;
            result.as.number = a.as.number / b.as.number;
            if (!push(runtime, result))
                return false;
            break;

        case OP_NEGATE:
            if (!pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = -a.as.number;
            if (!push(runtime, result))
                return false;
            break;

        // Relational
        case OP_EQ:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number == b.as.number) ? 1.0 : 0.0;
            if (!push(runtime, result))
                return false;
            break;

        case OP_NE:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number != b.as.number) ? 1.0 : 0.0;
            if (!push(runtime, result))
                return false;
            break;

        case OP_LT:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number < b.as.number) ? 1.0 : 0.0;
            if (!push(runtime, result))
                return false;
            break;

        case OP_LE:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number <= b.as.number) ? 1.0 : 0.0;
            if (!push(runtime, result))
                return false;
            break;

        case OP_GT:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number > b.as.number) ? 1.0 : 0.0;
            if (!push(runtime, result))
                return false;
            break;

        case OP_GE:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = (a.as.number >= b.as.number) ? 1.0 : 0.0;
            if (!push(runtime, result))
                return false;
            break;

        // Boolean (bitwise on truncated integers)
        case OP_NOT:
            if (!pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = (double)(~((int)a.as.number));
            if (!push(runtime, result))
                return false;
            break;

        case OP_AND:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = (double)(((int)a.as.number) & ((int)b.as.number));
            if (!push(runtime, result))
                return false;
            break;

        case OP_XOR:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = (double)(((int)a.as.number) ^ ((int)b.as.number));
            if (!push(runtime, result))
                return false;
            break;

        case OP_OR:
            if (!pop(runtime, &b) || !pop(runtime, &a))
                return false;
            result.type = VALUE_NUMBER;
            result.as.number = (double)(((int)a.as.number) | ((int)b.as.number));
            if (!push(runtime, result))
                return false;
            break;

        // Control flow
        case OP_JUMP:
            runtime->ip = instr->operand;
            break;

        case OP_JUMP_IF_FALSE:
            if (!pop(runtime, &a))
                return false;
            if (a.as.number == 0.0) {
                runtime->ip = instr->operand;
            }
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

        // TODO: Implement loop opcodes (FOR, WHILE)
        case OP_FOR_INIT:
        case OP_FOR_CHECK:
        case OP_FOR_INCR:
        case OP_WHILE_CHECK:
            blog(LOG_WARNING, "Loop opcodes not yet implemented");
            break;

        // TODO: Implement built-in function calls
        case OP_CALL_PEEK:
        case OP_CALL_BUILTIN:
            blog(LOG_WARNING, "Built-in function calls not yet implemented");
            break;

        // TODO: Implement plugin actions
        case OP_PUSH_NUM:
        case OP_EFFECT:
        case OP_EFFECTPARAM:
        case OP_PALETTE:
        case OP_PLAYSID:
        case OP_RUNPRG:
        case OP_MOUNTDISK:
        case OP_AUTOSTART:
        case OP_RESET:
        case OP_REBOOT:
        case OP_RECORDSTART:
        case OP_RECORDSTOP:
        case OP_TYPE:
        case OP_KEY:
        case OP_POKE_SINGLE:
        case OP_POKE_ARRAY:
        case OP_LOG:
        case OP_PRINT:
        case OP_LOGFILE:
        case OP_TRON:
        case OP_TROFF:
            blog(LOG_WARNING, "Opcode %d not yet implemented", instr->opcode);
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
