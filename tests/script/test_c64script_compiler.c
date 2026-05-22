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
#include "c64script_test_stubs.h"

#include <assert.h>
// stb_image_write is vendored at src/video/. The implementation lives in
// c64-script-vm-dispatch-io.c (linked into this test binary), so here we only
// pull in the declarations.
#include "stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

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

static void make_temp_test_dir(char *out_path, size_t out_size)
{
    unsigned long t = (unsigned long)time(NULL);
    int pid = (int)getpid();
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp || tmp[0] == '\0') {
        tmp = ".";
    }
    snprintf(out_path, out_size, "%s\\c64script_vm_test_%d_%lu", tmp, pid, t);
    assert(_mkdir(out_path) == 0);
#else
    snprintf(out_path, out_size, "/tmp/c64script_vm_test_%d_%lu", pid, t);
    assert(mkdir(out_path, 0700) == 0);
#endif
}

static void cleanup_temp_path(const char *path)
{
    if (!path || path[0] == '\0') {
        return;
    }
    remove(path);
}

static bool file_exists(const char *path)
{
    if (!path) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    fclose(file);
    return true;
}

static void remove_temp_dir(const char *path)
{
    if (!path || path[0] == '\0') {
        return;
    }
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

static void write_test_png_rgba(const char *path, uint32_t width, uint32_t height, const uint8_t *pixels)
{
    const int stride = (int)width * 4;
    int ok = stbi_write_png(path, (int)width, (int)height, 4, pixels, stride);
    assert(ok && "Failed to write test PNG");
}

static void unset_test_env_or_die(const char *name)
{
#ifdef _WIN32
    assert(_putenv_s(name, "") == 0);
#else
    assert(unsetenv(name) == 0);
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
                         "BUS$ = DRIVE$(DRIVE_A, PROP_BUS_ID)\n"
                         "TYPE$ = DRIVE$(DRIVE_A, PROP_TYPE)\n"
                         "ROM$ = DRIVE$(DRIVE_A, PROP_ROM)\n"
                         "FILE$ = DRIVE$(DRIVE_A, PROP_IMAGE_FILE)\n"
                         "PATH$ = DRIVE$(DRIVE_A, PROP_IMAGE_PATH)\n"
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

    got_var = c64script_runtime_get_var(runtime, "BUS$", &value);
    assert(got_var);
    assert(strcmp(value.as.string, "8") == 0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "TYPE$", &value);
    assert(got_var);
    assert(strcmp(value.as.string, "1541") == 0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "ROM$", &value);
    assert(got_var);
    assert(strcmp(value.as.string, "1541.rom") == 0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "FILE$", &value);
    assert(got_var);
    assert(strcmp(value.as.string, "game.d64") == 0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "PATH$", &value);
    assert(got_var);
    assert(strcmp(value.as.string, "c64u:/Games/game.d64") == 0);
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

TEST(compile_builtin_env_wrong_arg_count_reports_allowed_arities)
{
    const char *source = "X$ = ENV()\n";
    char error[256];

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(!success);
    assert(strstr(error, "ENV expects 1 or 2 arguments") != NULL);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_builtin_env_default_requires_string)
{
    const char *source = "X$ = ENV(\"C64SCRIPT_TEST_ABSENT_VAR_XYZ\", 123)\n";
    char error[256];

    unset_test_env_or_die("C64SCRIPT_TEST_ABSENT_VAR_XYZ");

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(!success);
    assert(strstr(runtime->error_msg, "ENV expects 1 or 2 string arguments") != NULL);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
}

TEST(execute_obs_wait_frames_uses_render_counter_stub)
{
    const char *source = "OBS WAIT FRAMES 3\nSTOP\n";
    char error[256];
    char test_dir[512];
    char script_path[640];

    make_temp_test_dir(test_dir, sizeof(test_dir));
    snprintf(script_path, sizeof(script_path), "%s/script.c64script", test_dir);

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);
    runtime->source_data = (void *)0x1;
    c64script_runtime_set_script_path(runtime, script_path);
    c64script_test_source_stub_reset();

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    success = c64script_execute(runtime);
    assert(success);
    assert(c64script_test_source_wait_call_count() == 1);
    assert(c64script_test_source_last_wait_frame_count() == 3);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
    remove_temp_dir(test_dir);
}

TEST(execute_obs_wait_frames_propagates_stub_failure)
{
    const char *source = "OBS WAIT FRAMES 2\nSTOP\n";
    char error[256];
    char test_dir[512];
    char script_path[640];

    make_temp_test_dir(test_dir, sizeof(test_dir));
    snprintf(script_path, sizeof(script_path), "%s/script.c64script", test_dir);

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);
    runtime->source_data = (void *)0x1;
    c64script_runtime_set_script_path(runtime, script_path);
    c64script_test_source_stub_reset();
    c64script_test_source_wait_fail_next("wait failed in stub");

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    success = c64script_execute(runtime);
    assert(!success);
    assert(strcmp(runtime->error_msg, "wait failed in stub") == 0);
    assert(c64script_test_source_wait_call_count() == 1);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
    remove_temp_dir(test_dir);
}

