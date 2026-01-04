/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-executor.h"
#include "c64-script.h"
#include "c64-script-runtime.h"
#include "c64-logging.h"
#include "c64-source.h"

#include <obs-module.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXECUTOR_LOG_PREFIX "🕹 SCRIPT: "

struct c64_script_executor {
    obs_source_t *source;

    pthread_t thread;
    bool thread_running;

    pthread_mutex_t mutex;

    c64_script_status_t status;
    char error_msg[512];

    c64script_runtime_t *runtime;
};

static const char *opcode_name(c64script_opcode_t opcode)
{
    switch (opcode) {
    case OP_NOP:
        return "NOP";
    case OP_PUSH_CONST:
        return "PUSH_CONST";
    case OP_PUSH_VAR:
        return "PUSH_VAR";
    case OP_POP_VAR:
        return "POP_VAR";
    case OP_ADD:
        return "ADD";
    case OP_SUBTRACT:
        return "SUBTRACT";
    case OP_MULTIPLY:
        return "MULTIPLY";
    case OP_DIVIDE:
        return "DIVIDE";
    case OP_NEGATE:
        return "NEGATE";
    case OP_EQ:
        return "EQ";
    case OP_NE:
        return "NE";
    case OP_LT:
        return "LT";
    case OP_LE:
        return "LE";
    case OP_GT:
        return "GT";
    case OP_GE:
        return "GE";
    case OP_NOT:
        return "NOT";
    case OP_AND:
        return "AND";
    case OP_XOR:
        return "XOR";
    case OP_OR:
        return "OR";
    case OP_JUMP:
        return "JUMP";
    case OP_JUMP_IF_FALSE:
        return "JUMP_IF_FALSE";
    case OP_CALL:
        return "CALL";
    case OP_RETURN:
        return "RETURN";
    case OP_FOR_INIT:
        return "FOR_INIT";
    case OP_FOR_CHECK:
        return "FOR_CHECK";
    case OP_FOR_INCR:
        return "FOR_INCR";
    case OP_WHILE_CHECK:
        return "WHILE_CHECK";
    case OP_WAIT:
        return "WAIT";
    case OP_WAIT_UNTIL:
        return "WAIT_UNTIL";
    case OP_CALL_PEEK:
        return "PEEK";
    case OP_EFFECT:
        return "EFFECT";
    case OP_EFFECTPARAM:
        return "EFFECTPARAM";
    case OP_PALETTE:
        return "PALETTE";
    case OP_PLAYSID:
        return "PLAYSID";
    case OP_RUNPRG:
        return "RUNPRG";
    case OP_MOUNTDISK:
        return "MOUNTDISK";
    case OP_AUTOSTART:
        return "AUTOSTART";
    case OP_RESET:
        return "RESET";
    case OP_REBOOT:
        return "REBOOT";
    case OP_RECORDSTART:
        return "RECORDSTART";
    case OP_RECORDSTOP:
        return "RECORDSTOP";
    case OP_TYPE:
        return "TYPE";
    case OP_KEY:
        return "KEY";
    case OP_POKE_SINGLE:
        return "POKE";
    case OP_POKE_ARRAY:
        return "POKE[]";
    case OP_LOGFILE:
        return "LOGFILE";
    case OP_LOG:
        return "LOG";
    case OP_PRINT:
        return "PRINT";
    case OP_TRON:
        return "TRON";
    case OP_TROFF:
        return "TROFF";
    case OP_STOP:
        return "STOP";
    case OP_HALT:
        return "HALT";
    default:
        return "UNKNOWN";
    }
}

static bool load_text_file(const char *path, char **out_text, size_t *out_size, char *error_msg, size_t error_size)
{
    if (!path || !out_text || !out_size) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Invalid arguments");
        }
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to open script file: %s", path);
        }
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to seek script file");
        }
        return false;
    }

    long fsize = ftell(f);
    if (fsize < 0 || fsize > (long)C64SCRIPT_MAX_SCRIPT_SIZE) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Script file too large");
        }
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to seek script file");
        }
        return false;
    }

    char *buf = malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Out of memory");
        }
        return false;
    }

    size_t read_count = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    if (read_count != (size_t)fsize) {
        free(buf);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to read script file");
        }
        return false;
    }

    buf[fsize] = '\0';
    *out_text = buf;
    *out_size = (size_t)fsize;
    return true;
}

