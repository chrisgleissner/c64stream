/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-ast.h"
#include "c64-script-bytecode.h"
#include "c64-script-parser.h"
#include "c64-script-runtime.h"
#include "c64-script-token.h"
#include "c64-script-vm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static c64script_runtime_t *compile_script(const char *script, char *error, size_t error_size)
{
    c64script_ast_node_t *ast = c64script_parse(script, strlen(script), error, error_size);
    if (!ast) {
        return NULL;
    }

    c64script_runtime_t *runtime = c64script_runtime_create();
    if (!runtime) {
        c64script_ast_free(ast);
        snprintf(error, error_size, "Failed to allocate runtime");
        return NULL;
    }

    bool ok = c64script_compile(ast, runtime, error, error_size);
    c64script_ast_free(ast);
    if (!ok) {
        c64script_runtime_destroy(runtime);
        return NULL;
    }

    return runtime;
}

int main(void)
{
    const char *script = "EFFECT \"demo\"\n"
                         "EFFECTPARAM \"scan_line_strength\" 0.5\n"
                         "PALETTE \"Default\"\n"
                         "PALETTECOLOR 1, 10, 20, 30\n"
                         "PRINT \"DONE\"\n";

    char error[512] = {0};
    c64script_runtime_t *runtime = compile_script(script, error, sizeof(error));
    if (!runtime) {
        fprintf(stderr, "Failed to compile script: %s\n", error);
        return 1;
    }

    runtime->obs_source = NULL;
    runtime->rest_client = NULL;
    runtime->keyboard = NULL;

    bool ok = c64script_execute(runtime);
    if (!ok) {
        fprintf(stderr, "Script execution failed: %s\n", runtime->error_msg);
    }

    assert(ok);
    c64script_runtime_destroy(runtime);
    return 0;
}
