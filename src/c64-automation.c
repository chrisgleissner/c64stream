/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-automation.h"
#include "c64-file.h"
#include "c64-keyboard.h"
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
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define AUTOMATION_LOG_PREFIX "[c64-automation] "
#define MAX_FILES 100000
#define RESET_DELAY_MS 500

typedef struct {
    char path[512];
    c64_file_type_t type;
} file_entry_t;

static int enumerate_files(c64_rest_client_t *rest_client, const char *folder_path, c64_file_source_t file_source,
                           bool include_subfolders, file_entry_t **out_files);

struct c64_automation {
    c64_rest_client_t *rest_client;
    c64_keyboard_t *keyboard;
    obs_source_t *source;
    c64_automation_config_t config;
    bool running;
    bool should_stop;
    bool skip_requested; // Flag to skip to next item
    char status[128];
    char current_file_path[512]; // Full path of currently playing file
    bool playlist_ready;

    // File list
    file_entry_t *files;
    int num_files;
    int current_index;

    // Worker thread
    pthread_t worker_thread;
    pthread_mutex_t status_mutex;
};

// Helper: Get file extension
static const char *get_extension(const char *path)
{
    const char *dot = strrchr(path, '.');
    return (dot && dot != path) ? dot : "";
}

// Helper: Determine file type by extension
static c64_file_type_t get_file_type(const char *path)
{
    const char *ext = get_extension(path);
    if (strcasecmp(ext, ".sid") == 0) {
        return C64_FILE_TYPE_SID;
    } else if (strcasecmp(ext, ".mod") == 0) {
        return C64_FILE_TYPE_MOD;
    } else if (strcasecmp(ext, ".prg") == 0) {
        return C64_FILE_TYPE_PRG;
    } else if (strcasecmp(ext, ".crt") == 0) {
        return C64_FILE_TYPE_CRT;
    } else if (strcasecmp(ext, ".d64") == 0) {
        return C64_FILE_TYPE_D64;
    } else if (strcasecmp(ext, ".g64") == 0) {
        return C64_FILE_TYPE_G64;
    } else if (strcasecmp(ext, ".d71") == 0) {
        return C64_FILE_TYPE_D71;
    } else if (strcasecmp(ext, ".g71") == 0) {
        return C64_FILE_TYPE_G71;
    } else if (strcasecmp(ext, ".d81") == 0) {
        return C64_FILE_TYPE_D81;
    }
    return -1;
}

// Helper: Shuffle array using Fisher-Yates
static void shuffle_files(file_entry_t *files, int count)
{
    srand((unsigned int)time(NULL));
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        file_entry_t temp = files[i];
        files[i] = files[j];
        files[j] = temp;
    }
}

typedef struct {
    obs_source_t *source;
} c64_automation_ui_update_t;

static void c64_automation_ui_update_task(void *data)
{
    c64_automation_ui_update_t *update = (c64_automation_ui_update_t *)data;
    if (!update || !update->source) {
        if (update) {
            free(update);
        }
        return;
    }

    obs_source_update_properties(update->source);
    obs_source_release(update->source);
    free(update);
}

static void c64_automation_queue_ui_update(c64_automation_t *automation)
{
    if (!automation || !automation->source) {
        return;
    }

    c64_automation_ui_update_t *update = calloc(1, sizeof(c64_automation_ui_update_t));
    if (!update) {
        return;
    }

    update->source = obs_source_get_ref(automation->source);
    if (!update->source) {
        free(update);
        return;
    }
    obs_queue_task(OBS_TASK_UI, c64_automation_ui_update_task, update, false);
}

static void c64_automation_clear_playlist_internal(c64_automation_t *automation)
{
    if (!automation) {
        return;
    }

    if (automation->files) {
        free(automation->files);
        automation->files = NULL;
    }
    automation->num_files = 0;
    automation->playlist_ready = false;
}

