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

    // TODO: Initialize runtime (variable storage, stacks, etc.)
    runtime->ip = 0;
    runtime->should_stop = false;
    runtime->trace_enabled = false;
    runtime->log_file = NULL;
    runtime->error_line = -1;

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

    // TODO: Free bytecode, constants, variables, stacks

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
