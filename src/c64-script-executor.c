/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-executor.h"
#include "c64-effect.h"
#include "c64-keyboard.h"
#include "c64-logging.h"
#include "c64-palette.h"
#include "c64-record.h"
#include "c64-rest-client.h"
#include "c64-source.h"

#include <obs-module.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/threading.h>

// Cross-platform sleep
#ifdef _WIN32
#include <windows.h>
#define usleep(x) Sleep((x) / 1000)
#else
#include <unistd.h>
#endif

#define EXECUTOR_LOG_PREFIX "[c64-script-executor] "
#define MAX_LABELS 64
#define MAX_LOOP_STACK 16

typedef struct {
    char name[256];
    size_t command_index;
} label_entry_t;

typedef struct {
    size_t start_index;
    int count;
    int remaining;
} loop_entry_t;

struct c64_script_executor {
    obs_source_t *source;
    pthread_t thread;
    bool thread_running;

    // Execution state
    c64_script_t *script;
    size_t current_index;
    c64_script_status_t status;
    char error_msg[512];
    char current_command[64];

    // Control
    volatile bool should_stop;
    pthread_mutex_t mutex;

    // Labels and loops
    label_entry_t labels[MAX_LABELS];
    size_t num_labels;
    loop_entry_t loop_stack[MAX_LOOP_STACK];
    size_t loop_stack_depth;
};

static const char *command_names[] = {
    "effect", "effect_param", "palette",      "play_sid",    "run_prg", "mount_disk", "autostart", "reset",
    "reboot", "wait",         "record_start", "record_stop", "stop",    "loop",       "label",     "goto",
};

// Helper: find label by name
static int find_label(c64_script_executor_t *executor, const char *name)
{
    for (size_t i = 0; i < executor->num_labels; i++) {
        if (strcmp(executor->labels[i].name, name) == 0) {
            return (int)executor->labels[i].command_index;
        }
    }
    return -1;
}

// Helper: build label map
static bool build_label_map(c64_script_executor_t *executor)
{
    executor->num_labels = 0;

    for (size_t i = 0; i < executor->script->num_commands; i++) {
        c64_script_command_t *cmd = &executor->script->commands[i];
        if (cmd->type == C64_SCRIPT_CMD_LABEL) {
            if (executor->num_labels >= MAX_LABELS) {
                snprintf(executor->error_msg, sizeof(executor->error_msg), "Too many labels (max %d)", MAX_LABELS);
                return false;
            }

            // Check for duplicate
            if (find_label(executor, cmd->arg1) != -1) {
                snprintf(executor->error_msg, sizeof(executor->error_msg), "Duplicate label: %.100s", cmd->arg1);
                return false;
            }

            label_entry_t *label = &executor->labels[executor->num_labels++];
            strncpy(label->name, cmd->arg1, sizeof(label->name) - 1);
            label->command_index = i;
        }
    }

    return true;
}