static bool c64_automation_build_playlist(c64_automation_t *automation)
{
    if (!automation) {
        return false;
    }

    c64_automation_clear_playlist_internal(automation);

    if (automation->config.mode == C64_AUTO_MODE_SINGLE) {
        automation->files = calloc(1, sizeof(file_entry_t));
        if (!automation->files) {
            return false;
        }

        strncpy(automation->files[0].path, automation->config.folder_path, sizeof(automation->files[0].path) - 1);
        automation->files[0].type = get_file_type(automation->config.folder_path);

        if (automation->files[0].type < 0) {
            C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Unsupported file type: %s", automation->config.folder_path);
            c64_automation_clear_playlist_internal(automation);
            return false;
        }

        automation->num_files = 1;
    } else if (automation->config.mode == C64_AUTO_MODE_FOLDER) {
        automation->num_files = enumerate_files(automation->rest_client, automation->config.folder_path,
                                                automation->config.file_source, automation->config.include_subfolders,
                                                &automation->files);
        if (automation->num_files == 0) {
            C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "No supported files found in: %s", automation->config.folder_path);
            c64_automation_clear_playlist_internal(automation);
            return false;
        }

        if (automation->config.shuffle) {
            shuffle_files(automation->files, automation->num_files);
        }
    } else {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Invalid mode: %d", automation->config.mode);
        c64_automation_clear_playlist_internal(automation);
        return false;
    }

    automation->playlist_ready = true;
    return true;
}

// Helper: Set status string (thread-safe)
static void set_status(c64_automation_t *automation, const char *status)
{
    pthread_mutex_lock(&automation->status_mutex);
    strncpy(automation->status, status, sizeof(automation->status) - 1);
    automation->status[sizeof(automation->status) - 1] = '\0';
    pthread_mutex_unlock(&automation->status_mutex);
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

// Enumerate files in folder (with optional recursion)
static int enumerate_files(c64_rest_client_t *rest_client, const char *folder_path, c64_file_source_t file_source,
                           bool include_subfolders, file_entry_t **out_files)
{
    file_entry_t *files = calloc(MAX_FILES, sizeof(file_entry_t));
    if (!files) {
        return 0;
    }

    int count = 0;

    if (file_source == C64_FILE_SOURCE_C64U) {
        // C64U filesystem: use REST API to enumerate files
        if (!rest_client) {
            C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "C64U mode requires REST client");
            free(files);
            return 0;
        }

        c64_file_entry_t *c64u_entries = NULL;
        size_t c64u_count = 0;
        bool success = c64_rest_list_files(rest_client, folder_path, true, &c64u_entries, &c64u_count);
        if (!success) {
            C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to list C64U files: %s", c64_rest_get_error(rest_client));
            free(files);
            return 0;
        }

        for (size_t i = 0; i < c64u_count && count < MAX_FILES; i++) {
            if (c64u_entries[i].is_directory) {
                continue; // Skip directories
            }

            c64_file_type_t type = get_file_type(c64u_entries[i].name);
            if (type < 0) {
                continue; // Not a supported file
            }

            // For C64U files, store the full path within the C64U filesystem
            // The path should be: <folder_path>/<filename>
            int written =
                snprintf(files[count].path, sizeof(files[count].path), "%s/%s", folder_path, c64u_entries[i].name);
            if (written < 0 || (size_t)written >= sizeof(files[count].path)) {
                continue; // Truncated
            }
            files[count].type = type;
            count++;
        }

        free(c64u_entries);
        *out_files = files;
        return count;
    }

    // Local filesystem enumeration
#ifdef _WIN32
    // Windows implementation using FindFirstFile/FindNextFile
    // Use a stack-based approach for recursion to avoid deep call stacks
    typedef struct {
        char path[MAX_PATH];
    } dir_entry_t;

    dir_entry_t *dir_stack = NULL;
    int dir_count = 0;
    int dir_capacity = 0;

    // Add initial directory to stack
    dir_capacity = 64;
    dir_stack = calloc(dir_capacity, sizeof(dir_entry_t));
    if (!dir_stack) {
        free(files);
        return 0;
    }
    strncpy_s(dir_stack[0].path, sizeof(dir_stack[0].path), folder_path, _TRUNCATE);
    dir_count = 1;

    // Process directories from stack
    while (dir_count > 0 && count < MAX_FILES) {
        // Pop directory from stack
        dir_count--;
        const char *current_dir = dir_stack[dir_count].path;

        WIN32_FIND_DATAA find_data;
        char search_path[MAX_PATH];
        int written = snprintf(search_path, sizeof(search_path), "%s\\*", current_dir);
        if (written < 0 || (size_t)written >= sizeof(search_path)) {
            continue;
        }

        HANDLE h_find = FindFirstFileA(search_path, &find_data);
        if (h_find == INVALID_HANDLE_VALUE) {
            C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Failed to open folder: %s", current_dir);
            continue;
        }

        do {
            // Skip . and ..
            if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
                continue;
            }

            // Build full path
            char full_path[MAX_PATH];
            written = snprintf(full_path, sizeof(full_path), "%s\\%s", current_dir, find_data.cFileName);
            if (written < 0 || (size_t)written >= sizeof(full_path)) {
                continue; // Path too long
            }

            // Check if it's a directory
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // Add to stack if recursion is enabled
                if (include_subfolders) {
                    if (dir_count >= dir_capacity) {
                        // Grow stack
                        int new_capacity = dir_capacity * 2;
                        dir_entry_t *new_stack = realloc(dir_stack, new_capacity * sizeof(dir_entry_t));
                        if (!new_stack) {
                            break; // Out of memory, stop recursion
                        }
                        dir_stack = new_stack;
                        dir_capacity = new_capacity;
                    }
                    strncpy_s(dir_stack[dir_count].path, sizeof(dir_stack[dir_count].path), full_path, _TRUNCATE);
                    dir_count++;
                }
                continue;
            }

            // Process regular files
            c64_file_type_t type = get_file_type(find_data.cFileName);
            if (type < 0) {
                continue; // Not a supported file
            }

            // Store file path
            if (strlen(full_path) >= sizeof(files[count].path)) {
                C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Path too long, skipping: %s", full_path);
                continue;
            }

            strncpy(files[count].path, full_path, sizeof(files[count].path) - 1);
            files[count].type = type;
            count++;
        } while (FindNextFileA(h_find, &find_data) && count < MAX_FILES);

        FindClose(h_find);
    }

    free(dir_stack);

