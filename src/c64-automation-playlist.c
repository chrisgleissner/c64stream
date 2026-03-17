/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-automation-playlist.h"
#include "c64-automation-internal.h"
#include "c64-logging.h"
#include <obs.h>
#include <util/platform.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <sys/stat.h>
#define strcasecmp _stricmp
#else
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#ifndef DT_DIR
#define DT_DIR 4
#endif
#ifndef DT_REG
#define DT_REG 8
#endif
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUTOMATION_LOG_PREFIX "[c64-automation] "

static const char *get_extension(const char *path)
{
    const char *dot = strrchr(path, '.');
    return (dot && dot != path) ? dot : "";
}

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

static bool is_playable_path(const char *path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

    const char *last_sep = strrchr(path, '/');
    const char *last_backslash = strrchr(path, '\\');
    if (last_backslash && (!last_sep || last_backslash > last_sep)) {
        last_sep = last_backslash;
    }

    const char *filename = last_sep ? last_sep + 1 : path;
    if (!filename || filename[0] == '\0') {
        return false;
    }

    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return false;
    }

    return get_file_type(filename) >= 0;
}

static bool local_path_is_directory(const char *path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
#endif
}

static bool path_is_root_directory(const char *path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

#ifdef _WIN32
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/') && path[3] == '\0') {
        return true;
    }
    if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/') && path[2] == '\0') {
        return true;
    }
    return false;
#else
    return strcmp(path, "/") == 0;
#endif
}

static bool local_scan_path_is_safe(const char *path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

    if (path_is_root_directory(path)) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Refusing to scan filesystem root: %s", path);
        return false;
    }

    if (!local_path_is_directory(path)) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Refusing to scan non-directory path: %s", path);
        return false;
    }

    return true;
}

static uint32_t shuffle_next(uint32_t *seed)
{
    *seed = (*seed * 1664525u) + 1013904223u;
    return *seed;
}

static void shuffle_files(file_entry_t *files, int count)
{
    if (!files || count <= 1) {
        return;
    }

    uint32_t seed = (uint32_t)(os_gettime_ns() ^ (uintptr_t)files ^ (uintptr_t)count);
    for (int i = count - 1; i > 0; i--) {
        int j = (int)(shuffle_next(&seed) % (uint32_t)(i + 1));
        file_entry_t tmp = files[i];
        files[i] = files[j];
        files[j] = tmp;
    }
}

static int compare_file_entries_by_path(const void *left, const void *right)
{
    const file_entry_t *left_entry = (const file_entry_t *)left;
    const file_entry_t *right_entry = (const file_entry_t *)right;
    return strcasecmp(left_entry->path, right_entry->path);
}

static void sort_files(file_entry_t *files, int count)
{
    if (!files || count <= 1) {
        return;
    }

    qsort(files, (size_t)count, sizeof(*files), compare_file_entries_by_path);
}

static bool ensure_files_capacity(c64_automation_t *automation, int additional)
{
    if (!automation || additional <= 0) {
        return false;
    }

    if (automation->num_files + additional <= automation->files_capacity) {
        return true;
    }

    int new_capacity = automation->files_capacity > 0 ? automation->files_capacity
                                                      : C64_AUTOMATION_PLAYLIST_INITIAL_CAPACITY;
    while (new_capacity < automation->num_files + additional) {
        new_capacity *= 2;
        if (new_capacity > C64_AUTOMATION_PLAYLIST_MAX_FILES) {
            new_capacity = C64_AUTOMATION_PLAYLIST_MAX_FILES;
            break;
        }
    }

    if (new_capacity <= automation->files_capacity) {
        return false;
    }

    file_entry_t *new_files = realloc(automation->files, sizeof(file_entry_t) * (size_t)new_capacity);
    if (!new_files) {
        return false;
    }

    automation->files = new_files;
    automation->files_capacity = new_capacity;
    return true;
}

void c64_automation_clear_playlist_internal(c64_automation_t *automation)
{
    if (!automation) {
        return;
    }

    if (automation->files) {
        free(automation->files);
        automation->files = NULL;
    }
    automation->num_files = 0;
    automation->files_capacity = 0;
    automation->playlist_ready = false;
    automation->playlist_config_valid = false;
}

