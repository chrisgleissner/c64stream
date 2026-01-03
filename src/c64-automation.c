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
#define MAX_FILES 1000
#define RESET_DELAY_MS 500

typedef struct {
    char path[512];
    c64_file_type_t type;
} file_entry_t;

struct c64_automation {
    c64_rest_client_t *rest_client;
    c64_keyboard_t *keyboard;
    c64_automation_config_t config;
    bool running;
    bool should_stop;
    char status[128];

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
    } else if (strcasecmp(ext, ".prg") == 0) {
        return C64_FILE_TYPE_PRG;
    } else if (strcasecmp(ext, ".d64") == 0) {
        return C64_FILE_TYPE_D64;
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

// Enumerate files in folder
static int enumerate_files(c64_rest_client_t *rest_client, const char *folder_path, c64_file_source_t file_source,
                           file_entry_t **out_files)
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
    WIN32_FIND_DATAA find_data;
    char search_path[MAX_PATH];
    int written = snprintf(search_path, sizeof(search_path), "%s\\*", folder_path);
    if (written < 0 || (size_t)written >= sizeof(search_path)) {
        free(files);
        return 0;
    }

    HANDLE h_find = FindFirstFileA(search_path, &find_data);
    if (h_find == INVALID_HANDLE_VALUE) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to open folder: %s", folder_path);
        free(files);
        return 0;
    }

    do {
        // Skip directories
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        c64_file_type_t type = get_file_type(find_data.cFileName);
        if (type < 0) {
            continue; // Not a supported file
        }

        // Check length to prevent overflow
        size_t needed = strlen(folder_path) + 1 + strlen(find_data.cFileName) + 1;
        if (needed > sizeof(files[count].path)) {
            C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Path too long, skipping: %s\\%s", folder_path, find_data.cFileName);
            continue;
        }

        written = snprintf(files[count].path, sizeof(files[count].path), "%s\\%s", folder_path, find_data.cFileName);
        if (written < 0 || (size_t)written >= sizeof(files[count].path)) {
            continue; // Truncated
        }
        files[count].type = type;
        count++;
    } while (FindNextFileA(h_find, &find_data) && count < MAX_FILES);

    FindClose(h_find);

#else
    // Unix implementation using opendir/readdir
    DIR *dir = opendir(folder_path);
    if (!dir) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to open folder: %s", folder_path);
        free(files);
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < MAX_FILES) {
        if (entry->d_type != DT_REG) {
            continue;
        }

        c64_file_type_t type = get_file_type(entry->d_name);
        if (type < 0) {
            continue; // Not a supported file
        }

        // Check length to prevent overflow
        size_t needed = strlen(folder_path) + 1 + strlen(entry->d_name) + 1; // path + / + name + \0
        if (needed > sizeof(files[count].path)) {
            C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Path too long, skipping: %s/%s", folder_path, entry->d_name);
            continue;
        }

        int written = snprintf(files[count].path, sizeof(files[count].path), "%s/%s", folder_path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(files[count].path)) {
            continue; // Truncated
        }
        files[count].type = type;
        count++;
    }

    closedir(dir);
#endif

    *out_files = files;
    return count;
}

