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
void c64script_test_rest_fail_next(c64_rest_client_t *client, const char *error);
const char *c64script_test_rest_log(const c64_rest_client_t *client);
const char *c64script_test_rest_last_action(const c64_rest_client_t *client);
const char *c64script_test_rest_last_category(const c64_rest_client_t *client);
const char *c64script_test_rest_last_item(const c64_rest_client_t *client);
const char *c64script_test_rest_last_value(const c64_rest_client_t *client);
const char *c64script_test_rest_last_drive(const c64_rest_client_t *client);
const char *c64script_test_rest_last_path(const c64_rest_client_t *client);
const char *c64script_test_rest_last_type(const c64_rest_client_t *client);
const char *c64script_test_rest_last_mode(const c64_rest_client_t *client);

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

TEST(execute_machine_control_rest_calls)
{
    const char *source = "PAUSE\nRESUME\nPOWEROFF\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    runtime->rest_client = c64script_test_rest_create();
    assert(runtime->rest_client != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    const char *log = c64script_test_rest_log(runtime->rest_client);
    assert(strstr(log, "pause\nresume\npoweroff\n") != NULL);

    c64script_test_rest_destroy(runtime->rest_client);
    runtime->rest_client = NULL;
    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_cfg_commands)
{
    const char *source = "DIM CATS$(3)\n"
                         "DIM ITEMS$(3)\n"
                         "DIM OPTS$(3)\n"
                         "COUNT = CFG_ITEM$(CATS$())\n"
                         "COUNT2 = CFG_ITEM$(\"Audio Mixer\", ITEMS$())\n"
                         "COUNT3 = CFG_OPTIONS$(\"Audio Mixer\",\"Vol Sid Socket 1\", OPTS$())\n"
                         "VAL$ = CFG$(\"Audio Mixer\",\"Vol Sid Socket 1\")\n"
                         "CFG \"Audio Mixer\",\"Vol Sid Socket 1\",\"60\"\n"
                         "CFGSAVE\n"
                         "CFGLOAD\n"
                         "CFGRESET\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);
    runtime->rest_client = c64script_test_rest_create();
    assert(runtime->rest_client != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "COUNT", &value);
    assert(got_var);
    assert(value.type == VALUE_NUMBER);
    assert(value.as.number == 3.0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "COUNT2", &value);
    assert(got_var);
    assert(value.as.number == 2.0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "COUNT3", &value);
    assert(got_var);
    assert(value.as.number == 3.0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "VAL$", &value);
    assert(got_var);
    assert(value.type == VALUE_STRING);
    assert(strcmp(value.as.string, "80") == 0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "CATS$", &value);
    assert(got_var);
    assert(value.type == VALUE_ARRAY);
    c64script_value_t elem = {0};
    assert(c64script_array_get(value.as.array, 0, &elem));
    assert(strcmp(elem.as.string, "Audio Mixer") == 0);
    c64script_value_free(&elem);
    assert(c64script_array_get(value.as.array, 1, &elem));
    assert(strcmp(elem.as.string, "U64 Specific Settings") == 0);
    c64script_value_free(&elem);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "ITEMS$", &value);
    assert(got_var);
    assert(c64script_array_get(value.as.array, 0, &elem));
    assert(strcmp(elem.as.string, "Vol Sid Socket 1") == 0);
    c64script_value_free(&elem);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "OPTS$", &value);
    assert(got_var);
    assert(c64script_array_get(value.as.array, 0, &elem));
    assert(strcmp(elem.as.string, "0") == 0);
    c64script_value_free(&elem);
    c64script_value_free(&value);

    const char *log = c64script_test_rest_log(runtime->rest_client);
    assert(strstr(log, "cfg_set") != NULL);

    c64script_test_rest_destroy(runtime->rest_client);
    runtime->rest_client = NULL;
    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_sid_vic_cpu_commands)
{
    const char *source = "SID_MODEL ULTI1, \"8580\"\n"
                         "SID_ENABLE SOCKET1, 1\n"
                         "SID_VOL ULTI1, \"80\"\n"
                         "SID_FILTER_CURVE ULTI1, \"Flat\"\n"
                         "SID_RESONANCE ULTI2, \"High\"\n"
                         "SID_COMBINED ULTI1, \"Enabled\"\n"
                         "SID_DIGIS ULTI2, \"Off\"\n"
                         "VIC_MODE \"PAL\"\n"
                         "CPU_SPEED \" 1\"\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);
    runtime->rest_client = c64script_test_rest_create();
    assert(runtime->rest_client != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    assert(strcmp(c64script_test_rest_last_category(runtime->rest_client), "U64 Specific Settings") == 0);
    assert(strcmp(c64script_test_rest_last_item(runtime->rest_client), "CPU Speed") == 0);
    assert(strcmp(c64script_test_rest_last_value(runtime->rest_client), " 1") == 0);

    c64script_test_rest_destroy(runtime->rest_client);
    runtime->rest_client = NULL;
    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_sid_model_invalid_target_fails)
{
    const char *source = "SID_MODEL SOCKET1, \"6581\"\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    runtime->rest_client = c64script_test_rest_create();

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(!success);
    assert(strstr(runtime->error_msg, "SID_MODEL target is read-only") != NULL);

    c64script_test_rest_destroy(runtime->rest_client);
    runtime->rest_client = NULL;
    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_drive_commands)
{
    const char *source = "X$ = DRIVE$(DRIVE_A, PROP_ENABLED)\n"
                         "DRIVE_ON DRIVE_A\n"
                         "DRIVE_OFF DRIVE_A\n"
                         "DRIVE_RESET DRIVE_A\n"
                         "DRIVE_UNMOUNT DRIVE_A\n"
                         "DRIVE_MOUNT DRIVE_A, \"c64u:/Games/game.d64\", TYPE_D64, MODE_READONLY\n"
                         "DRIVE_ROM DRIVE_B, \"c64u:/ROMs/1571.rom\"\n"
                         "DRIVE_MODE DRIVE_B, MODE_1581\n"
                         "DRIVE_BUS_ID DRIVE_A, 8\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    runtime->rest_client = c64script_test_rest_create();
    assert(runtime->rest_client != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X$", &value);
    assert(got_var);
    assert(strcmp(value.as.string, "true") == 0);
    c64script_value_free(&value);

    const char *log = c64script_test_rest_log(runtime->rest_client);
    assert(strstr(log, "drive_mount_image") != NULL);
    assert(strstr(log, "drive_rom_image") != NULL);
    assert(strstr(log, "drive_mode") != NULL);
    assert(strstr(log, "cfg_set") != NULL);

    c64script_test_rest_destroy(runtime->rest_client);
    runtime->rest_client = NULL;
    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_drive_invalid_property_fails)
{
    const char *source = "X$ = DRIVE$(DRIVE_A, \"PROP_NOPE\")\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    runtime->rest_client = c64script_test_rest_create();
    assert(runtime->rest_client != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(!success);
    assert(strstr(runtime->error_msg, "Invalid drive property") != NULL);

    c64script_test_rest_destroy(runtime->rest_client);
    runtime->rest_client = NULL;
    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_keyboard_buffer_commands)
{
    const char *source = "LOAD \"*\"\n"
                         "LOAD \"GAME\", 9\n"
                         "RUN\n"
                         "RUN \"GAME\"\n"
                         "RUN \"DEMO\", 9\n"
                         "SYS 64738\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    runtime->keyboard = c64script_test_keyboard_create();
    assert(runtime->keyboard != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    const char *log = c64script_test_keyboard_log(runtime->keyboard);
    assert(strstr(log, "TEXT:LOAD\"*\",8,1\\r") != NULL);
    assert(strstr(log, "TEXT:LOAD\"GAME\",9,1\\r") != NULL);
    assert(strstr(log, "TEXT:RUN\\r") != NULL);
    assert(strstr(log, "TEXT:LOAD\"GAME\",8,1\\rRUN\\r") != NULL);
    assert(strstr(log, "TEXT:LOAD\"DEMO\",9,1\\rRUN\\r") != NULL);
    assert(strstr(log, "TEXT:SYS 64738\\r") != NULL);

    c64script_test_keyboard_destroy(runtime->keyboard);
    runtime->keyboard = NULL;
    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_cfgsave_failure_propagates)
{
    const char *source = "CFGSAVE\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    runtime->rest_client = c64script_test_rest_create();
    assert(runtime->rest_client != NULL);
    c64script_test_rest_fail_next(runtime->rest_client, "boom");

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(!success);
    assert(strstr(runtime->error_msg, "CFGSAVE failed") != NULL);

    c64script_test_rest_destroy(runtime->rest_client);
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
// INTEGRATION TESTS FROM SPEC EXAMPLES
// ============================================================================

TEST(integration_example_c_for_loop)
{
    const char *source = "FOR I = 1 TO 5\n"
                         "  X = I * 2\n"
                         "NEXT I\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    // After loop, I should be 6 and X should be 10 (last iteration: 5 * 2)
    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "I", &value);
    assert(got_var);
    assert(value.as.number == 6.0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "X", &value);
    assert(got_var);
    assert(value.as.number == 10.0);
    c64script_value_free(&value);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(integration_example_b_label_if_goto)
{
    const char *source = "START:\n"
                         "I = 0\n"
                         "LOOP:\n"
                         "I = I + 1\n"
                         "IF I < 3 THEN GOTO LOOP\n"
                         "DONE:\n"
                         "X = I\n";
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

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(integration_example_d_gosub_return)
{
    const char *source = "TRACK = 1\n"
                         "GOSUB PLAYTRACK\n"
                         "TRACK = 2\n"
                         "GOSUB PLAYTRACK\n"
                         "STOP\n"
                         "PLAYTRACK:\n"
                         "TOTAL = TOTAL + TRACK\n"
                         "RETURN\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    // TOTAL should be 1 + 2 = 3
    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "TOTAL", &value);
    assert(got_var);
    assert(value.as.number == 3.0);
    c64script_value_free(&value);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(integration_example_e_peek_poke_while)
{
    const char *source = "POKE $C000, 65\n"
                         "X = PEEK($C000)\n"
                         "COUNT = 0\n"
                         "WHILE COUNT < 3\n"
                         "  COUNT = COUNT + 1\n"
                         "WEND\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    runtime->rest_client = c64script_test_rest_create();
    runtime->keyboard = c64script_test_keyboard_create();

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "X", &value);
    assert(got_var);
    assert(value.as.number == 65.0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "COUNT", &value);
    assert(got_var);
    assert(value.as.number == 3.0);
    c64script_value_free(&value);

    c64script_test_keyboard_destroy(runtime->keyboard);
    c64script_test_rest_destroy(runtime->rest_client);
    runtime->keyboard = NULL;
    runtime->rest_client = NULL;

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(integration_example_h_line_numbers)
{
    const char *source = "10 I = 0\n"
                         "20 GOSUB 100\n"
                         "30 I = I + 1\n"
                         "40 IF I < 2 THEN GOTO 20\n"
                         "50 STOP\n"
                         "100 RESULT = RESULT + 1\n"
                         "110 RETURN\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    // RESULT should be called twice (when I=0 and I=1)
    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "RESULT", &value);
    assert(got_var);
    assert(value.as.number == 2.0);
    c64script_value_free(&value);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(integration_nested_loops_and_conditions)
{
    const char *source = "TOTAL = 0\n"
                         "FOR I = 1 TO 3\n"
                         "  FOR J = 1 TO 2\n"
                         "    IF I = J THEN\n"
                         "      TOTAL = TOTAL + 1\n"
                         "    ELSE\n"
                         "      TOTAL = TOTAL + 2\n"
                         "    ENDIF\n"
                         "  NEXT\n"
                         "NEXT\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    // I=1, J=1: match, +1 (total=1)
    // I=1, J=2: no match, +2 (total=3)
    // I=2, J=1: no match, +2 (total=5)
    // I=2, J=2: match, +1 (total=6)
    // I=3, J=1: no match, +2 (total=8)
    // I=3, J=2: no match, +2 (total=10)
    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "TOTAL", &value);
    assert(got_var);
    assert(value.as.number == 10.0);
    c64script_value_free(&value);

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

    printf("\n--- Integration Tests from Spec Examples ---\n");
    RUN_TEST(integration_example_c_for_loop);
    RUN_TEST(integration_example_b_label_if_goto);
    RUN_TEST(integration_example_d_gosub_return);
    RUN_TEST(integration_example_e_peek_poke_while);
    RUN_TEST(integration_example_h_line_numbers);
    RUN_TEST(integration_nested_loops_and_conditions);

    printf("\n=== All Compiler & VM Tests Passed! ===\n");
    return 0;
}
