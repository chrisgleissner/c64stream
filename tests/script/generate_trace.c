/*
Simple trace generator for test_cancellation.c64script
*/
#include "c64-script.h"
#include "c64script_test_stubs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    // Read the script
    FILE *f = fopen("tests/script/scripts/test_cancellation.c64script", "r");
    if (!f) {
        fprintf(stderr, "Failed to open script\n");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);

    // Parse
    char error[1024];
    c64script_ast_node_t *ast = c64script_parse(source, size, error, sizeof(error));
    if (!ast) {
        fprintf(stderr, "Parse error: %s\n", error);
        free(source);
        return 1;
    }

    // Compile
    c64script_runtime_t *runtime = c64script_runtime_create();
    runtime->rest_client = c64script_test_rest_create();
    runtime->keyboard = c64script_test_keyboard_create();
    runtime->max_iterations = 100000;

    if (!c64script_compile(ast, runtime, error, sizeof(error))) {
        fprintf(stderr, "Compile error: %s\n", error);
        c64script_ast_free(ast);
        free(source);
        return 1;
    }
    c64script_ast_free(ast);

    // Enable trace recording
    runtime->source_text = source;
    const char *trace_path = "tests/script/scripts/test_cancellation.expected-trace.yaml";
    if (!c64script_enable_trace_recording(runtime, trace_path)) {
        fprintf(stderr, "Failed to enable trace recording\n");
        free(source);
        return 1;
    }

    // Execute
    if (!c64script_execute(runtime)) {
        fprintf(stderr, "Execution error: %s\n", runtime->error_msg);
        c64script_test_keyboard_destroy(runtime->keyboard);
        c64script_test_rest_destroy(runtime->rest_client);
        c64script_runtime_destroy(runtime);
        free(source);
        return 1;
    }

    printf("Trace generated successfully at %s\n", trace_path);

    c64script_test_keyboard_destroy(runtime->keyboard);
    c64script_test_rest_destroy(runtime->rest_client);
    c64script_runtime_destroy(runtime);
    free(source);
    return 0;
}