static bool compile_script(c64_script_executor_t *executor, const char *script_file_path,
                           c64script_runtime_t **out_runtime, char *error_msg, size_t error_size)
{
    char *source = NULL;
    size_t source_size = 0;

    if (!load_text_file(script_file_path, &source, &source_size, error_msg, error_size)) {
        return false;
    }

    char parse_error[1024] = {0};
    c64script_ast_node_t *ast = c64script_parse(source, source_size, parse_error, sizeof(parse_error));
    if (!ast) {
        free(source);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "%s", parse_error[0] ? parse_error : "Parse failed");
        }
        return false;
    }

    c64script_runtime_t *runtime = c64script_runtime_create();
    if (!runtime) {
        c64script_ast_free(ast);
        free(source);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to create runtime");
        }
        return false;
    }

    // Store source text for line display
    runtime->source_text = source; // Transfer ownership to runtime
    runtime->source_text_size = source_size;

    // Integration pointers
    void *source_data = obs_obj_get_data(executor->source);
    runtime->source_data = source_data;
    runtime->obs_source = executor->source;
    runtime->rest_client = c64_source_get_rest_client(source_data);
    runtime->keyboard = c64_source_get_keyboard(source_data);

    char compile_error[1024] = {0};
    bool ok = c64script_compile(ast, runtime, compile_error, sizeof(compile_error));
    c64script_ast_free(ast);

    if (!ok) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "%s", compile_error[0] ? compile_error : "Compile failed");
        }
        c64script_runtime_destroy(runtime);
        return false;
    }

    *out_runtime = runtime;
    return true;
}

static void *script_thread_main(void *data)
{
    c64_script_executor_t *executor = data;

    bool ok = c64script_execute(executor->runtime);

    pthread_mutex_lock(&executor->mutex);
    executor->thread_running = false;
    if (executor->runtime && executor->runtime->should_stop) {
        executor->status = C64_SCRIPT_STATUS_IDLE;
        executor->error_msg[0] = '\0';
    } else if (!ok) {
        executor->status = C64_SCRIPT_STATUS_ERROR;
        const char *runtime_error = executor->runtime ? executor->runtime->error_msg : "Runtime error";
        snprintf(executor->error_msg, sizeof(executor->error_msg), "%.*s", (int)(sizeof(executor->error_msg) - 1),
                 runtime_error ? runtime_error : "Runtime error");
    } else {
        executor->status = C64_SCRIPT_STATUS_COMPLETED;
        executor->error_msg[0] = '\0';
    }
    pthread_mutex_unlock(&executor->mutex);

    return NULL;
}

c64_script_executor_t *c64_script_executor_create(obs_source_t *source)
{
    if (!source) {
        return NULL;
    }

    c64_script_executor_t *executor = calloc(1, sizeof(c64_script_executor_t));
    if (!executor) {
        return NULL;
    }

    executor->source = source;
    executor->thread_running = false;
    executor->status = C64_SCRIPT_STATUS_IDLE;
    executor->error_msg[0] = '\0';
    executor->runtime = NULL;
    pthread_mutex_init(&executor->mutex, NULL);

    return executor;
}

void c64_script_executor_destroy(c64_script_executor_t *executor)
{
    if (!executor) {
        return;
    }

    c64_script_executor_stop(executor);

    if (executor->runtime) {
        c64script_runtime_destroy(executor->runtime);
        executor->runtime = NULL;
    }

    pthread_mutex_destroy(&executor->mutex);
    free(executor);
}

bool c64_script_executor_validate_file(c64_script_executor_t *executor, const char *script_file_path)
{
    if (!executor || !script_file_path || script_file_path[0] == '\0') {
        return false;
    }

    char error[512] = {0};
    c64script_runtime_t *runtime = NULL;
    bool ok = compile_script(executor, script_file_path, &runtime, error, sizeof(error));

    pthread_mutex_lock(&executor->mutex);
    if (!ok) {
        executor->status = C64_SCRIPT_STATUS_ERROR;
        snprintf(executor->error_msg, sizeof(executor->error_msg), "%s", error);
    } else {
        executor->error_msg[0] = '\0';
        if (executor->status == C64_SCRIPT_STATUS_ERROR) {
            executor->status = C64_SCRIPT_STATUS_IDLE;
        }
    }
    pthread_mutex_unlock(&executor->mutex);

    if (runtime) {
        c64script_runtime_destroy(runtime);
    }

    return ok;
}