static bool playlist_config_equals(const c64_automation_config_t *left, const c64_automation_config_t *right)
{
    if (!left || !right) {
        return false;
    }

    if (left->mode != right->mode || left->file_source != right->file_source ||
        left->include_subfolders != right->include_subfolders || left->shuffle != right->shuffle ||
        left->duration_seconds != right->duration_seconds || left->reset_between_items != right->reset_between_items ||
        left->use_songlengths != right->use_songlengths) {
        return false;
    }

    if (strncmp(left->folder_path, right->folder_path, sizeof(left->folder_path)) != 0) {
        return false;
    }

    if (strncmp(left->songlengths_path, right->songlengths_path, sizeof(left->songlengths_path)) != 0) {
        return false;
    }

    return strncmp(left->d64_autostart_template, right->d64_autostart_template, sizeof(left->d64_autostart_template)) ==
           0;
}

static void playlist_config_copy(c64_automation_config_t *dest, const c64_automation_config_t *src)
{
    if (!dest || !src) {
        return;
    }

    memset(dest, 0, sizeof(*dest));
    dest->mode = src->mode;
    dest->file_source = src->file_source;
    dest->shuffle = src->shuffle;
    dest->include_subfolders = src->include_subfolders;
    dest->duration_seconds = src->duration_seconds;
    dest->reset_between_items = src->reset_between_items;
    dest->use_songlengths = src->use_songlengths;
    strncpy(dest->folder_path, src->folder_path, sizeof(dest->folder_path) - 1);
    strncpy(dest->songlengths_path, src->songlengths_path, sizeof(dest->songlengths_path) - 1);
    strncpy(dest->d64_autostart_template, src->d64_autostart_template, sizeof(dest->d64_autostart_template) - 1);
}

static int c64_automation_scan_c64u(c64_automation_t *automation)
{
    if (!automation || !automation->rest_client) {
        return 0;
    }

    c64_file_entry_t *entries = NULL;
    size_t entry_count = 0;
    bool success = c64_rest_list_files(automation->rest_client, automation->config.folder_path,
                                       automation->config.include_subfolders, &entries, &entry_count);
    if (!success) {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Failed to list C64U files: %s",
                      c64_rest_get_error(automation->rest_client));
        return 0;
    }

    int added = 0;
    for (size_t i = 0; i < entry_count && automation->num_files < C64_AUTOMATION_PLAYLIST_MAX_FILES; i++) {
        const c64_file_entry_t *entry = &entries[i];
        if (entry->is_directory) {
            continue;
        }
        if (!is_playable_path(entry->name)) {
            continue;
        }
        if (!ensure_files_capacity(automation, 1)) {
            break;
        }

        int written = snprintf(automation->files[automation->num_files].path,
                               sizeof(automation->files[automation->num_files].path), "%s/%s",
                               automation->config.folder_path, entry->name);
        if (written < 0 || (size_t)written >= sizeof(automation->files[automation->num_files].path)) {
            continue;
        }
        automation->files[automation->num_files].type = get_file_type(entry->name);
        automation->num_files++;
        added++;
    }

    free(entries);

    if (automation->num_files >= C64_AUTOMATION_PLAYLIST_MAX_FILES) {
        C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Playlist capped at %d files (C64U)", C64_AUTOMATION_PLAYLIST_MAX_FILES);
    }

    return added;
}

