/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-automation.h"
#include "c64-automation-hvsc.h"
#include "c64-automation-internal.h"
#include "c64-automation-playlist.h"
#include "c64-file.h"
#include "c64-logging.h"
#include "c64-rest-client.h"
#include <util/platform.h>
#include <obs-module.h>
#include <obs.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define strcasecmp _stricmp
#define sleep(x) Sleep((x) * 1000)
#else
#include <strings.h>
#include <unistd.h>
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define AUTOMATION_LOG_PREFIX "[c64-automation] "
#define RESET_DELAY_MS 500

static void c64_automation_queue_ui_update(c64_automation_t *automation);
static bool automation_should_stop(c64_automation_t *automation);

static void c64_automation_clear_songlengths(c64_automation_t *automation)
{
    if (!automation) {
        return;
    }

    c64_hvsc_songlength_db_clear(&automation->songlength_db);
}

static bool c64_automation_ensure_songlengths_loaded(c64_automation_t *automation)
{
    if (!automation || !automation->use_songlengths) {
        return false;
    }

    if (automation->songlength_db.loaded) {
        return true;
    }

    if (automation->config.file_source != C64_FILE_SOURCE_LOCAL) {
        return false;
    }

    if (automation->config.songlengths_path[0] != '\0') {
        if (c64_hvsc_songlength_db_load(&automation->songlength_db, automation->config.songlengths_path)) {
            C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Using songlengths file: %s", automation->songlength_db.source_path);
            return true;
        }
        C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Failed to load songlengths file: %s",
                        automation->config.songlengths_path);
    }

    char root_path[512];
    if (automation->config.mode == C64_AUTO_MODE_FOLDER) {
        strncpy(root_path, automation->config.folder_path, sizeof(root_path) - 1);
        root_path[sizeof(root_path) - 1] = '\0';
    } else {
        const char *last_sep = strrchr(automation->config.folder_path, '/');
        const char *last_backslash = strrchr(automation->config.folder_path, '\\');
        if (last_backslash && (!last_sep || last_backslash > last_sep)) {
            last_sep = last_backslash;
        }
        if (last_sep) {
            size_t length = (size_t)(last_sep - automation->config.folder_path);
            if (length >= sizeof(root_path)) {
                length = sizeof(root_path) - 1;
            }
            memcpy(root_path, automation->config.folder_path, length);
            root_path[length] = '\0';
        } else {
            strncpy(root_path, automation->config.folder_path, sizeof(root_path) - 1);
            root_path[sizeof(root_path) - 1] = '\0';
        }
    }

    char songlengths_path[512];
    if (c64_hvsc_find_songlengths_file_local(root_path, songlengths_path, sizeof(songlengths_path))) {
        if (c64_hvsc_songlength_db_load(&automation->songlength_db, songlengths_path)) {
            C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Using songlengths file: %s", automation->songlength_db.source_path);
            return true;
        }
        C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Failed to load songlengths file: %s", songlengths_path);
    }

    return false;
}

const char *c64_automation_get_songlengths_path(c64_automation_t *automation)
{
    if (!automation) {
        return NULL;
    }

    if (c64_automation_ensure_songlengths_loaded(automation)) {
        return automation->songlength_db.source_path;
    }

    return NULL;
}

static bool c64_automation_lookup_songlength(c64_automation_t *automation, const char *path, double *out_seconds)
{
    if (!automation || !path || !out_seconds) {
        return false;
    }

    if (!c64_automation_ensure_songlengths_loaded(automation)) {
        return false;
    }

    char md5_hex[33];
    if (!c64_hvsc_md5_file_hex(path, md5_hex)) {
        return false;
    }

    return c64_hvsc_songlength_db_lookup(&automation->songlength_db, md5_hex, out_seconds);
}

bool c64_automation_get_songlength_seconds(c64_automation_t *automation, const char *path, double *out_seconds)
{
    return c64_automation_lookup_songlength(automation, path, out_seconds);
}

// Helper: Set status string (thread-safe)
static void set_status(c64_automation_t *automation, const char *status)
{
    pthread_mutex_lock(&automation->status_mutex);
    strncpy(automation->status, status, sizeof(automation->status) - 1);
    automation->status[sizeof(automation->status) - 1] = '\0';
    pthread_mutex_unlock(&automation->status_mutex);
}