bool c64_script_executor_start(c64_script_executor_t *executor, const char *script_file_path)
{
    if (!executor || !script_file_path || script_file_path[0] == '\0') {
        return false;
    }

    pthread_mutex_lock(&executor->mutex);
    if (executor->thread_running) {
        pthread_mutex_unlock(&executor->mutex);
        return false;
    }
    pthread_mutex_unlock(&executor->mutex);

    if (executor->runtime) {
        c64script_runtime_destroy(executor->runtime);
        executor->runtime = NULL;
    }

    char error[512] = {0};
    if (!compile_script(executor, script_file_path, &executor->runtime, error, sizeof(error))) {
        pthread_mutex_lock(&executor->mutex);
        executor->status = C64_SCRIPT_STATUS_ERROR;
        snprintf(executor->error_msg, sizeof(executor->error_msg), "%s", error);
        pthread_mutex_unlock(&executor->mutex);
        return false;
    }

    pthread_mutex_lock(&executor->mutex);
    executor->status = C64_SCRIPT_STATUS_RUNNING;
    executor->error_msg[0] = '\0';
    executor->thread_running = true;
    pthread_mutex_unlock(&executor->mutex);

    int rc = pthread_create(&executor->thread, NULL, script_thread_main, executor);
    if (rc != 0) {
        pthread_mutex_lock(&executor->mutex);
        executor->thread_running = false;
        executor->status = C64_SCRIPT_STATUS_ERROR;
        snprintf(executor->error_msg, sizeof(executor->error_msg), "Failed to start script thread");
        pthread_mutex_unlock(&executor->mutex);
        return false;
    }

    C64_LOG_INFO(EXECUTOR_LOG_PREFIX "Started script: %s", script_file_path);
    return true;
}

void c64_script_executor_stop(c64_script_executor_t *executor)
{
    if (!executor) {
        return;
    }

    pthread_mutex_lock(&executor->mutex);
    bool running = executor->thread_running;
    pthread_mutex_unlock(&executor->mutex);

    if (!running) {
        return;
    }

    if (executor->runtime) {
        executor->runtime->should_stop = true;
    }

    pthread_join(executor->thread, NULL);
    pthread_mutex_lock(&executor->mutex);
    executor->thread_running = false;
    if (executor->status == C64_SCRIPT_STATUS_RUNNING) {
        executor->status = C64_SCRIPT_STATUS_IDLE;
    }
    pthread_mutex_unlock(&executor->mutex);
}

bool c64_script_executor_is_running(c64_script_executor_t *executor)
{
    if (!executor) {
        return false;
    }

    pthread_mutex_lock(&executor->mutex);
    bool running = executor->thread_running;
    pthread_mutex_unlock(&executor->mutex);
    return running;
}

c64_script_status_t c64_script_executor_get_status(c64_script_executor_t *executor)
{
    if (!executor) {
        return C64_SCRIPT_STATUS_ERROR;
    }

    pthread_mutex_lock(&executor->mutex);
    c64_script_status_t status = executor->status;
    pthread_mutex_unlock(&executor->mutex);
    return status;
}

int c64_script_executor_get_current_line(c64_script_executor_t *executor)
{
    if (!executor || !executor->runtime) {
        return 0;
    }

    int line = executor->runtime->error_line;
    return line > 0 ? line : 0;
}

int c64_script_executor_get_progress(c64_script_executor_t *executor)
{
    if (!executor || !executor->runtime || executor->runtime->bytecode_size == 0) {
        return -1;
    }

    size_t ip = executor->runtime->ip;
    size_t size = executor->runtime->bytecode_size;
    if (ip > size) {
        ip = size;
    }

    return (int)((double)ip * 100.0 / (double)size);
}

const char *c64_script_executor_get_current_command(c64_script_executor_t *executor)
{
#ifdef _MSC_VER
    static __declspec(thread) char buf[64];
#else
    static __thread char buf[64];
#endif

    if (!executor || !executor->runtime || !executor->runtime->bytecode || executor->runtime->bytecode_size == 0) {
        return NULL;
    }

    size_t ip = executor->runtime->ip;
    if (ip == 0) {
        ip = 0;
    } else {
        ip = ip - 1;
    }
    if (ip >= executor->runtime->bytecode_size) {
        ip = executor->runtime->bytecode_size - 1;
    }

    c64script_instruction_t *instr = &executor->runtime->bytecode[ip];
    snprintf(buf, sizeof(buf), "%s", opcode_name(instr->opcode));
    return buf;
}

const char *c64_script_executor_get_error(c64_script_executor_t *executor)
{
    if (!executor) {
        return NULL;
    }

    pthread_mutex_lock(&executor->mutex);
    const char *err = (executor->status == C64_SCRIPT_STATUS_ERROR) ? executor->error_msg : NULL;
    pthread_mutex_unlock(&executor->mutex);
    return err;
}

void c64_script_executor_pause(c64_script_executor_t *executor)
{
    if (!executor || !executor->runtime) {
        return;
    }

    pthread_mutex_lock(&executor->mutex);
    if (executor->status == C64_SCRIPT_STATUS_RUNNING) {
        executor->runtime->should_pause = true;
        executor->status = C64_SCRIPT_STATUS_PAUSED;
    }
    pthread_mutex_unlock(&executor->mutex);
}

void c64_script_executor_resume(c64_script_executor_t *executor)
{
    if (!executor || !executor->runtime) {
        return;
    }

    pthread_mutex_lock(&executor->mutex);
    if (executor->status == C64_SCRIPT_STATUS_PAUSED) {
        executor->runtime->is_paused = false;
        executor->runtime->should_pause = false;
        executor->status = C64_SCRIPT_STATUS_RUNNING;
    }
    pthread_mutex_unlock(&executor->mutex);
}

