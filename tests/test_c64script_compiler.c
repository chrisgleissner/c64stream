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
#include <time.h>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

typedef struct c64_rest_client c64_rest_client_t;
typedef struct c64_keyboard c64_keyboard_t;

c64_rest_client_t *c64script_test_rest_create(void);
void c64script_test_rest_destroy(c64_rest_client_t *client);
void c64script_test_rest_set_byte(c64_rest_client_t *client, uint16_t address, uint8_t value);

c64_keyboard_t *c64script_test_keyboard_create(void);
void c64script_test_keyboard_destroy(c64_keyboard_t *keyboard);
const char *c64script_test_keyboard_log(const c64_keyboard_t *keyboard);

static void make_temp_log_path(char *out_path, size_t out_size)
{
    unsigned long t = (unsigned long)time(NULL);
    int pid = (int)getpid();
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp || tmp[0] == '\0') {
        tmp = ".";
    }
    snprintf(out_path, out_size, "%s\\c64script_test_%d_%lu.log", tmp, pid, t);
#else
    snprintf(out_path, out_size, "/tmp/c64script_test_%d_%lu.log", pid, t);
#endif
}

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
    c64script_value_free(&value);

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
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "Y", &value);
    assert(got_var);
    assert(value.as.number == 30.0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "Z", &value);
    assert(got_var);
    assert(value.as.number == 27.0);
    c64script_value_free(&value);

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
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "B", &value);
    assert(got_var);
    assert(value.as.number == 0.0); // false
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "C", &value);
    assert(got_var);
    assert(value.as.number == 1.0); // true
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "D", &value);
    assert(got_var);
    assert(value.as.number == 1.0); // true
    c64script_value_free(&value);

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
    c64script_value_free(&value);

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
    c64script_value_free(&value);

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
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "Y", &value);
    assert(got_var);
    assert(value.as.number == 2.0);
    c64script_value_free(&value);

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
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "Y", &value);
    assert(got_var);
    assert(value.as.number == 2.0);
    c64script_value_free(&value);

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
    c64script_value_free(&value);

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
    c64script_value_free(&value);

    // Verify I = 6 (one past the end)
    got_var = c64script_runtime_get_var(runtime, "I", &value);
    (void)got_var;
    assert(got_var);
    assert(value.as.number == 6.0);
    c64script_value_free(&value);

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
    c64script_value_free(&value);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_line_numbers_and_goto)
{
    const char *source = "10 X = 0\n"
                         "20 GOTO 40\n"
                         "30 X = 1\n"
                         "40 X = 2\n";
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
    assert(value.as.number == 2.0);
    c64script_value_free(&value);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_peek_poke_and_keyboard)
{
    const char *source = "POKE $1000, 65\n"
                         "X = PEEK($1000)\n"
                         "TYPE \"HI\\r\"\n"
                         "KEY \"RETURN\"\n"
                         "KEY 13\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    runtime->rest_client = c64script_test_rest_create();
    runtime->keyboard = c64script_test_keyboard_create();
    assert(runtime->rest_client != NULL);
    assert(runtime->keyboard != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    assert(got_var);
    assert(value.as.number == 65.0);
    c64script_value_free(&value);

    const char *log = c64script_test_keyboard_log(runtime->keyboard);
    assert(strstr(log, "TEXT:HI\\r") != NULL);
    assert(strstr(log, "SYMBOL:RETURN") != NULL);
    assert(strstr(log, "PETSCII:13") != NULL);

    c64script_test_keyboard_destroy(runtime->keyboard);
    c64script_test_rest_destroy(runtime->rest_client);
    runtime->keyboard = NULL;
    runtime->rest_client = NULL;

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_boolean_operator_precedence)
{
    const char *source = "X = 1 AND 2 XOR 3\n"
                         "Y = 1 XOR 2 OR 3\n"
                         "Z = (1 XOR 2) OR 3\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    assert(got_var);
    assert(value.as.number == 3.0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "Y", &value);
    assert(got_var);
    assert(value.as.number == 3.0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "Z", &value);
    assert(got_var);
    assert(value.as.number == 3.0);
    c64script_value_free(&value);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_logfile_and_log)
{
    char log_path[256];
    make_temp_log_path(log_path, sizeof(log_path));
    remove(log_path);

    char source[1024];
    snprintf(source, sizeof(source), "LOGFILE \"%s\" TRUNCATE\nLOG \"HELLO\"\n", log_path);

    char error[256];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    FILE *f = fopen(log_path, "rb");
    assert(f != NULL);
    char buf[64] = {0};
    size_t nread = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    assert(nread > 0);
    assert(strstr(buf, "HELLO\n") != NULL);
    remove(log_path);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(compile_duplicate_label_fails)
{
    const char *source = "START:\nX = 1\nSTART:\nX = 2\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(!success);
    assert(strstr(error, "Duplicate label") != NULL);

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

    printf("\n--- Labels & I/O Tests ---\n");
    RUN_TEST(execute_line_numbers_and_goto);
    RUN_TEST(execute_peek_poke_and_keyboard);
    RUN_TEST(execute_boolean_operator_precedence);
    RUN_TEST(execute_logfile_and_log);
    RUN_TEST(compile_duplicate_label_fails);

    printf("\n=== All Compiler & VM Tests Passed! ===\n");
    return 0;
}