static void c64_automation_apply_ui_update(void *data)
{
    obs_source_t *source = (obs_source_t *)data;
    if (!source) {
        return;
    }

    obs_source_update_properties(source);
    obs_source_release(source);
}

static void c64_automation_queue_ui_update(c64_automation_t *automation)
{
    if (!automation || !automation->source) {
        return;
    }

    obs_source_t *source_ref = obs_source_get_ref(automation->source);
    if (!source_ref) {
        return;
    }

    obs_queue_task(OBS_TASK_UI, c64_automation_apply_ui_update, source_ref, false);
}

static bool automation_should_stop(c64_automation_t *automation)
{
    if (!automation) {
        return true;
    }

    pthread_mutex_lock(&automation->status_mutex);
    bool should_stop = automation->should_stop;
    pthread_mutex_unlock(&automation->status_mutex);
    return should_stop;
}

// Helper: Load file into memory
static uint8_t *load_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 1024 * 1024) { // Max 1MB
        fclose(f);
        return NULL;
    }

    uint8_t *data = malloc((size_t)fsize);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(data, 1, (size_t)fsize, f);
    fclose(f);

    if (read_size != (size_t)fsize) {
        free(data);
        return NULL;
    }

    *size = (size_t)fsize;
    return data;
}

// Worker thread function
static void *automation_worker(void *arg)
{
    c64_automation_t *automation = (c64_automation_t *)arg;

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Worker thread started");

    int i = 0;
    pthread_mutex_lock(&automation->status_mutex);
    if (automation->current_index >= 0) {
        i = automation->current_index;
    }
    pthread_mutex_unlock(&automation->status_mutex);

    while (true) {
        if (i >= automation->num_files) {
            break;
        }
        pthread_mutex_lock(&automation->status_mutex);
        bool should_stop = automation->should_stop;
        pthread_mutex_unlock(&automation->status_mutex);
        if (should_stop) {
            break;
        }
        pthread_mutex_lock(&automation->status_mutex);
        automation->current_index = i;
        pthread_mutex_unlock(&automation->status_mutex);
        file_entry_t *file = &automation->files[i];

        // Extract filename for status message
        const char *filename = strrchr(file->path, '/');
        filename = filename ? filename + 1 : file->path;

        char status_msg[128];
        int written =
            snprintf(status_msg, sizeof(status_msg), "Playing %d/%d: %.80s", i + 1, automation->num_files, filename);
        if (written > 0 && (size_t)written < sizeof(status_msg)) {
            set_status(automation, status_msg);
        }

        // Store current file path (thread-safe)
        pthread_mutex_lock(&automation->status_mutex);
        strncpy(automation->current_file_path, file->path, sizeof(automation->current_file_path) - 1);
        automation->current_file_path[sizeof(automation->current_file_path) - 1] = '\0';
        pthread_mutex_unlock(&automation->status_mutex);

        c64_automation_queue_ui_update(automation);

        C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Processing: %s", file->path);

        pthread_mutex_lock(&automation->status_mutex);
        bool reset_between_items = automation->config.reset_between_items;
        pthread_mutex_unlock(&automation->status_mutex);

        // Execute based on file source
        bool success = false;
        if (automation->config.file_source == C64_FILE_SOURCE_C64U) {
            // C64U filesystem: use path-based REST API
            switch (file->type) {
            case C64_FILE_TYPE_SID:
                success = c64_rest_play_sid_path(automation->rest_client, file->path, 0);
                break;
            case C64_FILE_TYPE_MOD:
                success = c64_rest_play_mod_path(automation->rest_client, file->path);
                break;
            case C64_FILE_TYPE_PRG:
                success = c64_rest_run_prg_path(automation->rest_client, file->path);
                break;
            case C64_FILE_TYPE_CRT:
                success = c64_rest_run_crt_path(automation->rest_client, file->path);
                break;
            case C64_FILE_TYPE_D64:
            case C64_FILE_TYPE_G64:
            case C64_FILE_TYPE_D71:
            case C64_FILE_TYPE_G71:
            case C64_FILE_TYPE_D81:
                // Reset before mounting
                if (reset_between_items) {
                    c64_rest_reset(automation->rest_client);
                    os_sleep_ms(RESET_DELAY_MS);
                }
                success = c64_rest_mount_disk_path(automation->rest_client, 'a', file->path);
                if (success && automation->keyboard) {
                    // Inject LOAD"*",8,1:RUN followed by RETURN
                    const char *template = automation->config.d64_autostart_template;
                    for (size_t j = 0; template[j] && !automation_should_stop(automation); j++) {
                        c64_output_t output = {0};
                        output.mode = C64_OUTPUT_PETSCII;
                        if (template[j] == '\r') {
                            output.data.petscii = 0x0D; // RETURN
                        } else {
                            output.data.petscii = (uint8_t)template[j];
                        }
                        c64_keyboard_queue_output(automation->keyboard, &output);
                    }
                }
                break;
            }
        } else {
            // Local filesystem: load file and upload via REST
            size_t file_size;
            uint8_t *file_data = load_file(file->path, &file_size);
            if (!file_data) {
                C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to load file: %s", file->path);
                continue;
            }

            switch (file->type) {
            case C64_FILE_TYPE_SID:
                success = c64_rest_play_sid(automation->rest_client, file_data, file_size, 0, NULL, 0);
                break;
            case C64_FILE_TYPE_MOD:
                success = c64_rest_play_mod(automation->rest_client, file_data, file_size);
                break;
            case C64_FILE_TYPE_PRG:
                success = c64_rest_run_prg(automation->rest_client, file_data, file_size);
                break;
            case C64_FILE_TYPE_CRT:
                success = c64_rest_run_crt(automation->rest_client, file_data, file_size);
                break;
            case C64_FILE_TYPE_D64:
            case C64_FILE_TYPE_G64:
            case C64_FILE_TYPE_D71:
            case C64_FILE_TYPE_G71:
            case C64_FILE_TYPE_D81:
                // Reset, mount (determine type from file extension), inject autostart
                if (reset_between_items) {
                    c64_rest_reset(automation->rest_client);
                    os_sleep_ms(RESET_DELAY_MS);
                }
                // Determine disk type string from file type
                const char *disk_type;
                switch (file->type) {
                case C64_FILE_TYPE_D64:
                    disk_type = "d64";
                    break;
                case C64_FILE_TYPE_G64:
                    disk_type = "g64";
                    break;
                case C64_FILE_TYPE_D71:
                    disk_type = "d71";
                    break;
                case C64_FILE_TYPE_G71:
                    disk_type = "g71";
                    break;
                case C64_FILE_TYPE_D81:
                    disk_type = "d81";
                    break;
                default:
                    disk_type = "d64";
                }
                success =
                    c64_rest_mount_disk(automation->rest_client, 'a', disk_type, "readwrite", file_data, file_size);
                if (success && automation->keyboard) {
                    // Inject LOAD"*",8,1:RUN followed by RETURN
                    const char *template = automation->config.d64_autostart_template;
                    for (size_t j = 0; template[j] && !automation_should_stop(automation); j++) {
                        c64_output_t output = {0};
                        output.mode = C64_OUTPUT_PETSCII;
                        if (template[j] == '\r') {
                            output.data.petscii = 0x0D; // RETURN
                        } else {
                            output.data.petscii = (uint8_t)template[j];
                        }
                        c64_keyboard_queue_output(automation->keyboard, &output);
                    }
                }
                break;
            }

            free(file_data);
        }

        if (!success) {
            C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to execute: %s", file->path);
        }

        // Wait for duration (check should_stop and skip_requested periodically)
        int songlength_ms_override = -1;
        if (automation->use_songlengths && file->type == C64_FILE_TYPE_SID) {
            double song_seconds = 0.0;
            if (c64_automation_lookup_songlength(automation, file->path, &song_seconds)) {
                songlength_ms_override = (int)(song_seconds * 1000.0 + 0.5);
                if (songlength_ms_override < 1000) {
                    songlength_ms_override = 1000;
                }
                C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Using songlength %.2fs for %s", song_seconds, file->path);
            }
        }

        int elapsed_ms = 0;
        int sleep_interval_ms = 100;

        while (true) {
            pthread_mutex_lock(&automation->status_mutex);
            bool skip = automation->skip_requested;
            if (skip) {
                automation->skip_requested = false;
            }
            int jump_target = automation->current_index;
            int duration_seconds = automation->config.duration_seconds;
            if (duration_seconds < 1) {
                duration_seconds = 1;
            }
            int duration_ms = (songlength_ms_override > 0) ? songlength_ms_override : duration_seconds * 1000;
            bool should_stop_loop = automation->should_stop;
            pthread_mutex_unlock(&automation->status_mutex);

            if (should_stop_loop || elapsed_ms >= duration_ms) {
                break;
            }

            if (skip) {
                C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Skipping current item");
                if (jump_target >= 0 && jump_target < automation->num_files && jump_target != i) {
                    i = jump_target;
                } else {
                    i++;
                }
                goto advance_item;
            }

            os_sleep_ms(sleep_interval_ms);
            elapsed_ms += sleep_interval_ms;
        }

        // Reset between items if configured (skip if disk type since already reset before mount)
        if (reset_between_items && file->type != C64_FILE_TYPE_D64 && file->type != C64_FILE_TYPE_G64 &&
            file->type != C64_FILE_TYPE_D71 && file->type != C64_FILE_TYPE_G71 && file->type != C64_FILE_TYPE_D81) {
            c64_rest_reset(automation->rest_client);
            os_sleep_ms(RESET_DELAY_MS);
        }
        i++;

    advance_item:
        continue;
    }

    // Clear current file path
    pthread_mutex_lock(&automation->status_mutex);
    automation->current_file_path[0] = '\0';
    pthread_mutex_unlock(&automation->status_mutex);

    bool stopped = automation_should_stop(automation);
    set_status(automation, stopped ? "stopped" : "completed");
    pthread_mutex_lock(&automation->status_mutex);
    automation->running = false;
    pthread_mutex_unlock(&automation->status_mutex);

    c64_automation_queue_ui_update(automation);

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Worker thread finished");
    return NULL;
}