#else
    // Unix implementation using opendir/readdir
    // Use a stack-based approach for recursion to avoid deep call stacks
    typedef struct {
        char path[512];
    } dir_entry_t;

    dir_entry_t *dir_stack = NULL;
    int dir_count = 0;
    int dir_capacity = 0;

    // Add initial directory to stack
    dir_capacity = 64;
    dir_stack = calloc(dir_capacity, sizeof(dir_entry_t));
    if (!dir_stack) {
        free(files);
        return 0;
    }
    strncpy(dir_stack[0].path, folder_path, sizeof(dir_stack[0].path) - 1);
    dir_count = 1;

    // Process directories from stack
    while (dir_count > 0 && count < MAX_FILES) {
        // Pop directory from stack
        dir_count--;
        const char *current_dir = dir_stack[dir_count].path;

        DIR *dir = opendir(current_dir);
        if (!dir) {
            C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Failed to open folder: %s", current_dir);
            continue;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && count < MAX_FILES) {
            // Skip . and ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            // Build full path
            char full_path[512];
            int written = snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, entry->d_name);
            if (written < 0 || (size_t)written >= sizeof(full_path)) {
                continue; // Path too long
            }

            // Check if it's a directory
            if (entry->d_type == DT_DIR) {
                // Add to stack if recursion is enabled
                if (include_subfolders) {
                    if (dir_count >= dir_capacity) {
                        // Grow stack
                        int new_capacity = dir_capacity * 2;
                        dir_entry_t *new_stack = realloc(dir_stack, new_capacity * sizeof(dir_entry_t));
                        if (!new_stack) {
                            break; // Out of memory, stop recursion
                        }
                        dir_stack = new_stack;
                        dir_capacity = new_capacity;
                    }
                    strncpy(dir_stack[dir_count].path, full_path, sizeof(dir_stack[dir_count].path) - 1);
                    dir_count++;
                }
                continue;
            }

            // Process regular files
            if (entry->d_type != DT_REG) {
                continue;
            }

            c64_file_type_t type = get_file_type(entry->d_name);
            if (type < 0) {
                continue; // Not a supported file
            }

            // Store file path
            if (strlen(full_path) >= sizeof(files[count].path)) {
                C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Path too long, skipping: %s", full_path);
                continue;
            }

            strncpy(files[count].path, full_path, sizeof(files[count].path) - 1);
            files[count].type = type;
            count++;
        }

        closedir(dir);
    }

    free(dir_stack);