TEST(execute_obs_screenshot_resolves_relative_path)
{
    const char *source = "OBS SCREENSHOT TARGET PREVIEW PATH \"capture.png\"\nSTOP\n";
    char error[256];
    char test_dir[512];
    char script_path[640];
    char expected_path[640];

    make_temp_test_dir(test_dir, sizeof(test_dir));
    snprintf(script_path, sizeof(script_path), "%s/script.c64script", test_dir);
    snprintf(expected_path, sizeof(expected_path), "%s/capture.png", test_dir);

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);
    runtime->source_data = (void *)0x1;
    c64script_runtime_set_script_path(runtime, script_path);
    c64script_test_source_stub_reset();

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    success = c64script_execute(runtime);
    assert(success);
    assert(c64script_test_source_screenshot_call_count() == 1);
    assert(c64script_test_source_last_screenshot_preview());
    assert(strcmp(c64script_test_source_last_screenshot_path(), expected_path) == 0);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
    remove_temp_dir(test_dir);
}

TEST(execute_obs_screenshot_propagates_stub_failure)
{
    const char *source = "OBS SCREENSHOT TARGET SOURCE PATH \"capture.png\"\nSTOP\n";
    char error[256];
    char test_dir[512];
    char script_path[640];

    make_temp_test_dir(test_dir, sizeof(test_dir));
    snprintf(script_path, sizeof(script_path), "%s/script.c64script", test_dir);

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);
    runtime->source_data = (void *)0x1;
    c64script_runtime_set_script_path(runtime, script_path);
    c64script_test_source_stub_reset();
    c64script_test_source_screenshot_fail_next("screenshot failed in stub");

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    success = c64script_execute(runtime);
    assert(!success);
    assert(strcmp(runtime->error_msg, "screenshot failed in stub") == 0);
    assert(c64script_test_source_screenshot_call_count() == 1);
    assert(!c64script_test_source_last_screenshot_preview());

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
    remove_temp_dir(test_dir);
}

TEST(execute_assert_image_equals_succeeds_for_identical_pngs)
{
    const char *source = "ASSERT IMAGE_EQUALS \"actual.png\", \"expected.png\" TOLERANCE 0\nSTOP\n";
    static const uint8_t pixels[] = {
        0x10, 0x20, 0x30, 0xFF, 0x40, 0x50, 0x60, 0xFF, 0x70, 0x80, 0x90, 0xFF, 0xA0, 0xB0, 0xC0, 0xFF,
    };
    char error[256];
    char test_dir[512];
    char script_path[640];
    char actual_path[640];
    char expected_path[640];
    char diff_path[640];

    make_temp_test_dir(test_dir, sizeof(test_dir));
    snprintf(script_path, sizeof(script_path), "%s/script.c64script", test_dir);
    snprintf(actual_path, sizeof(actual_path), "%s/actual.png", test_dir);
    snprintf(expected_path, sizeof(expected_path), "%s/expected.png", test_dir);
    snprintf(diff_path, sizeof(diff_path), "%s/actual.diff.png", test_dir);

    write_test_png_rgba(actual_path, 2, 2, pixels);
    write_test_png_rgba(expected_path, 2, 2, pixels);

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);
    c64script_runtime_set_script_path(runtime, script_path);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    success = c64script_execute(runtime);
    assert(success);
    assert(!file_exists(diff_path));

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
    cleanup_temp_path(actual_path);
    cleanup_temp_path(expected_path);
    cleanup_temp_path(diff_path);
    remove_temp_dir(test_dir);
}