c64_automation_t *c64_automation_create(void *rest_client, void *keyboard, obs_source_t *source)
{
    if (!rest_client) {
        return NULL;
    }

    c64_automation_t *automation = calloc(1, sizeof(c64_automation_t));
    if (!automation) {
        return NULL;
    }

    automation->rest_client = (c64_rest_client_t *)rest_client;
    automation->keyboard = (c64_keyboard_t *)keyboard;
    automation->source = source;
    automation->running = false;
    automation->should_stop = false;
    automation->skip_requested = false;
    automation->files = NULL;
    automation->num_files = 0;
    automation->files_capacity = 0;
    automation->current_index = 0;
    automation->playlist_ready = false;
    automation->playlist_config_valid = false;
    automation->use_songlengths = true;
    c64_hvsc_songlength_db_clear(&automation->songlength_db);
    strncpy(automation->status, "idle", sizeof(automation->status) - 1);

    pthread_mutex_init(&automation->status_mutex, NULL);

    // Set defaults
    automation->config.mode = C64_AUTO_MODE_OFF;
    automation->config.duration_seconds = 120;
    automation->config.reset_between_items = true;
    automation->config.use_songlengths = true;
    strncpy(automation->config.d64_autostart_template, "LOAD\"*\",8,1\rRUN\r",
            sizeof(automation->config.d64_autostart_template) - 1);

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Created automation engine");
    return automation;
}

