/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "../../src/c64-automation.h"
#include "../../src/c64-automation-playlist.h"
#include "c64script_test_stubs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                                       \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                  \
            return 1;                                                                                                     \
        }                                                                                                                 \
    } while (0)

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
static bool make_temp_dir(char *buffer, size_t size)
{
    char temp_path[MAX_PATH] = {0};
    DWORD path_len = GetTempPathA((DWORD)sizeof(temp_path), temp_path);
    if (path_len == 0 || path_len >= sizeof(temp_path)) {
        return false;
    }

    if (size < MAX_PATH) {
        return false;
    }

    if (GetTempFileNameA(temp_path, "c64", 0, buffer) == 0) {
        return false;
    }

    if (!DeleteFileA(buffer)) {
        return false;
    }

    return _mkdir(buffer) == 0;
}
#else
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static bool make_temp_dir(char *buffer, size_t size)
{
    if (snprintf(buffer, size, "/tmp/c64stream_test_XXXXXX") < 0) {
        return false;
    }

    return mkdtemp(buffer) != NULL;
}
#endif

static bool write_file(const char *path, const char *contents)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    fwrite(contents, 1, strlen(contents), f);
    fclose(f);
    return true;
}

static bool build_path(char *buffer, size_t size, const char *base, const char *suffix)
{
    if (!buffer || size == 0 || !base || !suffix) {
        return false;
    }

    int written = snprintf(buffer, size, "%s%s", base, suffix);
    return written >= 0 && (size_t)written < size;
}

static bool build_indexed_suffix(char *buffer, size_t size, const char *pattern, int value)
{
    if (!buffer || size == 0 || !pattern) {
        return false;
    }

    int written = snprintf(buffer, size, pattern, value);
    return written >= 0 && (size_t)written < size;
}

static bool make_dir(const char *path)
{
#ifdef _WIN32
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0700) == 0;
#endif
}

static void cleanup_file_range(const char *base_dir, int dir_count, int files_per_dir)
{
    for (int dir_index = 0; dir_index < dir_count; dir_index++) {
        char suffix[32];
        char subdir[C64_AUTOMATION_PATH_MAX];
        if (!build_indexed_suffix(suffix, sizeof(suffix), "/library_%03d", dir_index) ||
            !build_path(subdir, sizeof(subdir), base_dir, suffix)) {
            continue;
        }
        for (int file_index = 0; file_index < files_per_dir; file_index++) {
            char name[32];
            char path[C64_AUTOMATION_PATH_MAX];
            if (!build_indexed_suffix(name, sizeof(name), "/track_%03d.sid", file_index) ||
                !build_path(path, sizeof(path), subdir, name)) {
                continue;
            }
            remove(path);
        }
#ifdef _WIN32
        _rmdir(subdir);
#else
        rmdir(subdir);
#endif
    }
}

static void cleanup_dir(const char *dir, const char *file1, const char *file2)
{
    if (file1) {
        remove(file1);
    }
    if (file2) {
        remove(file2);
    }
#ifdef _WIN32
    _rmdir(dir);
#else
    rmdir(dir);
#endif
}

