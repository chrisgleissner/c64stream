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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                                                                 \
    do {                                                                                                               \
        printf("Running test: %s ... ", #name);                                                                        \
        fflush(stdout);                                                                                                \
        test_##name();                                                                                                 \
        printf("OK\n");                                                                                                \
    } while (0)

// ============================================================================
// STRING EDGE CASES
// ============================================================================

TEST(parse_empty_string)
{
    const char *source = "X$ = \"\"\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse empty string");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value->type == AST_EXPR_STRING);
    assert(strcmp(ast->as.assignment.value->as.string, "") == 0);
    c64script_ast_free(ast);
}

TEST(parse_very_long_string)
{
    // Create a string with 10000 characters
    char source[12000];
    strcpy(source, "X$ = \"");
    for (int i = 0; i < 10000; i++) {
        source[6 + i] = 'A';
    }
    strcpy(source + 10006, "\"\n");

    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse very long string");
    c64script_ast_free(ast);
}

TEST(parse_string_with_all_escapes)
{
    const char *source = "X$ = \"\\n\\r\\t\\\\\\\"\\x41\"\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse string with all escape sequences");
    c64script_ast_free(ast);
}

TEST(parse_string_with_invalid_escape)
{
    const char *source = "X$ = \"\\z\"\n"; // \z is not a valid escape
    char error_msg[1024];
    c64script_parse_options_t options = {.log_errors = false};
    c64script_ast_node_t *ast =
        c64script_parse_with_options(source, strlen(source), error_msg, sizeof(error_msg), &options);
    // Should still parse (implementation may treat \z as literal z or error)
    // This tests that we handle it gracefully either way
    if (ast) {
        c64script_ast_free(ast);
    }
}

// ============================================================================
// NUMBER EDGE CASES
// ============================================================================

TEST(parse_very_large_number)
{
    const char *source = "X = 999999999999999999999\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse very large number");
    c64script_ast_free(ast);
}

TEST(parse_number_with_leading_zeros)
{
    const char *source = "X = 00042\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse number with leading zeros");
    assert(ast->as.assignment.value->as.number == 42.0);
    c64script_ast_free(ast);
}

TEST(parse_negative_zero)
{
    const char *source = "X = -0\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse negative zero");
    c64script_ast_free(ast);
}

TEST(execute_division_by_zero)
{
    const char *source = "X = 10 / 0\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    // Division by zero should either produce infinity or error gracefully
    // Not crash or produce undefined behavior

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

// ============================================================================
// VARIABLE NAME EDGE CASES
// ============================================================================

TEST(parse_very_long_variable_name)
{
    // Create a variable name with 300 characters
    char source[400];
    source[0] = 'A';
    for (int i = 1; i < 300; i++) {
        source[i] = 'X';
    }
    strcpy(source + 300, " = 42\n");

    char error_msg[1024];
    c64script_parse_options_t options = {.log_errors = false};
    c64script_ast_node_t *ast =
        c64script_parse_with_options(source, strlen(source), error_msg, sizeof(error_msg), &options);
    // Should either parse or error gracefully
    if (ast) {
        c64script_ast_free(ast);
    }
}

TEST(parse_variable_starting_with_underscore)
{
    const char *source = "_VAR = 42\n";
    char error_msg[1024];
    c64script_parse_options_t options = {.log_errors = false};
    c64script_ast_node_t *ast =
        c64script_parse_with_options(source, strlen(source), error_msg, sizeof(error_msg), &options);
    // Variables cannot start with underscore - should error
    assert(ast == NULL && "Variables cannot start with underscore");
    assert(strlen(error_msg) > 0 && "Should have error message");
}

// ============================================================================
// FUNCTION EDGE CASES
// ============================================================================

TEST(parse_function_with_many_params)
{
    const char *source = "FUN TEST(A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P)\n    RETURN A\nENDFUN\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function with many parameters");
    assert(ast->as.function_def.param_count == 16);
    c64script_ast_free(ast);
}

