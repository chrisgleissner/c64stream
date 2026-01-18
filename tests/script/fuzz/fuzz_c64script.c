/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script.h"
#include "c64-script-runtime.h"
#include "c64script_test_stubs.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define C64SCRIPT_FUZZ_MAX_INPUT 1024
#define C64SCRIPT_FUZZ_MAX_ITERATIONS 1000
#define C64SCRIPT_FUZZ_FIXED_TIME 1700000000

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!data || size == 0) {
        return 0;
    }

    if (size > C64SCRIPT_FUZZ_MAX_INPUT) {
        size = C64SCRIPT_FUZZ_MAX_INPUT;
    }

    char *source = malloc(size + 1);
    if (!source) {
        return 0;
    }

    memcpy(source, data, size);
    source[size] = '\0';

    char parse_error[512];
    c64script_parse_options_t options = {.log_errors = false};
    c64script_ast_node_t *ast = c64script_parse_with_options(source, size, parse_error, sizeof(parse_error), &options);
    free(source);

    if (!ast) {
        return 0;
    }

    c64script_runtime_t *runtime = c64script_runtime_create();
    if (!runtime) {
        c64script_ast_free(ast);
        return 0;
    }

    runtime->max_iterations = C64SCRIPT_FUZZ_MAX_ITERATIONS;
    runtime->step_skip_waits = true;
    c64script_set_time_override(runtime, (time_t)C64SCRIPT_FUZZ_FIXED_TIME);
    c64script_runtime_set_script_path(runtime, "fuzz_input.c64script");

    runtime->rest_client = c64script_test_rest_create();
    runtime->keyboard = c64script_test_keyboard_create();

    char compile_error[512];
    bool compiled = c64script_compile(ast, runtime, compile_error, sizeof(compile_error));
    if (compiled) {
        (void)c64script_execute(runtime);
    }

    if (runtime->keyboard) {
        c64script_test_keyboard_destroy((c64_keyboard_t *)runtime->keyboard);
        runtime->keyboard = NULL;
    }
    if (runtime->rest_client) {
        c64script_test_rest_destroy((c64_rest_client_t *)runtime->rest_client);
        runtime->rest_client = NULL;
    }

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);

    return 0;
}
