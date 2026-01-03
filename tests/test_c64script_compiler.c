/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script.h"
#include "c64-script-runtime.h"
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
// COMPILER TESTS
// ============================================================================

TEST(compile_simple_number)
{
    const char *source = "X = 42\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    (void)success; // Silence unused warning
    assert(runtime->bytecode != NULL);
    assert(runtime->bytecode_size > 0);
    assert(runtime->constant_count > 0);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(compile_arithmetic_expression)
{
    const char *source = "Y = 10 + 20 * 3\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    (void)success;
    assert(runtime->bytecode != NULL);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(compile_if_statement)
{
    const char *source = "IF X > 0 THEN\n"
                         "  Y = 1\n"
                         "ENDIF\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    (void)success;
    assert(runtime->bytecode != NULL);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(compile_goto_label)
{
    const char *source = "GOTO SKIP\n"
                         "X = 1\n"
                         "SKIP:\n"
                         "Y = 2\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    (void)success;
    // Verify jump patching worked
    assert(runtime->bytecode != NULL);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

// ============================================================================
// VM EXECUTION TESTS
// ============================================================================

TEST(execute_simple_assignment)
{
    const char *source = "X = 42\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    // Verify variable was set
    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    (void)got_var;
    assert(got_var);
    assert(value.type == VALUE_NUMBER);
    assert(value.as.number == 42.0);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_arithmetic)
{
    const char *source = "X = 10 + 5\n"
                         "Y = X * 2\n"
                         "Z = Y - 3\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    assert(got_var);
    assert(value.as.number == 15.0);

    got_var = c64script_runtime_get_var(runtime, "Y", &value);
    assert(got_var);
    assert(value.as.number == 30.0);

    got_var = c64script_runtime_get_var(runtime, "Z", &value);
    assert(got_var);
    assert(value.as.number == 27.0);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_relational_operators)
{
    const char *source = "A = 5 > 3\n"
                         "B = 5 < 3\n"
                         "C = 5 = 5\n"
                         "D = 5 <> 3\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "A", &value);
    assert(got_var);
    assert(value.as.number == 1.0); // true

    got_var = c64script_runtime_get_var(runtime, "B", &value);
    assert(got_var);
    assert(value.as.number == 0.0); // false

    got_var = c64script_runtime_get_var(runtime, "C", &value);
    assert(got_var);
    assert(value.as.number == 1.0); // true

    got_var = c64script_runtime_get_var(runtime, "D", &value);
    assert(got_var);
    assert(value.as.number == 1.0); // true

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_if_then)
{
    const char *source = "X = 0\n"
                         "IF 5 > 3 THEN\n"
                         "  X = 1\n"
                         "ENDIF\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    (void)got_var;
    assert(got_var);
    assert(value.as.number == 1.0); // Should be set

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_if_then_else)
{
    const char *source = "IF 5 < 3 THEN\n"
                         "  X = 1\n"
                         "ELSE\n"
                         "  X = 2\n"
                         "ENDIF\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    (void)got_var;
    assert(got_var);
    assert(value.as.number == 2.0); // Should take else branch

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_goto)
{
    const char *source = "X = 1\n"
                         "GOTO SKIP\n"
                         "X = 999\n"
                         "SKIP:\n"
                         "Y = 2\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    assert(got_var);
    assert(value.as.number == 1.0); // Should not be overwritten

    got_var = c64script_runtime_get_var(runtime, "Y", &value);
    assert(got_var);
    assert(value.as.number == 2.0);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_gosub_return)
{
    const char *source = "X = 1\n"
                         "GOSUB SUB\n"
                         "X = 3\n"
                         "STOP\n"
                         "SUB:\n"
                         "Y = 2\n"
                         "RETURN\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    assert(got_var);
    assert(value.as.number == 3.0);

    got_var = c64script_runtime_get_var(runtime, "Y", &value);
    assert(got_var);
    assert(value.as.number == 2.0);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_stop)
{
    const char *source = "X = 1\n"
                         "STOP\n"
                         "X = 999\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    (void)got_var;
    assert(got_var);
    assert(value.as.number == 1.0); // Should not reach X = 999

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_for_loop)
{
    const char *source = "TOTAL = 0\n"
                         "FOR I = 1 TO 5\n"
                         "  TOTAL = TOTAL + I\n"
                         "NEXT\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    // Verify TOTAL = 1 + 2 + 3 + 4 + 5 = 15
    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "TOTAL", &value);
    (void)got_var;
    assert(got_var);
    assert(value.as.number == 15.0);

    // Verify I = 6 (one past the end)
    got_var = c64script_runtime_get_var(runtime, "I", &value);
    (void)got_var;
    assert(got_var);
    assert(value.as.number == 6.0);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_while_loop)
{
    const char *source = "X = 1\n"
                         "WHILE X < 10\n"
                         "  X = X * 2\n"
                         "WEND\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    // X should be 16 (1 -> 2 -> 4 -> 8 -> 16, stops when >= 10)
    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    (void)got_var;
    assert(got_var);
    assert(value.as.number == 16.0);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

// ============================================================================
// MAIN
// ============================================================================

int main(void)
{
    printf("=== C64Script Compiler & VM Tests ===\n\n");

    printf("--- Compiler Tests ---\n");
    RUN_TEST(compile_simple_number);
    RUN_TEST(compile_arithmetic_expression);
    RUN_TEST(compile_if_statement);
    RUN_TEST(compile_goto_label);

    printf("\n--- VM Execution Tests ---\n");
    RUN_TEST(execute_simple_assignment);
    RUN_TEST(execute_arithmetic);
    RUN_TEST(execute_relational_operators);
    RUN_TEST(execute_if_then);
    RUN_TEST(execute_if_then_else);
    RUN_TEST(execute_goto);
    RUN_TEST(execute_gosub_return);
    RUN_TEST(execute_stop);

    printf("\n--- Loop Execution Tests ---\n");
    RUN_TEST(execute_for_loop);
    RUN_TEST(execute_while_loop);

    printf("\n=== All Compiler & VM Tests Passed! ===\n");
    return 0;
}
