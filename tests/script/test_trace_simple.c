/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c64-script.h"

int main(void)
{
    // Read test script
    FILE *f = fopen("tests/script/scripts/trace_test.c64script", "r");
    if (!f) {
        fprintf(stderr, "Failed to open trace_test.c64script\n");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = malloc(size + 1);
    size_t bytes_read = fread(source, 1, size, f);
    source[bytes_read] = '\0';
    fclose(f);

    // Parse
    char error[512];
    c64script_ast_node_t *ast = c64script_parse(source, bytes_read, error, sizeof(error));
    if (!ast) {
        fprintf(stderr, "Parse failed: %s\n", error);
        free(source);
        return 1;
    }

    // Compile
    c64script_runtime_t *runtime = c64script_runtime_create();

    if (!c64script_compile(ast, runtime, error, sizeof(error))) {
        fprintf(stderr, "Compile failed: %s\n", error);
        c64script_ast_free(ast);
        c64script_runtime_destroy(runtime);
        free(source);
        return 1;
    }

    c64script_ast_free(ast);

    // Store source for trace recording
    runtime->source_text = source;

    // Enable trace recording
    c64script_enable_trace_recording(runtime, "/tmp/trace_test.yaml");

    // Execute
    if (!c64script_execute(runtime)) {
        fprintf(stderr, "Execution failed: %s\n", runtime->error_msg);
        c64script_runtime_destroy(runtime);
        free(source);
        return 1;
    }

    printf("✅ Execution succeeded\n");
    printf("📄 Trace written to /tmp/trace_test.yaml\n");

    c64script_runtime_destroy(runtime);
    free(source);

    // Display trace
    printf("\n--- Trace Output ---\n");
    f = fopen("/tmp/trace_test.yaml", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            printf("%s", line);
        }
        fclose(f);
    }

    return 0;
}