TEST(parse_function_missing_endfun)
{
    const char *source = "FUN TEST()\n    RETURN 1\n";
    char error_msg[1024];
    c64script_parse_options_t options = {.log_errors = false};
    c64script_ast_node_t *ast =
        c64script_parse_with_options(source, strlen(source), error_msg, sizeof(error_msg), &options);
    // Should error - missing ENDFUN
    assert(ast == NULL && "Should fail without ENDFUN");
}

TEST(execute_undefined_function_call)
{
    const char *source = "X = NONEXISTENT()\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    // Should fail at compile time or runtime
    if (success) {
        success = c64script_execute(runtime);
        assert(!success && "Should fail calling undefined function");
    }

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_function_with_wrong_arg_count)
{
    const char *source = "FUN ADD(A, B)\n    RETURN A + B\nENDFUN\nX = ADD(1)\n"; // Wrong arg count
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    if (success) {
        success = c64script_execute(runtime);
        // Should fail gracefully with wrong arg count
    }

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

// ============================================================================
// VM COMPATIBILITY EDGE CASES
// ============================================================================

TEST(vm_op_return_value_is_supported)
{
    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    runtime->constant_count = 1;
    runtime->constants = calloc(runtime->constant_count, sizeof(*runtime->constants));
    assert(runtime->constants != NULL);
    runtime->constants[0] = c64script_value_number(42.0);

    runtime->bytecode_size = 2;
    runtime->bytecode = calloc(runtime->bytecode_size, sizeof(*runtime->bytecode));
    assert(runtime->bytecode != NULL);
    runtime->bytecode[0] = (c64script_instruction_t){.opcode = OP_PUSH_CONST, .operand = 0, .source_line = 1};
    runtime->bytecode[1] = (c64script_instruction_t){.opcode = OP_RETURN_VALUE, .operand = 0, .source_line = 1};

    runtime->scope_stack_capacity = 1;
    runtime->scope_stack_size = 1;
    runtime->scope_stack = calloc(runtime->scope_stack_capacity, sizeof(*runtime->scope_stack));
    assert(runtime->scope_stack != NULL);
    runtime->scope_stack[0] = (c64script_scope_t){
        .local_vars = NULL,
        .local_var_count = 0,
        .local_var_capacity = 0,
        .saved_var_count = runtime->variable_count,
        .return_ip = runtime->bytecode_size,
    };

    bool ok = c64script_execute(runtime);
    assert(ok && "OP_RETURN_VALUE should execute successfully");
    assert(runtime->stack_size == 1);
    assert(runtime->stack[0].type == VALUE_NUMBER);
    assert(runtime->stack[0].as.number == 42.0);

    c64script_runtime_destroy(runtime);
}

TEST(vm_scope_opcodes_are_supported)
{
    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    runtime->bytecode_size = 3;
    runtime->bytecode = calloc(runtime->bytecode_size, sizeof(*runtime->bytecode));
    assert(runtime->bytecode != NULL);
    runtime->bytecode[0] = (c64script_instruction_t){.opcode = OP_ENTER_SCOPE, .operand = 0, .source_line = 1};
    runtime->bytecode[1] = (c64script_instruction_t){.opcode = OP_EXIT_SCOPE, .operand = 0, .source_line = 1};
    runtime->bytecode[2] = (c64script_instruction_t){.opcode = OP_STOP, .operand = 0, .source_line = 1};

    bool ok = c64script_execute(runtime);
    assert(ok && "OP_ENTER_SCOPE/OP_EXIT_SCOPE should execute successfully");

    c64script_runtime_destroy(runtime);
}

// ============================================================================
// CONTROL FLOW EDGE CASES
// ============================================================================

TEST(execute_goto_undefined_label)
{
    const char *source = "GOTO NONEXISTENT\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(!success && "Should fail to compile with undefined label");

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_gosub_without_return)
{
    const char *source = "GOSUB SUB\nSTOP\nSUB:\nX = 1\nSTOP\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    // Should handle missing RETURN gracefully

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_for_loop_with_zero_step)
{
    const char *source = "FOR I = 1 TO 10 STEP 0\nNEXT I\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    if (success) {
        // Should handle STEP 0 gracefully (infinite loop detection or error)
        // We won't actually execute this as it could hang
    }

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_for_loop_backwards_no_step)
{
    const char *source = "FOR I = 10 TO 1\nNEXT I\nX = I\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    // Loop should not execute since start > end with positive step
    c64script_value_t val;
    success = c64script_runtime_get_var(runtime, "X", &val);
    // I should be 10 (never entered loop)

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

// ============================================================================
// EXPRESSION EDGE CASES
// ============================================================================

TEST(parse_deeply_nested_expressions)
{
    const char *source = "X = ((((((((((1 + 2) + 3) + 4) + 5) + 6) + 7) + 8) + 9) + 10) + 11)\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse deeply nested expressions");
    c64script_ast_free(ast);
}

