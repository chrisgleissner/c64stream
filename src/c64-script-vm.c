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

        // Loop opcodes
        case OP_FOR_INIT: {
            // Stack has: step, end, current
            // operand: variable name constant pool index
            c64script_value_t step, end, current;
            if (!pop(runtime, &step) || !pop(runtime, &end) || !pop(runtime, &current))
                return false;

            if (runtime->for_stack_size >= C64SCRIPT_MAX_FOR_NESTING) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "FOR loop nesting too deep");
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
            if (!get_variable(runtime, state->variable, &current)) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Loop variable not found");
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
            if (!get_variable(runtime, state->variable, &current)) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Loop variable not found");
                return false;
            }

            // Increment by step
            current.as.number += state->step_value;

            // Store back
            if (!set_variable(runtime, state->variable, current)) {
                return false;
            }
            break;
        }

        case OP_WHILE_CHECK: {
            // Check WHILE condition
            // operand: jump address if condition is false
            c64script_value_t condition;
            if (!pop(runtime, &condition))
                return false;

            // If condition is false (0), jump to end
            if (condition.as.number == 0.0) {
                runtime->ip = instr->operand;
            }
            break;
        }

        // Built-in function calls (PEEK)
        case OP_CALL_PEEK: {
            // Pop address from stack
            c64script_value_t addr_val;
            if (!pop(runtime, &addr_val))
                return false;

            uint16_t address = (uint16_t)addr_val.as.number;

            // Call PEEK built-in (currently returns 0 as placeholder)
            double result;
            if (!c64script_builtin_peek(runtime, address, &result)) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "PEEK failed");
                return false;
            }

            // Push result onto stack
            c64script_value_t result_val = {.type = VALUE_NUMBER, .as.number = result};
            if (!push(runtime, result_val))
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
            if (!push(runtime, result))
                return false;
            break;

        case OP_EFFECT: {
            // EFFECT preset_name - Apply effect preset
            c64script_value_t preset;
            if (!pop(runtime, &preset))
                return false;
            blog(LOG_INFO, "EFFECT: %s (stub - REST API integration pending)", preset.as.string);
            break;
        }

        case OP_EFFECTPARAM: {
            // EFFECTPARAM param_name value - Set effect parameter
            c64script_value_t value, param;
            if (!pop(runtime, &value) || !pop(runtime, &param))
                return false;
            blog(LOG_INFO, "EFFECTPARAM: %s = %.2f (stub - REST API integration pending)", param.as.string,
                 value.as.number);
            break;
        }

        case OP_PALETTE: {
            // PALETTE palette_name - Load palette
            c64script_value_t palette;
            if (!pop(runtime, &palette))
                return false;
            blog(LOG_INFO, "PALETTE: %s (stub - REST API integration pending)", palette.as.string);
            break;
        }

        case OP_PLAYSID: {
            // PLAYSID sid_file [SONGNR song_number]
            c64script_value_t song_nr, sid_file;
            if (!pop(runtime, &song_nr) || !pop(runtime, &sid_file))
                return false;
            blog(LOG_INFO, "PLAYSID: %s SONGNR %.0f (stub - REST API integration pending)", sid_file.as.string,
                 song_nr.as.number);
            break;
        }

        case OP_RUNPRG: {
            // RUNPRG prg_file - Run a PRG file
            c64script_value_t prg_file;
            if (!pop(runtime, &prg_file))
                return false;
            blog(LOG_INFO, "RUNPRG: %s (stub - REST API integration pending)", prg_file.as.string);
            break;
        }

        case OP_MOUNTDISK: {
            // MOUNTDISK disk_file - Mount a disk image
            c64script_value_t disk_file;
            if (!pop(runtime, &disk_file))
                return false;
            blog(LOG_INFO, "MOUNTDISK: %s (stub - REST API integration pending)", disk_file.as.string);
            break;
        }

        case OP_AUTOSTART:
            blog(LOG_INFO, "AUTOSTART (stub - REST API integration pending)");
            break;

        case OP_RESET:
            blog(LOG_INFO, "RESET (stub - REST API integration pending)");
            break;

        case OP_REBOOT:
            blog(LOG_INFO, "REBOOT (stub - REST API integration pending)");
            break;

        case OP_RECORDSTART:
            blog(LOG_INFO, "RECORDSTART (stub - REST API integration pending)");
            break;

        case OP_RECORDSTOP:
            blog(LOG_INFO, "RECORDSTOP (stub - REST API integration pending)");
            break;

        case OP_TYPE: {
            // TYPE text - Type text via keyboard injection
            c64script_value_t text;
            if (!pop(runtime, &text))
                return false;
            blog(LOG_INFO, "TYPE: \"%s\" (stub - keyboard injection pending)", text.as.string);
            break;
        }

        case OP_KEY: {
            // KEY key_name - Press a key via keyboard injection
            c64script_value_t key;
            if (!pop(runtime, &key))
                return false;
            blog(LOG_INFO, "KEY: %s (stub - keyboard injection pending)", key.as.string);
            break;
        }

        case OP_POKE_SINGLE: {
            // POKE address value - Write single byte via REST DMA
            c64script_value_t value, address;
            if (!pop(runtime, &value) || !pop(runtime, &address))
                return false;
            blog(LOG_INFO, "POKE: $%04X, $%02X (stub - REST API integration pending)", (uint16_t)address.as.number,
                 (uint8_t)value.as.number);
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
                if (!pop(runtime, &values[i])) {
                    free(values);
                    return false;
                }
            }

            c64script_value_t address;
            if (!pop(runtime, &address)) {
                free(values);
                return false;
            }

            blog(LOG_INFO, "POKE: $%04X, [%d values] (stub - REST API integration pending)",
                 (uint16_t)address.as.number, count);
            free(values);
            break;
        }

        case OP_LOG:
        case OP_PRINT: {
            // LOG/PRINT message - Print to log or console
            c64script_value_t message;
            if (!pop(runtime, &message))
                return false;

            if (message.type == VALUE_STRING) {
                blog(LOG_INFO, "[C64Script] %s", message.as.string);
            } else {
                blog(LOG_INFO, "[C64Script] %.2f", message.as.number);
            }
            break;
        }

        case OP_LOGFILE: {
            // LOGFILE filename mode - Open log file
            c64script_value_t mode, filename;
            if (!pop(runtime, &mode) || !pop(runtime, &filename))
                return false;

            // Close existing log file if open
            if (runtime->log_file) {
                fclose(runtime->log_file);
                runtime->log_file = NULL;
            }

            // Open new log file
            const char *mode_str = (mode.as.number != 0.0) ? "w" : "a"; // truncate vs append
            runtime->log_file = fopen(filename.as.string, mode_str);
            if (!runtime->log_file) {
                blog(LOG_WARNING, "Failed to open log file: %s", filename.as.string);
            } else {
                strncpy(runtime->log_filename, filename.as.string, sizeof(runtime->log_filename) - 1);
                blog(LOG_INFO, "LOGFILE: %s (mode: %s)", filename.as.string, mode_str);
            }
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