void c64_automation_destroy(c64_automation_t *automation)
{
    if (!automation) {
        return;
    }

    // Stop if running
    if (c64_automation_is_running(automation)) {
        c64_automation_stop(automation);
    }

    c64_automation_clear_playlist_internal(automation);
    c64_automation_clear_songlengths(automation);

    pthread_mutex_destroy(&automation->status_mutex);
    free(automation);
}

void c64_automation_configure(c64_automation_t *automation, const c64_automation_config_t *config)
{
    if (!automation || !config) {
        return;
    }

    bool previous_use_songlengths = automation->use_songlengths;
    c64_file_source_t previous_source = automation->config.file_source;
    char previous_folder[512];
    char previous_songlengths[512];
    strncpy(previous_folder, automation->config.folder_path, sizeof(previous_folder) - 1);
    previous_folder[sizeof(previous_folder) - 1] = '\0';
    strncpy(previous_songlengths, automation->config.songlengths_path, sizeof(previous_songlengths) - 1);
    previous_songlengths[sizeof(previous_songlengths) - 1] = '\0';

    int duration_seconds = config->duration_seconds;
    if (duration_seconds < 1) {
        duration_seconds = 1;
    }

    pthread_mutex_lock(&automation->status_mutex);
    memcpy(&automation->config, config, sizeof(c64_automation_config_t));
    automation->config.duration_seconds = duration_seconds;
    pthread_mutex_unlock(&automation->status_mutex);
    automation->use_songlengths = config->use_songlengths;

    if (!automation->use_songlengths || automation->config.file_source != C64_FILE_SOURCE_LOCAL) {
        c64_automation_clear_songlengths(automation);
    } else if (previous_use_songlengths != automation->use_songlengths ||
               previous_source != automation->config.file_source ||
               strncmp(previous_folder, automation->config.folder_path, sizeof(previous_folder)) != 0 ||
               strncmp(previous_songlengths, automation->config.songlengths_path, sizeof(previous_songlengths)) != 0) {
        c64_automation_clear_songlengths(automation);
    }
    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Configured: mode=%d folder=%s shuffle=%d recursive=%d duration=%d",
                 config->mode, config->folder_path, config->shuffle, config->include_subfolders,
                 automation->config.duration_seconds);
}