#endif

    *out_files = files;
    return count;
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

    while (i < automation->num_files && !automation->should_stop) {
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
                if (automation->config.reset_between_items) {
                    c64_rest_reset(automation->rest_client);
                    os_sleep_ms(RESET_DELAY_MS);
                }
                success = c64_rest_mount_disk_path(automation->rest_client, 'a', file->path);
                if (success && automation->keyboard) {
                    // Inject LOAD"*",8,1:RUN followed by RETURN
                    const char *template = automation->config.d64_autostart_template;
                    for (size_t j = 0; template[j] && !automation->should_stop; j++) {
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
                success = c64_rest_play_sid(automation->rest_client, file_data, file_size, 0);
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
                if (automation->config.reset_between_items) {
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
                    for (size_t j = 0; template[j] && !automation->should_stop; j++) {
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
        int duration_ms = automation->config.duration_seconds * 1000;
        int elapsed_ms = 0;
        int sleep_interval_ms = 100;

        while (elapsed_ms < duration_ms && !automation->should_stop) {
            os_sleep_ms(sleep_interval_ms);
            elapsed_ms += sleep_interval_ms;

            // Check skip flag
            pthread_mutex_lock(&automation->status_mutex);
            bool skip = automation->skip_requested;
            if (skip) {
                automation->skip_requested = false;
            }
            int jump_target = automation->current_index;
            pthread_mutex_unlock(&automation->status_mutex);

            if (skip) {
                C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Skipping current item");
                if (jump_target >= 0 && jump_target < automation->num_files && jump_target != i) {
                    i = jump_target;
                } else {
                    i++;
                }
                goto advance_item;
            }
        }

        // Reset between items if configured (skip if disk type since already reset before mount)
        if (automation->config.reset_between_items && file->type != C64_FILE_TYPE_D64 &&
            file->type != C64_FILE_TYPE_G64 && file->type != C64_FILE_TYPE_D71 && file->type != C64_FILE_TYPE_G71 &&
            file->type != C64_FILE_TYPE_D81) {
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

    set_status(automation, automation->should_stop ? "stopped" : "completed");
    automation->running = false;

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
    automation->current_index = 0;
    automation->playlist_ready = false;
    strncpy(automation->status, "idle", sizeof(automation->status) - 1);

    pthread_mutex_init(&automation->status_mutex, NULL);

    // Set defaults
    automation->config.mode = C64_AUTO_MODE_OFF;
    automation->config.duration_seconds = 120;
    automation->config.reset_between_items = true;
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
    if (automation->running) {
        c64_automation_stop(automation);
    }

    c64_automation_clear_playlist_internal(automation);

    pthread_mutex_destroy(&automation->status_mutex);
    free(automation);
}

void c64_automation_configure(c64_automation_t *automation, const c64_automation_config_t *config)
{
    if (!automation || !config) {
        return;
    }

    memcpy(&automation->config, config, sizeof(c64_automation_config_t));
    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Configured: mode=%d folder=%s shuffle=%d recursive=%d duration=%d",
                 config->mode, config->folder_path, config->shuffle, config->include_subfolders,
                 config->duration_seconds);
}

void c64_automation_clear_playlist(c64_automation_t *automation)
{
    c64_automation_clear_playlist_internal(automation);
}

bool c64_automation_refresh_playlist(c64_automation_t *automation, const c64_automation_config_t *config,
                                     int selected_index)
{
    if (!automation || !config) {
        return false;
    }

    if (automation->running) {
        return false;
    }

    c64_automation_configure(automation, config);

    if (!c64_automation_build_playlist(automation)) {
        return false;
    }

    if (selected_index < 0 || selected_index >= automation->num_files) {
        selected_index = 0;
    }

    pthread_mutex_lock(&automation->status_mutex);
    automation->current_index = selected_index;
    pthread_mutex_unlock(&automation->status_mutex);

    return true;
}

bool c64_automation_start(c64_automation_t *automation, int start_index)
{
    if (!automation) {
        return false;
    }

    if (automation->running) {
        C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Already running");
        return false;
    }

    if (!automation->playlist_ready || !automation->files || automation->num_files == 0) {
        if (!c64_automation_build_playlist(automation)) {
            return false;
        }
    }

    // Start worker thread
    automation->running = true;
    automation->should_stop = false;
    automation->skip_requested = false;
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
        automation->running = false;
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

    if (!automation->running) {
        return;
    }

    // Signal worker to stop
    automation->should_stop = true;
    set_status(automation, "stopping");

    // Wait for worker thread to finish
    pthread_join(automation->worker_thread, NULL);

    automation->running = false;
    automation->should_stop = false;

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Stopped automation");
}

bool c64_automation_is_running(c64_automation_t *automation)
{
    if (!automation) {
        return false;
    }
    return automation->running;
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

bool c64_automation_play_sid(c64_automation_t *automation, const char *path, int song_number)
{
    if (!automation || !path) {
        return false;
    }

    size_t file_size;
    uint8_t *file_data = load_file(path, &file_size);
    if (!file_data) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to load SID: %s", path);
        return false;
    }

    bool success = c64_rest_play_sid(automation->rest_client, file_data, file_size, song_number);
    free(file_data);

    if (success) {
        C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Playing SID: %s song=%d", path, song_number);
    }

    return success;
}

bool c64_automation_run_prg(c64_automation_t *automation, const char *path)
{
    if (!automation || !path) {
        return false;
    }

    size_t file_size;
    uint8_t *file_data = load_file(path, &file_size);
    if (!file_data) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to load PRG: %s", path);
        return false;
    }

    bool success = c64_rest_run_prg(automation->rest_client, file_data, file_size);
    free(file_data);

    if (success) {
        C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Running PRG: %s", path);
    }

    return success;
}

bool c64_automation_start_d64(c64_automation_t *automation, const char *path)
{
    if (!automation || !path) {
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
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to load D64: %s", path);
        return false;
    }

    bool success = c64_rest_mount_disk(automation->rest_client, 'a', "d64", "readonly", file_data, file_size);
    free(file_data);

    if (!success) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to mount D64: %s", path);
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

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Started D64: %s", path);
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

int c64_automation_get_playlist_count(c64_automation_t *automation)
{
    if (!automation) {
        return 0;
    }

    pthread_mutex_lock(&automation->status_mutex);
    int count = automation->num_files;
    pthread_mutex_unlock(&automation->status_mutex);

    return count;
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

const char *c64_automation_get_playlist_item(c64_automation_t *automation, int index)
{
    if (!automation || index < 0) {
        return NULL;
    }

    pthread_mutex_lock(&automation->status_mutex);
    const char *path = NULL;
    if (index < automation->num_files && automation->files) {
        path = automation->files[index].path;
    }
    pthread_mutex_unlock(&automation->status_mutex);

    return path;
}

bool c64_automation_skip_next(c64_automation_t *automation)
{
    if (!automation || !automation->running) {
        return false;
    }

    pthread_mutex_lock(&automation->status_mutex);
    // Set skip flag to interrupt the wait loop
    automation->skip_requested = true;
    pthread_mutex_unlock(&automation->status_mutex);

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