TEST(execute_very_deep_expression)
{
    // Create expression with 100 levels of nesting
    char source[2000];
    strcpy(source, "X = ");
    for (int i = 0; i < 100; i++) {
        strcat(source, "(");
    }
    strcat(source, "1");
    for (int i = 0; i < 100; i++) {
        strcat(source, ")");
    }
    strcat(source, "\n");

    char error[256];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    // Should either parse or hit nesting limit gracefully
    if (ast) {
        c64script_runtime_t *runtime = c64script_runtime_create();
        bool success = c64script_compile(ast, runtime, error, sizeof(error));
        if (success) {
            c64script_execute(runtime);
        }
        c64script_runtime_destroy(runtime);
        c64script_ast_free(ast);
    }
}

// ============================================================================
// WHITESPACE AND COMMENT EDGE CASES
// ============================================================================

TEST(parse_lines_with_only_whitespace)
{
    const char *source = "X = 1\n    \n\t\t\n  \t  \nY = 2\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse with whitespace-only lines");
    c64script_ast_free(ast);
}

TEST(parse_mixed_tabs_and_spaces)
{
    const char *source = "\t  X = 1\n  \tY = 2\n\t\tZ = 3\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse with mixed tabs and spaces");
    c64script_ast_free(ast);
}

TEST(parse_very_long_comment)
{
    char source[12000];
    strcpy(source, "REM ");
    for (int i = 0; i < 10000; i++) {
        source[4 + i] = 'A';
    }
    strcpy(source + 10004, "\nX = 1\n");

    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse very long comment");
    c64script_ast_free(ast);
}

// ============================================================================
// RESOURCE LIMIT TESTS
// ============================================================================

TEST(parse_many_variables)
{
    // Create program with 500 variables
    char source[50000];
    int offset = 0;
    for (int i = 0; i < 500; i++) {
        offset += snprintf(source + offset, sizeof(source) - offset, "VAR%d = %d\n", i, i);
    }

    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse many variables");
    c64script_ast_free(ast);
}

TEST(parse_many_labels)
{
    // Create program with 200 labels
    char source[20000];
    int offset = 0;
    for (int i = 0; i < 200; i++) {
        offset += snprintf(source + offset, sizeof(source) - offset, "LABEL%d:\nX = %d\n", i, i);
    }

    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse many labels");
    c64script_ast_free(ast);
}

// ============================================================================
// TYPE COERCION EDGE CASES
// ============================================================================