void c64_automation_update_runtime_config(c64_automation_t *automation, const c64_automation_config_t *config)
{
    if (!automation || !config) {
        return;
    }

    char previous_songlengths[512];
    strncpy(previous_songlengths, automation->config.songlengths_path, sizeof(previous_songlengths) - 1);
    previous_songlengths[sizeof(previous_songlengths) - 1] = '\0';

    int duration_seconds = config->duration_seconds;
    if (duration_seconds < 1) {
        duration_seconds = 1;
    }

    pthread_mutex_lock(&automation->status_mutex);
    automation->config.duration_seconds = duration_seconds;
    automation->config.reset_between_items = config->reset_between_items;
    if (config->songlengths_path[0] != '\0') {
        strncpy(automation->config.songlengths_path, config->songlengths_path,
                sizeof(automation->config.songlengths_path) - 1);
        automation->config.songlengths_path[sizeof(automation->config.songlengths_path) - 1] = '\0';
    } else {
        automation->config.songlengths_path[0] = '\0';
    }
    pthread_mutex_unlock(&automation->status_mutex);

    if (automation->use_songlengths != config->use_songlengths) {
        automation->use_songlengths = config->use_songlengths;
        if (!automation->use_songlengths) {
            c64_automation_clear_songlengths(automation);
        } else {
            c64_hvsc_songlength_db_clear(&automation->songlength_db);
        }
    } else if (strncmp(previous_songlengths, automation->config.songlengths_path, sizeof(previous_songlengths)) != 0) {
        c64_hvsc_songlength_db_clear(&automation->songlength_db);
    }
}

bool c64_automation_start(c64_automation_t *automation, int start_index)
{
    if (!automation) {
        return false;
    }

    pthread_mutex_lock(&automation->status_mutex);
    bool already_running = automation->running;
    pthread_mutex_unlock(&automation->status_mutex);
    if (already_running) {
        C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Already running");
        return false;
    }

    if (!automation->playlist_ready || !automation->files || automation->num_files == 0) {
        if (!c64_automation_build_playlist(automation)) {
            return false;
        }
    }

    // Start worker thread
    pthread_mutex_lock(&automation->status_mutex);
    automation->running = true;
    automation->should_stop = false;
    automation->skip_requested = false;
    pthread_mutex_unlock(&automation->status_mutex);
    if (start_index < 0 || start_index >= automation->num_files) {
        start_index = 0;
    }
    pthread_mutex_lock(&automation->status_mutex);
    automation->current_index = start_index;
    pthread_mutex_unlock(&automation->status_mutex);
    set_status(automation, "starting");

    if (pthread_create(&automation->worker_thread, NULL, automation_worker, automation) != 0) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to create worker thread");
        free(automation->files);
        automation->files = NULL;
        automation->files_capacity = 0;
        pthread_mutex_lock(&automation->status_mutex);
        automation->running = false;
        pthread_mutex_unlock(&automation->status_mutex);
        return false;
    }

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Started automation mode=%d, files=%d", automation->config.mode,
                 automation->num_files);
    return true;
}