static int c64_automation_scan_local(c64_automation_t *automation)
{
    if (!automation) {
        return 0;
    }

    if (!local_scan_path_is_safe(automation->config.folder_path)) {
        return 0;
    }

    typedef struct {
        char path[C64_AUTOMATION_PATH_MAX];
    } dir_entry_t;

    int added = 0;
    int open_failures = 0;
    char first_failed_path[C64_AUTOMATION_PATH_MAX] = {0};
    int dir_count = 0;
    int dir_capacity = 64;
    dir_entry_t *dir_stack = calloc((size_t)dir_capacity, sizeof(*dir_stack));
    if (!dir_stack) {
        return 0;
    }

    strncpy(dir_stack[0].path, automation->config.folder_path, sizeof(dir_stack[0].path) - 1);
    dir_stack[0].path[sizeof(dir_stack[0].path) - 1] = '\0';
    dir_count = 1;

    while (dir_count > 0 && automation->num_files < C64_AUTOMATION_PLAYLIST_MAX_FILES) {
        dir_count--;
        char current_dir[C64_AUTOMATION_PATH_MAX];
        strncpy(current_dir, dir_stack[dir_count].path, sizeof(current_dir) - 1);
        current_dir[sizeof(current_dir) - 1] = '\0';

#ifdef _WIN32

        os_dir_t *dir = os_opendir(current_dir);
        if (!dir) {
            open_failures++;
            if (first_failed_path[0] == '\0') {
                strncpy(first_failed_path, current_dir, sizeof(first_failed_path) - 1);
                first_failed_path[sizeof(first_failed_path) - 1] = '\0';
            }
            continue;
        }

        struct os_dirent *entry = NULL;
        while ((entry = os_readdir(dir)) != NULL && automation->num_files < C64_AUTOMATION_PLAYLIST_MAX_FILES) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

#ifdef _WIN32
            const char separator = '\\';
#else
            const char separator = '/';
#endif

            char full_path[C64_AUTOMATION_PATH_MAX];
            int written = snprintf(full_path, sizeof(full_path), "%s%c%s", current_dir, separator, entry->d_name);
            if (written < 0 || (size_t)written >= sizeof(full_path)) {
                continue;
            }

            bool is_dir = entry->directory;
            bool is_file = !entry->directory;
            if (!is_dir && !is_file) {
#ifdef _WIN32
                DWORD attrs = GetFileAttributesA(full_path);
                if (attrs != INVALID_FILE_ATTRIBUTES) {
                    is_dir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    is_file = !is_dir;
                }
#else
                struct stat st;
                if (stat(full_path, &st) == 0) {
                    is_dir = S_ISDIR(st.st_mode);
                    is_file = S_ISREG(st.st_mode);
                }
#endif
            }

            if (is_dir) {
                if (!automation->config.include_subfolders) {
                    continue;
                }
                if (dir_count >= dir_capacity) {
                    int new_capacity = dir_capacity * 2;
                    void *new_stack = realloc(dir_stack, sizeof(*dir_stack) * (size_t)new_capacity);
                    if (!new_stack) {
                        break;
                    }
                    dir_stack = new_stack;
                    dir_capacity = new_capacity;
                }
                strncpy(dir_stack[dir_count].path, full_path, sizeof(dir_stack[dir_count].path) - 1);
                dir_stack[dir_count].path[sizeof(dir_stack[dir_count].path) - 1] = '\0';
                dir_count++;
                continue;
            }

            if (!is_file || !is_playable_path(full_path)) {
                continue;
            }

            if (!ensure_files_capacity(automation, 1)) {
                break;
            }

            strncpy(automation->files[automation->num_files].path, full_path,
                    sizeof(automation->files[automation->num_files].path) - 1);
            automation->files[automation->num_files].path[sizeof(automation->files[automation->num_files].path) - 1] =
                '\0';
            automation->files[automation->num_files].type = get_file_type(full_path);
            automation->num_files++;
            added++;
        }

        os_closedir(dir);
#else
        DIR *dir = opendir(current_dir);
        if (!dir) {
            open_failures++;
            if (first_failed_path[0] == '\0') {
                strncpy(first_failed_path, current_dir, sizeof(first_failed_path) - 1);
                first_failed_path[sizeof(first_failed_path) - 1] = '\0';
            }
            continue;
        }

        struct dirent *entry = NULL;
        while ((entry = readdir(dir)) != NULL && automation->num_files < C64_AUTOMATION_PLAYLIST_MAX_FILES) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char full_path[C64_AUTOMATION_PATH_MAX];
            int written = snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, entry->d_name);
            if (written < 0 || (size_t)written >= sizeof(full_path)) {
                continue;
            }

            bool is_dir = false;
            bool is_file = false;
            struct stat st;
            if (stat(full_path, &st) == 0) {
                is_dir = S_ISDIR(st.st_mode);
                is_file = S_ISREG(st.st_mode);
            }

            if (is_dir) {
                if (!automation->config.include_subfolders) {
                    continue;
                }
                if (dir_count >= dir_capacity) {
                    int new_capacity = dir_capacity * 2;
                    void *new_stack = realloc(dir_stack, sizeof(*dir_stack) * (size_t)new_capacity);
                    if (!new_stack) {
                        break;
                    }
                    dir_stack = new_stack;
                    dir_capacity = new_capacity;
                }
                strncpy(dir_stack[dir_count].path, full_path, sizeof(dir_stack[dir_count].path) - 1);
                dir_stack[dir_count].path[sizeof(dir_stack[dir_count].path) - 1] = '\0';
                dir_count++;
                continue;
            }

            if (!is_file || !is_playable_path(full_path)) {
                continue;
            }

            if (!ensure_files_capacity(automation, 1)) {
                break;
            }

            strncpy(automation->files[automation->num_files].path, full_path,
                    sizeof(automation->files[automation->num_files].path) - 1);
            automation->files[automation->num_files].path[sizeof(automation->files[automation->num_files].path) - 1] =
                '\0';
            automation->files[automation->num_files].type = get_file_type(full_path);
            automation->num_files++;
            added++;
        }

        closedir(dir);
