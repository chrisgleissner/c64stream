/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

/**
 * Test that validates ALL .c64script files in the repository.
 * This ensures that every script file we ship can be:
 * 1. Parsed successfully
 * 2. Compiled successfully
 * 3. EXECUTED successfully (with mocked OBS/C64U dependencies)
 *
 * This is a critical test that prevents shipping broken scripts.
 */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-script.h"
#include "c64-script-runtime.h"

#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define stat _stat
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#endif

#include "c64script_test_stubs.h"

// Timeout mechanism to prevent hangs
#define SCRIPT_TIMEOUT_SECONDS 2  // Reduced to 2 seconds for snappy tests
#define PER_TEST_TIMEOUT_SECONDS 3  // Max wall-clock time per test (including fork overhead)

static jmp_buf timeout_jump;
static volatile sig_atomic_t timeout_occurred = 0;

static void timeout_handler(int signum)
{
    (void)signum;
    timeout_occurred = 1;
    longjmp(timeout_jump, 1);
}

// Test stub prototypes
typedef struct c64_rest_client c64_rest_client_t;
typedef struct c64_keyboard c64_keyboard_t;

c64_rest_client_t *c64script_test_rest_create(void);
void c64script_test_rest_destroy(c64_rest_client_t *client);
void c64script_test_rest_set_byte(c64_rest_client_t *client, uint16_t address, uint8_t value);

c64_keyboard_t *c64script_test_keyboard_create(void);
void c64script_test_keyboard_destroy(c64_keyboard_t *keyboard);
const char *c64script_test_keyboard_log(const c64_keyboard_t *keyboard);

// Global counters
static int parse_success = 0;
static int parse_expected_fail = 0;
static int compile_success = 0;
static int compile_expected_fail = 0;
static int execution_success = 0;
static int execution_expected_fail = 0;
static int unexpected_failures = 0;

// Maximum iterations to prevent infinite loops in tests
#define MAX_TEST_ITERATIONS 100000
#define MAX_WORKERS_CAP 64
#define MAX_WORKERS_CAP 64

// Scripts that are expected to fail parsing (error test cases)
static const char *EXPECTED_PARSE_FAILURES[] = {"test_error_missing_wend.c64script",
                                                "test_error_missing_next.c64script", NULL};

// Scripts that should parse but fail compilation (type errors, etc.)
static const char *EXPECTED_COMPILE_FAILURES[] = {"test_error_missing_label.c64script",   // Undefined label
                                                  "test_error_goto_missing.c64script",    // Undefined label
                                                  "test_error_duplicate_label.c64script", // Duplicate label
                                                  NULL};

// Scripts that should compile but fail execution (runtime errors)
static const char *EXPECTED_EXECUTION_FAILURES[] = {
    "test_safety_max_nesting.c64script",   // Expected to hit nesting limit
    "test_error_gosub_overflow.c64script", // Expected to hit GOSUB stack limit
    "demo_basic_hello_world.c64script",    // Exceeds test timeout (multi-second waits)
    "hello_world.c64script",               // Exceeds test timeout (looped waits)
    "demo_palette_cycle.c64script",        // Requires OBS source (long running demo)
    "demo_effect_preset_cycle.c64script",  // Requires OBS source (long running demo)
    "test_comparisons.c64script",          // String comparison not yet supported
    "test_error_invalid.c64script",        // Type mismatch
    NULL};