void c64_automation_stop(c64_automation_t *automation)
{
    if (!automation) {
        return;
    }

    pthread_mutex_lock(&automation->status_mutex);
    bool is_running = automation->running;
    if (is_running) {
        automation->should_stop = true;
    }
    pthread_mutex_unlock(&automation->status_mutex);
    if (!is_running) {
        return;
    }

    // Signal worker to stop
    set_status(automation, "stopping");

    // Wait for worker thread to finish
    pthread_join(automation->worker_thread, NULL);

    pthread_mutex_lock(&automation->status_mutex);
    automation->running = false;
    automation->should_stop = false;
    pthread_mutex_unlock(&automation->status_mutex);

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Stopped automation");
}

bool c64_automation_is_running(c64_automation_t *automation)
{
    if (!automation) {
        return false;
    }
    pthread_mutex_lock(&automation->status_mutex);
    bool running = automation->running;
    pthread_mutex_unlock(&automation->status_mutex);
    return running;
}

const char *c64_automation_get_status(c64_automation_t *automation)
{
    if (!automation) {
        return "invalid";
    }

    pthread_mutex_lock(&automation->status_mutex);
    static char status_copy[128];
    strncpy(status_copy, automation->status, sizeof(status_copy) - 1);
    pthread_mutex_unlock(&automation->status_mutex);

    return status_copy;
}

static const char *c64_get_extension(const char *path)
{
    const char *dot = strrchr(path, '.');
    return (dot && dot != path) ? dot : "";
}

static const char *c64_volume_type_from_path(const char *path)
{
    const char *ext = c64_get_extension(path);
    if (strcasecmp(ext, ".d64") == 0) {
        return "d64";
    }
    if (strcasecmp(ext, ".g64") == 0) {
        return "g64";
    }
    if (strcasecmp(ext, ".d71") == 0) {
        return "d71";
    }
    if (strcasecmp(ext, ".g71") == 0) {
        return "g71";
    }
    if (strcasecmp(ext, ".d81") == 0) {
        return "d81";
    }
    return NULL;
}

bool c64_automation_play_song(c64_automation_t *automation, const char *path, int song_number, int song_length_seconds)
{
    if (!automation || !path) {
        return false;
    }

    const char *ext = c64_get_extension(path);
    size_t file_size;
    uint8_t *file_data = load_file(path, &file_size);
    if (!file_data) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to load song: %s", path);
        return false;
    }

    bool success = false;
    if (strcasecmp(ext, ".sid") == 0) {
        char md5_hex[33] = {0};
        uint8_t *songlengths_payload = NULL;
        size_t songlengths_size = 0;

        if (song_length_seconds > 0 && c64_hvsc_md5_file_hex(path, md5_hex)) {
            int subsongs = 1;
            if (file_size >= 0x10 && (memcmp(file_data, "PSID", 4) == 0 || memcmp(file_data, "RSID", 4) == 0)) {
                subsongs = (int)((file_data[0x0E] << 8) | file_data[0x0F]);
                if (subsongs < 1) {
                    subsongs = 1;
                }
            }

            int minutes = song_length_seconds / 60;
            int seconds = song_length_seconds % 60;
            if (minutes < 0) {
                minutes = 0;
            }
            if (seconds < 0) {
                seconds = 0;
            }

            size_t estimate = 32 + 1 + (size_t)subsongs * 6 + (size_t)(subsongs - 1) + 3;
            songlengths_payload = (uint8_t *)malloc(estimate);
            if (songlengths_payload) {
                size_t written =
                    (size_t)snprintf((char *)songlengths_payload, estimate, "%s=%d:%02d", md5_hex, minutes, seconds);
                for (int i = 1; i < subsongs && written + 1 < estimate; i++) {
                    written += (size_t)snprintf((char *)songlengths_payload + written, estimate - written, " %d:%02d",
                                                minutes, seconds);
                }
                if (written + 2 < estimate) {
                    songlengths_payload[written++] = '\r';
                    songlengths_payload[written++] = '\n';
                    songlengths_payload[written] = '\0';
                }
                songlengths_size = written;
            }
        }

        success = c64_rest_play_sid(automation->rest_client, file_data, file_size, song_number, songlengths_payload,
                                    songlengths_size);
        free(songlengths_payload);
    } else if (strcasecmp(ext, ".mod") == 0) {
        success = c64_rest_play_mod(automation->rest_client, file_data, file_size);
    } else {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Unsupported song file: %s", path);
    }
    free(file_data);

    if (success) {
        if (strcasecmp(ext, ".sid") == 0) {
            C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Playing SID: %s song=%d", path, song_number);
        } else {
            C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Playing MOD: %s", path);
        }
    }

    return success;
}