#endif
    }

    free(dir_stack);

    if (open_failures > 0) {
        C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Skipped %d unreadable folders while scanning %s (first failure: %s)",
                        open_failures, automation->config.folder_path,
                        first_failed_path[0] ? first_failed_path : "(unknown)");
    }

    if (automation->num_files >= C64_AUTOMATION_PLAYLIST_MAX_FILES) {
        C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Playlist capped at %d files (local)", C64_AUTOMATION_PLAYLIST_MAX_FILES);
    }

    return added;
}

bool c64_automation_build_playlist(c64_automation_t *automation)
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
        automation->files_capacity = 1;

        strncpy(automation->files[0].path, automation->config.folder_path, sizeof(automation->files[0].path) - 1);
        automation->files[0].type = get_file_type(automation->config.folder_path);

        if (automation->files[0].type < 0) {
            C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Unsupported file type: %s", automation->config.folder_path);
            c64_automation_clear_playlist_internal(automation);
            return false;
        }

        automation->num_files = 1;
    } else if (automation->config.mode == C64_AUTO_MODE_FOLDER) {
        int added = 0;
        if (automation->config.file_source == C64_FILE_SOURCE_C64U) {
            added = c64_automation_scan_c64u(automation);
        } else {
            added = c64_automation_scan_local(automation);
        }
        if (added <= 0 || automation->num_files == 0) {
            C64_LOG_DEBUG(AUTOMATION_LOG_PREFIX "No supported files found in: %s", automation->config.folder_path);
            c64_automation_clear_playlist_internal(automation);
            return false;
        }

        if (automation->config.shuffle) {
            shuffle_files(automation->files, automation->num_files);
        } else {
            sort_files(automation->files, automation->num_files);
        }
    } else {
        C64_LOG_ERROR(AUTOMATION_LOG_PREFIX "Invalid mode: %d", automation->config.mode);
        c64_automation_clear_playlist_internal(automation);
        return false;
    }

    automation->playlist_ready = true;
    playlist_config_copy(&automation->playlist_config, &automation->config);
    automation->playlist_config_valid = true;
    return true;
}

void c64_automation_clear_playlist(c64_automation_t *automation)
{
    c64_automation_clear_playlist_internal(automation);
}

bool c64_automation_refresh_playlist(c64_automation_t *automation, const c64_automation_config_t *config,
                                     int selected_index, bool force_rebuild)
{
    if (!automation || !config) {
        return false;
    }

    pthread_mutex_lock(&automation->status_mutex);
    bool is_running = automation->running;
    pthread_mutex_unlock(&automation->status_mutex);
    if (is_running) {
        return false;
    }

    c64_automation_configure(automation, config);

    bool needs_rebuild = force_rebuild || !automation->playlist_ready || !automation->files ||
                         automation->num_files == 0 || !automation->playlist_config_valid ||
                         !playlist_config_equals(config, &automation->playlist_config) || config->shuffle;
    if (needs_rebuild) {
        if (!c64_automation_build_playlist(automation)) {
            return false;
        }
        playlist_config_copy(&automation->playlist_config, config);
        automation->playlist_config_valid = true;
    }

    if (selected_index < 0 || selected_index >= automation->num_files) {
        selected_index = 0;
    }

    pthread_mutex_lock(&automation->status_mutex);
    automation->current_index = selected_index;
    pthread_mutex_unlock(&automation->status_mutex);

    return true;
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