// Helper: execute a single command
static bool execute_command(c64_script_executor_t *executor, c64_script_command_t *cmd)
{
    pthread_mutex_lock(&executor->mutex);
    strncpy(executor->current_command, command_names[cmd->type], sizeof(executor->current_command) - 1);
    pthread_mutex_unlock(&executor->mutex);

    obs_data_t *settings = obs_source_get_settings(executor->source);
    if (!settings) {
        snprintf(executor->error_msg, sizeof(executor->error_msg), "Failed to get source settings");
        return false;
    }

    // Get source context for REST client access
    void *source_data = obs_obj_get_data(executor->source);
    void *rest_client = c64_source_get_rest_client(source_data);

    bool success = true;

    switch (cmd->type) {
    case C64_SCRIPT_CMD_EFFECT: {
        // Apply effect preset via settings
        if (!c64_effect_apply(settings, cmd->arg1)) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "Effect preset not found: %.100s", cmd->arg1);
            success = false;
        } else {
            obs_source_update(executor->source, settings);
        }
        break;
    }

    case C64_SCRIPT_CMD_EFFECT_PARAM: {
        // Set effect parameter directly
        double value = atof(cmd->arg2);
        obs_data_set_double(settings, cmd->arg1, value);
        obs_source_update(executor->source, settings);
        break;
    }

    case C64_SCRIPT_CMD_PALETTE: {
        // Apply palette by name
        obs_data_set_string(settings, "palette", cmd->arg1);
        obs_source_update(executor->source, settings);
        break;
    }

    case C64_SCRIPT_CMD_PLAY_SID: {
        if (!rest_client) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "REST client not available");
            success = false;
            break;
        }

        if (cmd->path_type == C64_SCRIPT_PATH_C64U) {
            if (!c64_rest_play_sid_path(rest_client, cmd->arg1, cmd->song_number)) {
                snprintf(executor->error_msg, sizeof(executor->error_msg), "Failed to play SID: %.100s", cmd->arg1);
                success = false;
            }
        } else {
            // TODO: Read local file and upload via c64_rest_play_sid
            C64_LOG_WARNING(EXECUTOR_LOG_PREFIX "Local file upload not yet implemented");
            snprintf(executor->error_msg, sizeof(executor->error_msg), "Local file upload not yet implemented");
            success = false;
        }
        break;
    }

    case C64_SCRIPT_CMD_RUN_PRG: {
        if (!rest_client) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "REST client not available");
            success = false;
            break;
        }

        if (cmd->path_type == C64_SCRIPT_PATH_C64U) {
            if (!c64_rest_run_prg_path(rest_client, cmd->arg1)) {
                snprintf(executor->error_msg, sizeof(executor->error_msg), "Failed to run PRG: %.100s", cmd->arg1);
                success = false;
            }
        } else {
            // TODO: Read local file and upload via c64_rest_run_prg
            C64_LOG_WARNING(EXECUTOR_LOG_PREFIX "Local file upload not yet implemented");
            snprintf(executor->error_msg, sizeof(executor->error_msg), "Local file upload not yet implemented");
            success = false;
        }
        break;
    }

    case C64_SCRIPT_CMD_MOUNT_DISK: {
        if (!rest_client) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "REST client not available");
            success = false;
            break;
        }

        if (cmd->path_type == C64_SCRIPT_PATH_C64U) {
            // Default to drive 'a'
            if (!c64_rest_mount_disk_path(rest_client, 'a', cmd->arg1)) {
                snprintf(executor->error_msg, sizeof(executor->error_msg), "Failed to mount disk: %.100s", cmd->arg1);
                success = false;
            }
        } else {
            // TODO: Read local file and upload via c64_rest_mount_disk
            C64_LOG_WARNING(EXECUTOR_LOG_PREFIX "Local file upload not yet implemented");
            snprintf(executor->error_msg, sizeof(executor->error_msg), "Local file upload not yet implemented");
            success = false;
        }
        break;
    }

    case C64_SCRIPT_CMD_AUTOSTART: {
        // Inject autostart sequence via keyboard (LOAD"*",8,1\rRUN\r)
        void *keyboard = c64_source_get_keyboard(source_data);
        if (!keyboard) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "Keyboard not available for autostart");
            success = false;
            break;
        }

        // Inject default autostart template
        const char *template = "LOAD\"*\",8,1\rRUN\r";
        for (size_t i = 0; template[i]; i++) {
            c64_output_t output = {0};
            output.mode = C64_OUTPUT_PETSCII;
            if (template[i] == '\r') {
                output.data.petscii = 0x0D; // RETURN
            } else {
                output.data.petscii = (uint8_t)template[i];
            }
            c64_keyboard_queue_output(keyboard, &output);
        }
        break;
    }

    case C64_SCRIPT_CMD_RESET: {
        if (!rest_client) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "REST client not available");
            success = false;
            break;
        }

        if (!c64_rest_reset(rest_client)) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "Failed to reset C64");
            success = false;
        }
        break;
    }

    case C64_SCRIPT_CMD_REBOOT: {
        if (!rest_client) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "REST client not available");
            success = false;
            break;
        }

        if (!c64_rest_reboot(rest_client)) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "Failed to reboot C64");
            success = false;
        }
        break;
    }

    case C64_SCRIPT_CMD_WAIT: {
        pthread_mutex_lock(&executor->mutex);
        executor->status = C64_SCRIPT_STATUS_WAITING;
        pthread_mutex_unlock(&executor->mutex);

        // Wait with cancellation checking (100ms intervals)
        uint32_t elapsed_ms = 0;
        while (elapsed_ms < cmd->duration_ms && !executor->should_stop) {
            usleep(100000); // 100ms
            elapsed_ms += 100;
        }

        pthread_mutex_lock(&executor->mutex);
        executor->status = C64_SCRIPT_STATUS_RUNNING;
        pthread_mutex_unlock(&executor->mutex);
        break;
    }

    case C64_SCRIPT_CMD_RECORD_START: {
        // Start CSV/network recording via c64-record functions
        if (source_data) {
            c64_start_csv_recording(source_data);
            c64_start_network_recording(source_data);
        }
        break;
    }

    case C64_SCRIPT_CMD_RECORD_STOP: {
        // Stop CSV/network recording
        if (source_data) {
            c64_stop_csv_recording(source_data);
            c64_stop_network_recording(source_data);
        }
        break;
    }

    case C64_SCRIPT_CMD_STOP: {
        executor->should_stop = true;
        break;
    }

    case C64_SCRIPT_CMD_LOOP: {
        if (executor->loop_stack_depth >= MAX_LOOP_STACK) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "Loop stack overflow");
            success = false;
            break;
        }

        loop_entry_t *loop = &executor->loop_stack[executor->loop_stack_depth];
        loop->start_index = executor->current_index;
        loop->count = cmd->loop_count;
        loop->remaining = cmd->loop_count;
        executor->loop_stack_depth++;
        break;
    }

    case C64_SCRIPT_CMD_LABEL:
        // Labels are passive, no action
        break;

    case C64_SCRIPT_CMD_GOTO: {
        int target = find_label(executor, cmd->arg1);
        if (target == -1) {
            snprintf(executor->error_msg, sizeof(executor->error_msg), "Label not found: %.100s", cmd->arg1);
            success = false;
        } else {
            executor->current_index = target - 1; // -1 because loop will increment
        }
        break;
    }
    }

    obs_data_release(settings);
    return success;
}