bool c64_automation_run_program(c64_automation_t *automation, const char *path)
{
    if (!automation || !path) {
        return false;
    }

    const char *ext = c64_get_extension(path);
    size_t file_size;
    uint8_t *file_data = load_file(path, &file_size);
    if (!file_data) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to load program: %s", path);
        return false;
    }

    bool success = false;
    if (strcasecmp(ext, ".prg") == 0) {
        success = c64_rest_run_prg(automation->rest_client, file_data, file_size);
    } else if (strcasecmp(ext, ".crt") == 0) {
        success = c64_rest_run_crt(automation->rest_client, file_data, file_size);
    } else {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Unsupported program file: %s", path);
    }
    free(file_data);

    if (success) {
        if (strcasecmp(ext, ".crt") == 0) {
            C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Running CRT: %s", path);
        } else {
            C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Running PRG: %s", path);
        }
    }

    return success;
}

bool c64_automation_run_disk(c64_automation_t *automation, const char *path)
{
    if (!automation || !path) {
        return false;
    }

    const char *disk_type = c64_volume_type_from_path(path);
    if (!disk_type) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Unsupported volume file: %s", path);
        return false;
    }

    // Reset machine first
    if (!c64_rest_reset(automation->rest_client)) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to reset machine");
        return false;
    }
    os_sleep_ms(RESET_DELAY_MS);

    // Load and mount disk
    size_t file_size;
    uint8_t *file_data = load_file(path, &file_size);
    if (!file_data) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to load volume: %s", path);
        return false;
    }

    bool success = c64_rest_mount_disk(automation->rest_client, 'a', disk_type, "readonly", file_data, file_size);
    free(file_data);

    if (!success) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to mount volume: %s", path);
        return false;
    }

    // Inject autostart command if keyboard available
    if (automation->keyboard) {
        const char *template = automation->config.d64_autostart_template;
        for (size_t i = 0; template[i]; i++) {
            c64_output_t output = {0};
            output.mode = C64_OUTPUT_PETSCII;
            if (template[i] == '\r') {
                output.data.petscii = 0x0D; // RETURN
            } else {
                output.data.petscii = (uint8_t)template[i];
            }
            c64_keyboard_queue_output(automation->keyboard, &output);
        }
    }

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Started volume: %s", path);
    return true;
}

const char *c64_automation_get_current_file(c64_automation_t *automation)
{
    if (!automation) {
        return NULL;
    }

    static char file_path_copy[512];
    pthread_mutex_lock(&automation->status_mutex);
    strncpy(file_path_copy, automation->current_file_path, sizeof(file_path_copy) - 1);
    pthread_mutex_unlock(&automation->status_mutex);

    return file_path_copy[0] ? file_path_copy : NULL;
}

int c64_automation_get_current_index(c64_automation_t *automation)
{
    if (!automation) {
        return -1;
    }

    pthread_mutex_lock(&automation->status_mutex);
    int index = (automation->num_files > 0) ? automation->current_index : -1;
    pthread_mutex_unlock(&automation->status_mutex);

    return index;
}

bool c64_automation_skip_next(c64_automation_t *automation)
{
    if (!automation) {
        return false;
    }

    pthread_mutex_lock(&automation->status_mutex);
    bool running = automation->running;
    if (running) {
        // Set skip flag to interrupt the wait loop
        automation->skip_requested = true;
    }
    pthread_mutex_unlock(&automation->status_mutex);

    if (!running) {
        return false;
    }

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Skip to next item requested");
    return true;
}

bool c64_automation_jump_to_index(c64_automation_t *automation, int target_index)
{
    if (!automation || target_index < 0) {
        return false;
    }

    pthread_mutex_lock(&automation->status_mutex);
    if (target_index >= automation->num_files) {
        pthread_mutex_unlock(&automation->status_mutex);
        return false;
    }

    automation->current_index = target_index;
    if (automation->running) {
        automation->skip_requested = true; // Skip current playback
    }
    pthread_mutex_unlock(&automation->status_mutex);

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Jumping to index %d", target_index);
    return true;
}
