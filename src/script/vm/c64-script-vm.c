/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-script.h"
#include "c64-script-runtime.h"
#include "c64-script-vm-internal.h"
#include "c64-script-vm.h"
#include "c64-logging.h"

#include <obs-module.h>
#include <string.h>
#include <util/platform.h>

bool c64script_execute(c64script_runtime_t *runtime)
{
    bool result = c64script_vm_execute(runtime);

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

        if (runtime->max_iterations > 0 && ++runtime->iteration_count >= runtime->max_iterations) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Iteration limit exceeded (%llu iterations)",
                     (unsigned long long)runtime->max_iterations);
            runtime->should_stop = true;
            return false;
        }

        if (runtime->should_pause && current_line != runtime->last_executed_line && current_line > 0) {
            runtime->is_paused = true;
            runtime->should_pause = false;
            if (runtime->step_skip_waits) {
                runtime->step_skip_waits = false;
            }
            while (runtime->is_paused && !runtime->should_stop) {
                os_sleep_ms(10);

                if (runtime->step_mode) {
                    runtime->step_mode = false;
                    runtime->step_skip_waits = true;
                    runtime->is_paused = false;
                    runtime->should_pause = true;
                    break;
                }
            }

            if (runtime->should_stop) {
                break;
            }
        }

        runtime->error_line = instr->source_line;

        if (runtime->trace_recording_enabled && current_line != runtime->last_executed_line && current_line > 0) {
            c64script_vm_record_trace_entry(runtime, current_line);
        }

        if (runtime->trace_enabled) {
            blog(LOG_INFO, "[TRACE] IP=%zu OP=%d line=%d", runtime->ip, instr->opcode, instr->source_line);
        }

        runtime->ip++;

        bool skip_line_update = false;
        if (!c64script_vm_execute_instruction(runtime, instr, &skip_line_update)) {
            return false;
        }

        if (skip_line_update) {
            if (runtime->ip < runtime->bytecode_size) {
                runtime->next_line_to_execute = runtime->bytecode[runtime->ip].source_line;
            } else {
                runtime->next_line_to_execute = 0;
            }
            continue;
        }

        if (current_line > 0 && current_line != runtime->last_executed_line) {
            runtime->last_executed_line = current_line;
        }

        if (runtime->ip < runtime->bytecode_size) {
            runtime->next_line_to_execute = runtime->bytecode[runtime->ip].source_line;
        } else {
            runtime->next_line_to_execute = 0;
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

    if (!runtime->bytecode || runtime->bytecode_size == 0) {
        blog(LOG_ERROR, "No bytecode to execute");
        return false;
    }

    if (runtime->ip >= runtime->bytecode_size || runtime->should_stop) {
        return false;
    }

    c64script_instruction_t *instr = &runtime->bytecode[runtime->ip];
    int current_line = instr->source_line;

    if (runtime->max_iterations > 0 && ++runtime->iteration_count >= runtime->max_iterations) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Iteration limit exceeded (%llu iterations)",
                 (unsigned long long)runtime->max_iterations);
        runtime->should_stop = true;
        return false;
    }

    runtime->error_line = instr->source_line;

    if (runtime->trace_recording_enabled && current_line != runtime->last_executed_line && current_line > 0) {
        c64script_vm_record_trace_entry(runtime, current_line);
    }

    if (runtime->trace_enabled) {
        blog(LOG_INFO, "[TRACE] IP=%zu OP=%d line=%d", runtime->ip, instr->opcode, instr->source_line);
    }

    runtime->ip++;

    bool skip_line_update = false;
    if (!c64script_vm_execute_instruction(runtime, instr, &skip_line_update)) {
        return false;
    }

    if (!skip_line_update && current_line > 0 && current_line != runtime->last_executed_line) {
        runtime->last_executed_line = current_line;
    }

    if (runtime->ip < runtime->bytecode_size) {
        runtime->next_line_to_execute = runtime->bytecode[runtime->ip].source_line;
    } else {
        runtime->next_line_to_execute = 0;
    }

    return true;
}