TEST(execute_string_to_number_invalid)
{
    const char *source = "X = \"not_a_number\" + 0\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    if (success) {
        success = c64script_execute(runtime);
        // Should handle invalid string-to-number conversion gracefully
    }

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_empty_string_to_number)
{
    const char *source = "X = \"\" + 0\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    // Empty string should convert to 0 or handle gracefully

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

// ============================================================================
// SECURITY EDGE CASES
// ============================================================================

TEST(parse_potential_buffer_overflow_string)
{
    // Test that parser handles potential buffer overflow attempts
    char source[100000];
    strcpy(source, "X$ = \"");
    for (int i = 0; i < 90000; i++) {
        source[6 + i] = 'A';
    }
    strcpy(source + 90006, "\"\n");

    char error_msg[1024];
    c64script_parse_options_t options = {.log_errors = false};
    c64script_ast_node_t *ast =
        c64script_parse_with_options(source, strlen(source), error_msg, sizeof(error_msg), &options);
    // Should either parse or fail gracefully without crashing
    if (ast) {
        c64script_ast_free(ast);
    }
}

TEST(parse_null_byte_in_source)
{
    char source[20];
    strcpy(source, "X = 1");
    source[5] = '\0'; // Null byte
    strcpy(source + 6, "\nY = 2\n");

    char error_msg[1024];
    // Parser should stop at null byte or handle it
    c64script_parse_options_t options = {.log_errors = false};
    c64script_ast_node_t *ast = c64script_parse_with_options(source, 13, error_msg, sizeof(error_msg), &options);
    if (ast) {
        c64script_ast_free(ast);
    }
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main(void)
{
    printf("=== C64Script Edge Case Tests ===\n\n");

    printf("--- String Edge Cases ---\n");
    RUN_TEST(parse_empty_string);
    RUN_TEST(parse_very_long_string);
    RUN_TEST(parse_string_with_all_escapes);
    RUN_TEST(parse_string_with_invalid_escape);

    printf("\n--- Number Edge Cases ---\n");
    RUN_TEST(parse_very_large_number);
    RUN_TEST(parse_number_with_leading_zeros);
    RUN_TEST(parse_negative_zero);
    RUN_TEST(execute_division_by_zero);

    printf("\n--- Variable Name Edge Cases ---\n");
    RUN_TEST(parse_very_long_variable_name);
    RUN_TEST(parse_variable_starting_with_underscore);

    printf("\n--- Function Edge Cases ---\n");
    RUN_TEST(parse_function_with_many_params);
    RUN_TEST(parse_function_missing_endfun);
    RUN_TEST(execute_undefined_function_call);
    RUN_TEST(execute_function_with_wrong_arg_count);

    printf("\n--- VM Compatibility Edge Cases ---\n");
    RUN_TEST(vm_op_return_value_is_supported);
    RUN_TEST(vm_scope_opcodes_are_supported);

    printf("\n--- Control Flow Edge Cases ---\n");
    RUN_TEST(execute_goto_undefined_label);
    RUN_TEST(execute_gosub_without_return);
    RUN_TEST(execute_for_loop_with_zero_step);
    RUN_TEST(execute_for_loop_backwards_no_step);

    printf("\n--- Expression Edge Cases ---\n");
    RUN_TEST(parse_deeply_nested_expressions);
    RUN_TEST(execute_very_deep_expression);

    printf("\n--- Whitespace and Comment Edge Cases ---\n");
    RUN_TEST(parse_lines_with_only_whitespace);
    RUN_TEST(parse_mixed_tabs_and_spaces);
    RUN_TEST(parse_very_long_comment);

    printf("\n--- Resource Limit Tests ---\n");
    RUN_TEST(parse_many_variables);
    RUN_TEST(parse_many_labels);

    printf("\n--- Type Coercion Edge Cases ---\n");
    RUN_TEST(execute_string_to_number_invalid);
    RUN_TEST(execute_empty_string_to_number);

    printf("\n--- Security Edge Cases ---\n");
    RUN_TEST(parse_potential_buffer_overflow_string);
    RUN_TEST(parse_null_byte_in_source);

    printf("\n=== All Edge Case Tests Passed! ===\n");
    return 0;
}