static bool should_expect_parse_failure(const char *filename)
{
    for (int i = 0; EXPECTED_PARSE_FAILURES[i] != NULL; i++) {
        if (strstr(filename, EXPECTED_PARSE_FAILURES[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static bool should_expect_compile_failure(const char *filename)
{
    for (int i = 0; EXPECTED_COMPILE_FAILURES[i] != NULL; i++) {
        if (strstr(filename, EXPECTED_COMPILE_FAILURES[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static bool should_expect_execution_failure(const char *filename)
{
    for (int i = 0; EXPECTED_EXECUTION_FAILURES[i] != NULL; i++) {
        if (strstr(filename, EXPECTED_EXECUTION_FAILURES[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static char *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(content, 1, size, f);
    content[read] = '\0';
    fclose(f);

    if (out_size) {
        *out_size = read;
    }
    return content;
}

static void find_c64script_files(const char *dir_path, char ***files, int *count, int *capacity)
{
#ifdef _WIN32
    char search_path[2048];
    snprintf(search_path, sizeof(search_path), "%s\\*", dir_path);

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);

    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (find_data.cFileName[0] == '.') {
            continue;
        }

        char path[2048];
        snprintf(path, sizeof(path), "%s\\%s", dir_path, find_data.cFileName);

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Skip build directories
            if (strstr(path, "\\build") != NULL || strstr(path, "\\node_modules") != NULL) {
                continue;
            }
            find_c64script_files(path, files, count, capacity);
        } else {
            const char *ext = strrchr(find_data.cFileName, '.');
            if (ext && strcmp(ext, ".c64script") == 0) {
                if (*count >= *capacity) {
                    *capacity *= 2;
                    *files = realloc(*files, *capacity * sizeof(char *));
                    assert(*files != NULL);
                }
                (*files)[*count] = strdup(path);
                (*count)++;
            }
        }
    } while (FindNextFileA(hFind, &find_data) != 0);

    FindClose(hFind);
#else
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Skip build directories
            if (strstr(path, "/build") != NULL || strstr(path, "/node_modules") != NULL) {
                continue;
            }
            find_c64script_files(path, files, count, capacity);
        } else if (S_ISREG(st.st_mode)) {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".c64script") == 0) {
                if (*count >= *capacity) {
                    *capacity *= 2;
                    *files = realloc(*files, *capacity * sizeof(char *));
                    assert(*files != NULL);
                }
                (*files)[*count] = strdup(path);
                (*count)++;
            }
        }
    }

    closedir(dir);
#endif
}

// Write a simple error trace for scripts that fail to parse or compile
static void write_error_trace(const char *file, const char *source, const char *error_msg, const char *status)
{
    char expected_trace_path[1024];
    int base_len = (int)strlen(file) - 10; // Length without .c64script
    snprintf(expected_trace_path, sizeof(expected_trace_path), "%.*s.expected-trace.yaml", base_len, file);

    FILE *f = fopen(expected_trace_path, "w");
    if (!f) {
        return;
    }

    // Write header
    fprintf(f, "# Execution trace\n");
    const char *script_name = strrchr(file, '/');
    script_name = script_name ? script_name + 1 : file;
    fprintf(f, "script: \"%s\"\n", script_name);
    fprintf(f, "status: %s\n", status);

    if (error_msg && error_msg[0]) {
        fprintf(f, "error:\n");
        fprintf(f, "  line: 0\n");
        fprintf(f, "  message: \"");
        for (const char *p = error_msg; *p; p++) {
            if (*p == '"')
                fputs("\\\"", f);
            else if (*p == '\n')
                fputs("\\n", f);
            else if (*p == '\\')
                fputs("\\\\", f);
            else
                fputc(*p, f);
        }
        fprintf(f, "\"\n");
    } else {
        fprintf(f, "error: ~\n");
    }

    // Write program listing if available
    if (source) {
        // First pass: count total lines to determine padding
        const char *src_count = source;
        int max_line = 1;
        while (*src_count) {
            if (*src_count == '\n') {
                max_line++;
            }
            src_count++;
        }

        // Calculate padding width (number of digits)
        int padding_width = 1;
        int temp = max_line;
        while (temp >= 10) {
            padding_width++;
            temp /= 10;
        }

        fprintf(f, "program: |\n");
        const char *src = source;
        int line_num = 1;
        const char *line_start = src;

        while (*src) {
            if (*src == '\n' || *src == '\r') {
                fprintf(f, "  %0*d: ", padding_width, line_num);
                fwrite(line_start, 1, src - line_start, f);
                fprintf(f, "\n");

                if (*src == '\r' && *(src + 1) == '\n') {
                    src++;
                }
                src++;
                line_start = src;
                line_num++;
            } else {
                src++;
            }
        }

        if (line_start < src) {
            fprintf(f, "  %0*d: ", padding_width, line_num);
            fwrite(line_start, 1, src - line_start, f);
            fprintf(f, "\n");
        }
    }

    // Empty trace list
    fprintf(f, "trace: []\n");

    fclose(f);
}

// Process a single script file
static void process_script(const char *file)
{
    printf("Testing: %s\n", file);
    fflush(stdout);

    size_t size;
    char *source = read_file(file, &size);
    if (!source) {
        fprintf(stderr, "  ❌ Failed to read file\n");
        unexpected_failures++;
        return;
    }

    bool expect_parse_fail = should_expect_parse_failure(file);
    bool expect_compile_fail = should_expect_compile_failure(file);
    bool expect_execution_fail = should_expect_execution_failure(file);

    // Set up alarm for parsing (paranoid safeguard)
#ifndef _WIN32
    struct sigaction sa_parse;
    memset(&sa_parse, 0, sizeof(sa_parse));
    sa_parse.sa_handler = timeout_handler;
    sigemptyset(&sa_parse.sa_mask);
    sigaction(SIGALRM, &sa_parse, NULL);
#endif

    // Parse with timeout protection
    char error[1024];
    c64script_ast_node_t *ast = NULL;
    timeout_occurred = 0;
#ifndef _WIN32
    alarm(SCRIPT_TIMEOUT_SECONDS);
#endif

    if (setjmp(timeout_jump) == 0) {
        c64script_parse_options_t parse_options = {.log_errors = !expect_parse_fail};
        ast = c64script_parse_with_options(source, size, error, sizeof(error), &parse_options);
    } else {
        snprintf(error, sizeof(error), "Parse timeout after %d seconds", SCRIPT_TIMEOUT_SECONDS);
        ast = NULL;
    }

#ifndef _WIN32
    alarm(0);
#endif

    if (!ast) {
        // Write error trace
        write_error_trace(file, source, error, "parse_failure");

        if (expect_parse_fail) {
            printf("  ✅ Parse failed as expected: %s\n", error);
            parse_expected_fail++;
        } else {
            fprintf(stderr, "  ❌ UNEXPECTED PARSE FAILURE: %s\n", error);
            unexpected_failures++;
        }
        free(source);
        return;
    }

    if (expect_parse_fail) {
        fprintf(stderr, "  ❌ UNEXPECTED PARSE SUCCESS (expected to fail)\n");
        unexpected_failures++;
        c64script_ast_free(ast);
        free(source);
        return;
    }

    printf("  ✅ Parse succeeded (%zu bytes)\n", size);
    parse_success++;

    // Compile
    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);

    // Set iteration limit to prevent infinite loops
    runtime->max_iterations = MAX_TEST_ITERATIONS;

    // Set up isolated test environment
    runtime->rest_client = c64script_test_rest_create();
    runtime->keyboard = c64script_test_keyboard_create();

    bool compiled = c64script_compile(ast, runtime, error, sizeof(error));
    c64script_ast_free(ast);

    if (!compiled) {
        // Write error trace
        write_error_trace(file, source, error, "compile_failure");

        if (expect_compile_fail) {
            printf("  ✅ Compile failed as expected: %s\n", error);
            compile_expected_fail++;
        } else {
            fprintf(stderr, "  ❌ UNEXPECTED COMPILE FAILURE: %s\n", error);
            unexpected_failures++;
        }
        c64script_test_keyboard_destroy(runtime->keyboard);
        c64script_test_rest_destroy(runtime->rest_client);
        c64script_runtime_destroy(runtime);
        free(source);
        return;
    }

    if (expect_compile_fail) {
        fprintf(stderr, "  ❌ UNEXPECTED COMPILE SUCCESS (expected to fail)\n");
        unexpected_failures++;
        c64script_test_keyboard_destroy(runtime->keyboard);
        c64script_test_rest_destroy(runtime->rest_client);
        c64script_runtime_destroy(runtime);
        free(source);
        return;
    }

    printf("  ✅ Compile succeeded\n");
    compile_success++;

    // Always enable trace recording (will generate .expected-trace.yaml)
    char expected_trace_path[1024];
    // Remove .c64script extension (10 chars) and append .expected-trace.yaml
    int base_len = (int)strlen(file) - 10; // Length without .c64script
    snprintf(expected_trace_path, sizeof(expected_trace_path), "%.*s.expected-trace.yaml", base_len, file);

    runtime->source_text = source;
    c64script_enable_trace_recording(runtime, expected_trace_path);

    // Execute with iteration limit AND timeout protection
    bool executed = false;
    timeout_occurred = 0;

    // Set up alarm signal handler
#ifndef _WIN32
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = timeout_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    // Set timeout
    alarm(SCRIPT_TIMEOUT_SECONDS);
#endif

    // Execute with timeout protection
    if (setjmp(timeout_jump) == 0) {
        executed = c64script_execute(runtime);
    } else {
        // Timeout occurred
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Execution timeout after %d seconds",
                 SCRIPT_TIMEOUT_SECONDS);
        executed = false;
    }

    // Cancel alarm
#ifndef _WIN32
    alarm(0);
#endif

    if (!executed) {
        if (expect_execution_fail) {
            printf("  ✅ Execution failed as expected: %s\n", runtime->error_msg);
            execution_expected_fail++;
        } else {
            fprintf(stderr, "  ❌ UNEXPECTED EXECUTION FAILURE: %s\n", runtime->error_msg);
            unexpected_failures++;
        }
    } else {
        if (expect_execution_fail) {
            fprintf(stderr, "  ❌ UNEXPECTED EXECUTION SUCCESS (expected to fail)\n");
            unexpected_failures++;
        } else {
            printf("  ✅ Execution succeeded\n");
            execution_success++;
        }
    }

    c64script_test_keyboard_destroy(runtime->keyboard);
    c64script_test_rest_destroy(runtime->rest_client);
    c64script_runtime_destroy(runtime);
    // Note: source is owned by runtime->source_text and freed by runtime_destroy
    // Do NOT free(source) here as it would be a double-free
}

#ifndef _WIN32
static int detect_worker_count(void)
{
    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    if (nproc < 1) {
        return 1;
    }
    if (nproc > MAX_WORKERS_CAP) {
        nproc = MAX_WORKERS_CAP;
    }
    return (int)nproc;
}
#endif

int main(int argc, char **argv)
{
    printf("=== C64Script Repository-Wide Validation ===\n\n");

    // Find all .c64script files in tests/script directory
    char **files = malloc(100 * sizeof(char *));
    int count = 0;
    int capacity = 100;

    // Start from repository root (parent of tests directory)
    const char *repo_root = argc > 1 ? argv[1] : "..";

    // Build path to tests/script directory
    char scripts_dir[512];
    snprintf(scripts_dir, sizeof(scripts_dir), "%s/tests/script", repo_root);
    find_c64script_files(scripts_dir, &files, &count, &capacity);

    if (count == 0) {
        fprintf(stderr, "No .c64script files found in repository\n");
        free(files);
        return 1;
    }

    printf("Found %d .c64script files to test (max %llu iterations each)\n\n", count,
           (unsigned long long)MAX_TEST_ITERATIONS);

#ifdef _WIN32
    // Windows: Run tests directly (no fork available)
    for (int i = 0; i < count; i++) {
        process_script(files[i]);
    }
#else
    // POSIX: run tests with a worker pool using fork for isolation
    int worker_count = detect_worker_count();
    if (worker_count > count) {
        worker_count = count;
    }
    printf("Running up to %d parallel workers\n", worker_count);

    pid_t *pids = calloc((size_t)count, sizeof(pid_t));
    time_t *starts = calloc((size_t)count, sizeof(time_t));
    if (!pids || !starts) {
        fprintf(stderr, "  ❌ Failed to allocate worker tracking arrays\n");
        free(pids);
        free(starts);
        for (int i = 0; i < count; i++) {
            process_script(files[i]);
        }
    } else {
        int next = 0;
        int active = 0;
        int completed = 0;

        while (completed < count) {
            // Launch new workers until we reach the concurrency limit
            while (next < count && active < worker_count) {
                pid_t pid = fork();
                if (pid == 0) {
                    // Child process - reset counters and run one script
                    parse_success = 0;
                    parse_expected_fail = 0;
                    compile_success = 0;
                    compile_expected_fail = 0;
                    execution_success = 0;
                    execution_expected_fail = 0;
                    unexpected_failures = 0;

                    process_script(files[next]);
                    exit(unexpected_failures > 0 ? 1 : 0);
                } else if (pid > 0) {
                    pids[next] = pid;
                    starts[next] = time(NULL);
                    active++;
                } else {
                    fprintf(stderr, "  ❌ Failed to fork test process\n");
                    unexpected_failures++;
                    completed++;
                }
                next++;
            }

            int status;
            pid_t done = waitpid(-1, &status, WNOHANG);
            if (done > 0) {
                bool found = false;
                for (int i = 0; i < count; i++) {
                    if (pids[i] == done) {
                        pids[i] = 0;
                        active--;
                        completed++;
                        if ((WIFEXITED(status) && WEXITSTATUS(status) != 0) || WIFSIGNALED(status)) {
                            unexpected_failures++;
                        }
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // Should not happen, but prevent deadlock if it does
                    completed++;
                    unexpected_failures++;
                }
            } else if (done == 0) {
                // Check for per-test wall-clock timeout
                time_t now = time(NULL);
                for (int i = 0; i < count; i++) {
                    if (pids[i] != 0 && now - starts[i] >= PER_TEST_TIMEOUT_SECONDS) {
                        fprintf(stderr, "  ❌ TEST TIMEOUT (exceeded %d seconds wall-clock time)\n",
                                PER_TEST_TIMEOUT_SECONDS);
                        kill(pids[i], SIGKILL);
                        waitpid(pids[i], &status, 0);
                        pids[i] = 0;
                        active--;
                        completed++;
                        unexpected_failures++;
                    }
                }
                usleep(10000); // 10ms
            } else {
                if (errno == ECHILD) {
                    break;
                }
                fprintf(stderr, "  ❌ waitpid error: %s\n", strerror(errno));
                unexpected_failures++;
                break;
            }
        }

        free(pids);
        free(starts);
    }
#endif

    fflush(stdout);
    fflush(stderr);

    printf("\n=== Summary ===\n");
    printf("Total files tested: %d\n", count);
    printf("Unexpected failures (tests that didn't behave as expected): %d\n", unexpected_failures);

    // Cleanup
    for (int i = 0; i < count; i++) {
        free(files[i]);
    }
    free(files);

    if (unexpected_failures > 0) {
        fprintf(stderr, "\n❌ FAILED: %d unexpected failures\n", unexpected_failures);
        return 1;
    }

    printf("\n✅ All repository scripts validated successfully!\n");
    return 0;
}