int main(void)
{
    char temp_dir[C64_AUTOMATION_PATH_MAX] = {0};
    CHECK(make_temp_dir(temp_dir, sizeof(temp_dir)));

    char file_sid[C64_AUTOMATION_PATH_MAX];
    char file_prg[C64_AUTOMATION_PATH_MAX];
    CHECK(build_path(file_sid, sizeof(file_sid), temp_dir, "/a.sid"));
    CHECK(build_path(file_prg, sizeof(file_prg), temp_dir, "/b.prg"));

    CHECK(write_file(file_sid, "SID"));
    CHECK(write_file(file_prg, "PRG"));

    c64_rest_client_t *rest_client = c64script_test_rest_create();
    c64_keyboard_t *keyboard = c64script_test_keyboard_create();
    CHECK(rest_client != NULL);

    c64_automation_t *automation = c64_automation_create(rest_client, keyboard, NULL);
    CHECK(automation != NULL);

    c64_automation_config_t config = {0};
    config.mode = C64_AUTO_MODE_FOLDER;
    config.file_source = C64_FILE_SOURCE_LOCAL;
    strncpy(config.folder_path, temp_dir, sizeof(config.folder_path) - 1);
    config.shuffle = false;
    config.include_subfolders = false;
    config.duration_seconds = 1;
    config.reset_between_items = false;
    strncpy(config.d64_autostart_template, "LOAD\"*\",8,1\rRUN\r", sizeof(config.d64_autostart_template) - 1);

    CHECK(c64_automation_refresh_playlist(automation, &config, 1));
    CHECK(c64_automation_get_playlist_count(automation) == 2);
    CHECK(c64_automation_get_current_index(automation) == 1);

    CHECK(c64_automation_jump_to_index(automation, 0));
    CHECK(c64_automation_get_current_index(automation) == 0);

    CHECK(c64_automation_refresh_playlist(automation, &config, 5));
    CHECK(c64_automation_get_current_index(automation) == 0);

    c64_automation_config_t root_config = config;
    root_config.include_subfolders = true;
#ifdef _WIN32
    snprintf(root_config.folder_path, sizeof(root_config.folder_path), "%c:\\", temp_dir[0]);
#else
    snprintf(root_config.folder_path, sizeof(root_config.folder_path), "/");
#endif
    CHECK(!c64_automation_refresh_playlist(automation, &root_config, 0));

    char hvsc_root[C64_AUTOMATION_PATH_MAX];
    char docs_dir[C64_AUTOMATION_PATH_MAX];
    char songlengths_path[C64_AUTOMATION_PATH_MAX];
    CHECK(build_path(hvsc_root, sizeof(hvsc_root), temp_dir, "/hvsc"));
    CHECK(build_path(docs_dir, sizeof(docs_dir), hvsc_root, "/DOCUMENTS"));
    CHECK(build_path(songlengths_path, sizeof(songlengths_path), docs_dir, "/Songlengths.txt"));
    CHECK(make_dir(hvsc_root));
    CHECK(make_dir(docs_dir));
    CHECK(write_file(songlengths_path, "0123456789abcdef0123456789abcdef=1:23\n"));

    c64_automation_config_t songlength_config = config;
    strncpy(songlength_config.folder_path, hvsc_root, sizeof(songlength_config.folder_path) - 1);
    songlength_config.include_subfolders = true;
    songlength_config.use_songlengths = true;
    songlength_config.songlengths_path[0] = '\0';
    c64_automation_configure(automation, &songlength_config);
    CHECK(strcmp(c64_automation_get_songlengths_path(automation), songlengths_path) == 0);

    const int dir_count = 100;
    const int files_per_dir = 101;
    for (int dir_index = 0; dir_index < dir_count; dir_index++) {
        char subdir[C64_AUTOMATION_PATH_MAX];
        char suffix[32];
        if (!build_indexed_suffix(suffix, sizeof(suffix), "/library_%03d", dir_index)) {
            return 1;
        }
        CHECK(build_path(subdir, sizeof(subdir), temp_dir, suffix));
        CHECK(make_dir(subdir));
        for (int file_index = 0; file_index < files_per_dir; file_index++) {
            char name[32];
            char path[C64_AUTOMATION_PATH_MAX];
            if (!build_indexed_suffix(name, sizeof(name), "/track_%03d.sid", file_index)) {
                return 1;
            }
            CHECK(build_path(path, sizeof(path), subdir, name));
            CHECK(write_file(path, "SID"));
        }
    }

    c64_automation_config_t large_config = config;
    large_config.include_subfolders = true;
    large_config.use_songlengths = false;
    CHECK(c64_automation_refresh_playlist(automation, &large_config, 0));
    int actual_playlist_count = c64_automation_get_playlist_count(automation);
    int expected_playlist_count = dir_count * files_per_dir + 2;
    if (actual_playlist_count < expected_playlist_count) {
        fprintf(stderr, "Expected playlist count at least %d, got %d\n", expected_playlist_count,
                actual_playlist_count);
        return 1;
    }

    c64_automation_destroy(automation);
    c64script_test_keyboard_destroy(keyboard);
    c64script_test_rest_destroy(rest_client);

    cleanup_file_range(temp_dir, dir_count, files_per_dir);
    remove(songlengths_path);
#ifdef _WIN32
    _rmdir(docs_dir);
    _rmdir(hvsc_root);
#else
    rmdir(docs_dir);
    rmdir(hvsc_root);
#endif

    cleanup_dir(temp_dir, file_sid, file_prg);
    return 0;
}