// Worker thread function
static void *executor_thread(void *data)
{
    c64_script_executor_t *executor = data;

    C64_LOG_INFO(EXECUTOR_LOG_PREFIX "Starting execution with %zu commands", executor->script->num_commands);

    // Build label map
    if (!build_label_map(executor)) {
        pthread_mutex_lock(&executor->mutex);
        executor->status = C64_SCRIPT_STATUS_ERROR;
        pthread_mutex_unlock(&executor->mutex);
        C64_LOG_ERROR(EXECUTOR_LOG_PREFIX "Label map build failed: %s", executor->error_msg);
        return NULL;
    }

    // Execute commands sequentially
    pthread_mutex_lock(&executor->mutex);
    executor->status = C64_SCRIPT_STATUS_RUNNING;
    pthread_mutex_unlock(&executor->mutex);

    while (executor->current_index < executor->script->num_commands && !executor->should_stop) {
        c64_script_command_t *cmd = &executor->script->commands[executor->current_index];

        C64_LOG_DEBUG(EXECUTOR_LOG_PREFIX "Executing line %d: %s", cmd->line_number, command_names[cmd->type]);

        if (!execute_command(executor, cmd)) {
            pthread_mutex_lock(&executor->mutex);
            executor->status = C64_SCRIPT_STATUS_ERROR;
            pthread_mutex_unlock(&executor->mutex);
            C64_LOG_ERROR(EXECUTOR_LOG_PREFIX "Command failed: %s", executor->error_msg);
            break;
        }

        // Handle loop end
        if (executor->loop_stack_depth > 0) {
            loop_entry_t *loop = &executor->loop_stack[executor->loop_stack_depth - 1];
            if (executor->current_index + 1 >= executor->script->num_commands ||
                executor->current_index + 1 < loop->start_index) {
                // End of loop block
                if (loop->count == 0 || loop->remaining > 0) {
                    // Infinite loop or iterations remaining
                    if (loop->remaining > 0) {
                        loop->remaining--;
                    }
                    executor->current_index = loop->start_index;
                    continue;
                } else {
                    // Loop finished
                    executor->loop_stack_depth--;
                }
            }
        }

        executor->current_index++;
    }

    pthread_mutex_lock(&executor->mutex);
    if (executor->status == C64_SCRIPT_STATUS_RUNNING) {
        executor->status = C64_SCRIPT_STATUS_COMPLETED;
    }
    executor->thread_running = false;
    pthread_mutex_unlock(&executor->mutex);

    C64_LOG_INFO(EXECUTOR_LOG_PREFIX "Execution completed with status %d", executor->status);
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
    executor->status = C64_SCRIPT_STATUS_IDLE;
    pthread_mutex_init(&executor->mutex, NULL);

    return executor;
}

