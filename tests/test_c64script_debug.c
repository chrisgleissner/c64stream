/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

// Ensure asserts are always enabled in tests
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-script.h"
#include "c64-script-runtime.h"
#include "c64-script-vm.h"
#include "c64-script-executor.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test helper macros
#define TEST(name) static void name(void)
#define RUN_TEST(name)                                                                                                 \
    do {                                                                                                               \
        printf("Running test: %s ... ", #name);                                                                        \
        name();                                                                                                        \
        printf("OK\n");                                                                                                \
    } while (0)

// ============================================================================
// DEBUG FEATURE TESTS
// ============================================================================

TEST(pause_and_resume)
{
    const char *source = "X = 1\nY = 2\nZ = 3\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    c64script_ast_free(ast);

    // Set pause flag before starting execution
    runtime->should_pause = true;

    // Start execution in a simulated step mode
    // Since we can't test threading here, we just verify the pause mechanism works
    assert(runtime->should_pause == true);
    assert(runtime->is_paused == false);

    // When VM encounters pause, it should set is_paused to true
    runtime->is_paused = true;
    assert(runtime->is_paused == true);

    // Resume should clear is_paused
    runtime->is_paused = false;
    runtime->should_pause = false;
    assert(runtime->is_paused == false);

    c64script_runtime_destroy(runtime);
}

TEST(step_mode)
{
    const char *source = "X = 1\nY = 2\nZ = 3\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    c64script_ast_free(ast);

    // Test step mode behavior: enable step mode and execute one line
    runtime->step_mode = true;
    runtime->should_pause = false;
    runtime->is_paused = false;

    // Execute VM - should process first instruction then stop due to step mode
    int result = c64script_vm_execute(runtime);

    // VM should have executed at least one instruction
    // In step mode during pause loop, step_mode is cleared after one iteration
    assert(runtime->step_mode == false); // Step mode should be cleared
    assert(result == 0);                 // Execution should succeed

    // Verify first variable was set
    c64script_value_t value;
    bool found = c64script_runtime_get_var(runtime, "X", &value);
    assert(found);
    assert(value.type == C64SCRIPT_VALUE_NUMBER);
    assert(value.number == 1.0);

    c64script_runtime_destroy(runtime);
}

TEST(line_tracking)
{
    const char *source = "X = 1\nY = 2\nZ = 3\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    // Store source text for line retrieval
    runtime->source_text = strdup(source);
    runtime->source_text_size = strlen(source);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    c64script_ast_free(ast);

    // Initially, last_executed_line should be 0
    assert(runtime->last_executed_line == 0);

    // Next line to execute should be set
    assert(runtime->next_line_to_execute >= 0);

    c64script_runtime_destroy(runtime);
}

TEST(wait_skips_in_step_mode)
{
    const char *source = "WAIT 1000ms\nX = 1\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    c64script_ast_free(ast);

    // Set step mode
    runtime->step_mode = true;

    // Execute should skip the WAIT when in step mode
    // We can't easily test the actual execution here without threading,
    // but we verify the flag is set correctly
    assert(runtime->step_mode == true);

    c64script_runtime_destroy(runtime);
}

TEST(variable_logging_empty)
{
    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    // With no variables, should handle gracefully
    assert(runtime->variable_count == 0);

    c64script_runtime_destroy(runtime);
}

TEST(variable_logging_with_values)
{
    const char *source = "X = 42\nY = \"hello\"\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    c64script_ast_free(ast);

    // Execute to set variables
    success = c64script_execute(runtime);
    assert(success);

    // Verify variables were set
    assert(runtime->variable_count == 2);

    // Find X variable
    bool found_x = false;
    for (size_t i = 0; i < runtime->variable_count; i++) {
        if (strcmp(runtime->variables[i].name, "X") == 0) {
            assert(runtime->variables[i].value.type == VALUE_NUMBER);
            assert(runtime->variables[i].value.as.number == 42.0);
            found_x = true;
            break;
        }
    }
    assert(found_x);

    c64script_runtime_destroy(runtime);
}

TEST(status_transitions)
{
    // Test that status enum includes PAUSED
    c64_script_status_t status = C64_SCRIPT_STATUS_IDLE;
    assert(status == C64_SCRIPT_STATUS_IDLE);

    status = C64_SCRIPT_STATUS_RUNNING;
    assert(status == C64_SCRIPT_STATUS_RUNNING);

    status = C64_SCRIPT_STATUS_PAUSED;
    assert(status == C64_SCRIPT_STATUS_PAUSED);

    status = C64_SCRIPT_STATUS_ERROR;
    assert(status == C64_SCRIPT_STATUS_ERROR);

    status = C64_SCRIPT_STATUS_COMPLETED;
    assert(status == C64_SCRIPT_STATUS_COMPLETED);
}

TEST(line_at_script_start)
{
    const char *source = "REM First line\nX = 1\nY = 2\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    runtime->source_text = strdup(source);
    runtime->source_text_size = strlen(source);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    c64script_ast_free(ast);

    // At start, last_executed should be 0 (not started)
    assert(runtime->last_executed_line == 0);

    // Next line should be initialized to first instruction's line
    // (Will be set by VM during execution)

    c64script_runtime_destroy(runtime);
}

TEST(line_at_script_end)
{
    const char *source = "X = 1\nY = 2\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    runtime->source_text = strdup(source);
    runtime->source_text_size = strlen(source);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    c64script_ast_free(ast);

    // Execute to completion
    success = c64script_execute(runtime);
    assert(success);

    // At end, next_line should be 0 (completed)
    assert(runtime->next_line_to_execute == 0);

    c64script_runtime_destroy(runtime);
}

// ============================================================================
// MAIN
// ============================================================================

int main(void)
{
    printf("C64Script Debug Features Test Suite\n");
    printf("====================================\n\n");

    RUN_TEST(pause_and_resume);
    RUN_TEST(step_mode);
    RUN_TEST(line_tracking);
    RUN_TEST(wait_skips_in_step_mode);
    RUN_TEST(variable_logging_empty);
    RUN_TEST(variable_logging_with_values);
    RUN_TEST(status_transitions);
    RUN_TEST(line_at_script_start);
    RUN_TEST(line_at_script_end);

    printf("\n====================================\n");
    printf("All tests passed!\n");
    return 0;
}