// Worker thread function
static void *automation_worker(void *arg)
{
    c64_automation_t *automation = (c64_automation_t *)arg;

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Worker thread started");

    for (int i = 0; i < automation->num_files && !automation->should_stop; i++) {
        automation->current_index = i;
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

        C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Processing: %s", file->path);

        // Execute based on file source
        bool success = false;
        if (automation->config.file_source == C64_FILE_SOURCE_C64U) {
            // C64U filesystem: use path-based REST API
            switch (file->type) {
            case C64_FILE_TYPE_SID:
                success = c64_rest_play_sid_path(automation->rest_client, file->path, 0);
                break;
            case C64_FILE_TYPE_PRG:
                success = c64_rest_run_prg_path(automation->rest_client, file->path);
                break;
            case C64_FILE_TYPE_D64:
                // Reset before mounting
                if (automation->config.reset_between_items) {
                    c64_rest_reset(automation->rest_client);
                    os_sleep_ms(RESET_DELAY_MS);
                }
                success = c64_rest_mount_disk_path(automation->rest_client, 'a', file->path);
                if (success && automation->keyboard) {
                    // Inject autostart command
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
            case C64_FILE_TYPE_PRG:
                success = c64_rest_run_prg(automation->rest_client, file_data, file_size);
                break;
            case C64_FILE_TYPE_D64:
                // Reset, mount, inject autostart
                if (automation->config.reset_between_items) {
                    c64_rest_reset(automation->rest_client);
                    os_sleep_ms(RESET_DELAY_MS);
                }
                success = c64_rest_mount_disk(automation->rest_client, 'a', "d64", "readonly", file_data, file_size);
                if (success && automation->keyboard) {
                    // Inject autostart command
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

        // Wait for duration (check should_stop periodically)
        int duration_ms = automation->config.duration_seconds * 1000;
        int elapsed_ms = 0;
        int sleep_interval_ms = 100;

        while (elapsed_ms < duration_ms && !automation->should_stop) {
            os_sleep_ms(sleep_interval_ms);
            elapsed_ms += sleep_interval_ms;
        }

        // Reset between items if configured
        if (automation->config.reset_between_items && file->type != C64_FILE_TYPE_D64) {
            c64_rest_reset(automation->rest_client);
            os_sleep_ms(RESET_DELAY_MS);
        }
    }

    set_status(automation, automation->should_stop ? "stopped" : "completed");
    automation->running = false;

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Worker thread finished");
    return NULL;
}

c64_automation_t *c64_automation_create(void *rest_client, void *keyboard)
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
    automation->running = false;
    automation->should_stop = false;
    automation->files = NULL;
    automation->num_files = 0;
    automation->current_index = 0;
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

    // Free file list
    if (automation->files) {
        free(automation->files);
    }

    pthread_mutex_destroy(&automation->status_mutex);
    free(automation);
}

void c64_automation_configure(c64_automation_t *automation, const c64_automation_config_t *config)
{
    if (!automation || !config) {
        return;
    }

    memcpy(&automation->config, config, sizeof(c64_automation_config_t));
    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Configured: mode=%d folder=%s shuffle=%d duration=%d", config->mode,
                 config->folder_path, config->shuffle, config->duration_seconds);
}

bool c64_automation_start(c64_automation_t *automation)
{
    if (!automation) {
        return false;
    }

    if (automation->running) {
        C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Already running");
        return false;
    }

    // Handle single file vs folder mode
    if (automation->config.mode == C64_AUTO_MODE_SINGLE) {
        // Single file mode - create single-entry list
        automation->files = calloc(1, sizeof(file_entry_t));
        if (!automation->files) {
            return false;
        }

        strncpy(automation->files[0].path, automation->config.folder_path, sizeof(automation->files[0].path) - 1);
        automation->files[0].type = get_file_type(automation->config.folder_path);

        if (automation->files[0].type < 0) {
            C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Unsupported file type: %s", automation->config.folder_path);
            free(automation->files);
            automation->files = NULL;
            return false;
        }

        automation->num_files = 1;
    } else if (automation->config.mode == C64_AUTO_MODE_FOLDER) {
        // Folder mode - enumerate files
        automation->num_files = enumerate_files(automation->rest_client, automation->config.folder_path,
                                                automation->config.file_source, &automation->files);
        if (automation->num_files == 0) {
            C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "No supported files found in: %s", automation->config.folder_path);
            return false;
        }

        // Shuffle if configured
        if (automation->config.shuffle) {
            shuffle_files(automation->files, automation->num_files);
        }
    } else {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Invalid mode: %d", automation->config.mode);
        return false;
    }

    // Start worker thread
    automation->running = true;
    automation->should_stop = false;
    automation->current_index = 0;
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

    // Cleanup
    if (automation->files) {
        free(automation->files);
        automation->files = NULL;
    }
    automation->num_files = 0;
    automation->running = false;

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