void c64_script_executor_destroy(c64_script_executor_t *executor)
{
    if (!executor) {
        return;
    }

    c64_script_executor_stop(executor);

    // Wait for thread to finish
    if (executor->thread_running) {
        pthread_join(executor->thread, NULL);
    }

    pthread_mutex_destroy(&executor->mutex);
    free(executor);
}

bool c64_script_executor_start(c64_script_executor_t *executor, c64_script_t *script)
{
    if (!executor || !script) {
        return false;
    }

    pthread_mutex_lock(&executor->mutex);

    if (executor->thread_running) {
        pthread_mutex_unlock(&executor->mutex);
        return false;
    }

    executor->script = script;
    executor->current_index = 0;
    executor->should_stop = false;
    executor->error_msg[0] = '\0';
    executor->current_command[0] = '\0';
    executor->num_labels = 0;
    executor->loop_stack_depth = 0;
    executor->status = C64_SCRIPT_STATUS_RUNNING;
    executor->thread_running = true;

    pthread_mutex_unlock(&executor->mutex);

    if (pthread_create(&executor->thread, NULL, executor_thread, executor) != 0) {
        pthread_mutex_lock(&executor->mutex);
        executor->thread_running = false;
        executor->status = C64_SCRIPT_STATUS_ERROR;
        snprintf(executor->error_msg, sizeof(executor->error_msg), "Failed to create thread");
        pthread_mutex_unlock(&executor->mutex);
        return false;
    }

    return true;
}

void c64_script_executor_stop(c64_script_executor_t *executor)
{
    if (!executor) {
        return;
    }

    executor->should_stop = true;
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
        return C64_SCRIPT_STATUS_IDLE;
    }

    pthread_mutex_lock(&executor->mutex);
    c64_script_status_t status = executor->status;
    pthread_mutex_unlock(&executor->mutex);

    return status;
}

int c64_script_executor_get_current_line(c64_script_executor_t *executor)
{
    if (!executor || !executor->script) {
        return 0;
    }

    pthread_mutex_lock(&executor->mutex);
    int line = 0;
    if (executor->current_index < executor->script->num_commands) {
        line = executor->script->commands[executor->current_index].line_number;
    }
    pthread_mutex_unlock(&executor->mutex);

    return line;
}

int c64_script_executor_get_progress(c64_script_executor_t *executor)
{
    if (!executor || !executor->script || executor->script->num_commands == 0) {
        return -1;
    }

    pthread_mutex_lock(&executor->mutex);
    int progress = (int)((executor->current_index * 100) / executor->script->num_commands);
    pthread_mutex_unlock(&executor->mutex);

    return progress;
}

const char *c64_script_executor_get_current_command(c64_script_executor_t *executor)
{
    if (!executor) {
        return NULL;
    }

    pthread_mutex_lock(&executor->mutex);
    const char *cmd = executor->current_command[0] ? executor->current_command : NULL;
    pthread_mutex_unlock(&executor->mutex);

    return cmd;
}

const char *c64_script_executor_get_error(c64_script_executor_t *executor)
{
    if (!executor) {
        return NULL;
    }

    pthread_mutex_lock(&executor->mutex);
    const char *err = executor->error_msg[0] ? executor->error_msg : NULL;
    pthread_mutex_unlock(&executor->mutex);

    return err;
}