TEST(execute_assert_image_equals_writes_diff_on_mismatch)
{
    const char *source = "ASSERT IMAGE_EQUALS \"actual.png\", \"expected.png\" TOLERANCE 0\nSTOP\n";
    static const uint8_t actual_pixels[] = {
        0x10, 0x20, 0x30, 0xFF, 0x40, 0x50, 0x60, 0xFF, 0x70, 0x80, 0x90, 0xFF, 0xAA, 0xBB, 0xCC, 0xFF,
    };
    static const uint8_t expected_pixels[] = {
        0x10, 0x20, 0x30, 0xFF, 0x40, 0x50, 0x60, 0xFF, 0x70, 0x80, 0x90, 0xFF, 0x00, 0x00, 0x00, 0xFF,
    };
    char error[256];
    char test_dir[512];
    char script_path[640];
    char actual_path[640];
    char expected_path[640];
    char diff_path[640];

    make_temp_test_dir(test_dir, sizeof(test_dir));
    snprintf(script_path, sizeof(script_path), "%s/script.c64script", test_dir);
    snprintf(actual_path, sizeof(actual_path), "%s/actual.png", test_dir);
    snprintf(expected_path, sizeof(expected_path), "%s/expected.png", test_dir);
    snprintf(diff_path, sizeof(diff_path), "%s/actual.diff.png", test_dir);

    write_test_png_rgba(actual_path, 2, 2, actual_pixels);
    write_test_png_rgba(expected_path, 2, 2, expected_pixels);

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);
    c64script_runtime_set_script_path(runtime, script_path);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    success = c64script_execute(runtime);
    assert(!success);
    assert(strstr(runtime->error_msg, "ASSERT IMAGE_EQUALS failed:") != NULL);
    assert(strstr(runtime->error_msg, "actual.diff.png") != NULL);
    assert(file_exists(diff_path));

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
    cleanup_temp_path(actual_path);
    cleanup_temp_path(expected_path);
    cleanup_temp_path(diff_path);
    remove_temp_dir(test_dir);
}

TEST(execute_assert_image_equals_detects_dimension_mismatch)
{
    const char *source = "ASSERT IMAGE_EQUALS \"actual.png\", \"expected.png\" TOLERANCE 0\nSTOP\n";
    static const uint8_t actual_pixels[] = {
        0x10,
        0x20,
        0x30,
        0xFF,
    };
    static const uint8_t expected_pixels[] = {
        0x10, 0x20, 0x30, 0xFF, 0x40, 0x50, 0x60, 0xFF,
    };
    char error[256];
    char test_dir[512];
    char script_path[640];
    char actual_path[640];
    char expected_path[640];
    char diff_path[640];

    make_temp_test_dir(test_dir, sizeof(test_dir));
    snprintf(script_path, sizeof(script_path), "%s/script.c64script", test_dir);
    snprintf(actual_path, sizeof(actual_path), "%s/actual.png", test_dir);
    snprintf(expected_path, sizeof(expected_path), "%s/expected.png", test_dir);
    snprintf(diff_path, sizeof(diff_path), "%s/actual.diff.png", test_dir);

    write_test_png_rgba(actual_path, 1, 1, actual_pixels);
    write_test_png_rgba(expected_path, 2, 1, expected_pixels);

    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);
    c64script_runtime_set_script_path(runtime, script_path);

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);
    success = c64script_execute(runtime);
    assert(!success);
    assert(strstr(runtime->error_msg, "ASSERT IMAGE_EQUALS failed:") != NULL);
    assert(file_exists(diff_path));

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
    cleanup_temp_path(actual_path);
    cleanup_temp_path(expected_path);
    cleanup_temp_path(diff_path);
    remove_temp_dir(test_dir);
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
    RUN_TEST(compile_builtin_env_wrong_arg_count_reports_allowed_arities);

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
    RUN_TEST(execute_builtin_env_default_requires_string);
    RUN_TEST(execute_obs_wait_frames_uses_render_counter_stub);
    RUN_TEST(execute_obs_wait_frames_propagates_stub_failure);
    RUN_TEST(execute_obs_screenshot_resolves_relative_path);
    RUN_TEST(execute_obs_screenshot_propagates_stub_failure);
    RUN_TEST(execute_assert_image_equals_succeeds_for_identical_pngs);
    RUN_TEST(execute_assert_image_equals_writes_diff_on_mismatch);
    RUN_TEST(execute_assert_image_equals_detects_dimension_mismatch);

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