bool c64_script_executor_step(c64_script_executor_t *executor)
{
    if (!executor || !executor->runtime) {
        return false;
    }

    pthread_mutex_lock(&executor->mutex);
    if (executor->status != C64_SCRIPT_STATUS_PAUSED) {
        pthread_mutex_unlock(&executor->mutex);
        return false;
    }

    // Activate step mode - this will execute one line and pause again
    executor->runtime->step_mode = true;
    pthread_mutex_unlock(&executor->mutex);

    return true;
}

static void get_source_line(const char *source_text, int line_number, char *out_buf, size_t out_size)
{
    if (!source_text || !out_buf || out_size == 0 || line_number <= 0) {
        if (out_buf && out_size > 0) {
            out_buf[0] = '\0';
        }
        return;
    }

    int current_line = 1;
    const char *line_start = source_text;
    const char *p = source_text;

    // Find the start of the requested line
    while (*p && current_line < line_number) {
        if (*p == '\n') {
            current_line++;
            line_start = p + 1;
        }
        p++;
    }

    // If we found the line, copy it
    if (current_line == line_number) {
        const char *line_end = line_start;
        while (*line_end && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        size_t line_len = line_end - line_start;
        if (line_len > out_size - 1) {
            line_len = out_size - 1;
        }

        memcpy(out_buf, line_start, line_len);
        out_buf[line_len] = '\0';
    } else {
        out_buf[0] = '\0';
    }
}

int c64_script_executor_get_last_executed_line(c64_script_executor_t *executor, char *line_text, size_t line_text_size)
{
    if (!executor) {
        return 0;
    }

    pthread_mutex_lock(&executor->mutex);

    c64script_runtime_t *runtime = executor->runtime;
    if (!runtime) {
        if (line_text && line_text_size > 0) {
            line_text[0] = '\0';
        }
        pthread_mutex_unlock(&executor->mutex);
        return 0;
    }

    int line_num = runtime->last_executed_line;

    if (line_text && line_text_size > 0) {
        if (line_num > 0 && runtime->source_text) {
            get_source_line(runtime->source_text, line_num, line_text, line_text_size);
        } else {
            line_text[0] = '\0';
        }
    }

    pthread_mutex_unlock(&executor->mutex);

    return line_num;
}

int c64_script_executor_get_next_line(c64_script_executor_t *executor, char *line_text, size_t line_text_size)
{
    if (!executor) {
        return 0;
    }

    pthread_mutex_lock(&executor->mutex);

    c64script_runtime_t *runtime = executor->runtime;
    if (!runtime) {
        if (line_text && line_text_size > 0) {
            line_text[0] = '\0';
        }
        pthread_mutex_unlock(&executor->mutex);
        return 0;
    }

    int line_num = runtime->next_line_to_execute;

    if (line_text && line_text_size > 0) {
        if (line_num > 0 && runtime->source_text) {
            get_source_line(runtime->source_text, line_num, line_text, line_text_size);
        } else {
            line_text[0] = '\0';
        }
    }

    pthread_mutex_unlock(&executor->mutex);
    return line_num;
}

void c64_script_executor_log_variables(c64_script_executor_t *executor)
{
    if (!executor || !executor->runtime) {
        C64_LOG_INFO(EXECUTOR_LOG_PREFIX "No runtime available for variable logging");
        return;
    }

    c64script_runtime_t *runtime = executor->runtime;

    C64_LOG_INFO(EXECUTOR_LOG_PREFIX "=== Variable Dump ===");

    if (runtime->variable_count == 0) {
        C64_LOG_INFO(EXECUTOR_LOG_PREFIX "No variables defined");
    } else {
        // Log variables in alphabetical order for consistency
        for (size_t i = 0; i < runtime->variable_count; i++) {
            c64script_variable_t *var = &runtime->variables[i];
            if (var->value.type == VALUE_NUMBER) {
                C64_LOG_INFO(EXECUTOR_LOG_PREFIX "  %s = %.6g (number)", var->name, var->value.as.number);
            } else if (var->value.type == VALUE_STRING) {
                const char *str = var->value.as.string ? var->value.as.string : "";
                // Truncate long strings
                if (strlen(str) > 200) {
                    C64_LOG_INFO(EXECUTOR_LOG_PREFIX "  %s = \"%.197s...\" (string, %zu chars)", var->name, str,
                                 strlen(str));
                } else {
                    C64_LOG_INFO(EXECUTOR_LOG_PREFIX "  %s = \"%s\" (string)", var->name, str);
                }
            }
        }
    }

    C64_LOG_INFO(EXECUTOR_LOG_PREFIX "=== End Variable Dump ===");
}
