/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-automation.h"
#include "c64-automation-playlist.h"
#include "c64script_test_stubs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/platform.h>

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

static void cleanup_file(const char *path)
{
    if (path && path[0] != '\0') {
        remove(path);
    }
}

static void cleanup_empty_dir(const char *path)
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

    CHECK(c64_automation_refresh_playlist(automation, &config, 1, false));
    CHECK(c64_automation_get_playlist_count(automation) == 2);
    CHECK(c64_automation_get_current_index(automation) == 1);

    CHECK(c64_automation_jump_to_index(automation, 0));
    CHECK(c64_automation_get_current_index(automation) == 0);

    CHECK(c64_automation_refresh_playlist(automation, &config, 5, false));
    CHECK(c64_automation_get_current_index(automation) == 0);
    CHECK(!c64_automation_jump_to_index(automation, 0));

    c64_automation_config_t root_config = config;
    root_config.include_subfolders = true;
#ifdef _WIN32
    snprintf(root_config.folder_path, sizeof(root_config.folder_path), "%c:\\", temp_dir[0]);
#else
    snprintf(root_config.folder_path, sizeof(root_config.folder_path), "/");
#endif
    CHECK(!c64_automation_refresh_playlist(automation, &root_config, 0, false));

    char nested_root[C64_AUTOMATION_PATH_MAX];
    char nested_level_one[C64_AUTOMATION_PATH_MAX];
    char nested_level_two[C64_AUTOMATION_PATH_MAX];
    char nested_song_one[C64_AUTOMATION_PATH_MAX];
    char nested_song_two[C64_AUTOMATION_PATH_MAX];
    CHECK(build_path(nested_root, sizeof(nested_root), temp_dir, "/nested_only_root"));
    CHECK(build_path(nested_level_one, sizeof(nested_level_one), nested_root, "/level1"));
    CHECK(build_path(nested_level_two, sizeof(nested_level_two), nested_level_one, "/level2"));
    CHECK(build_path(nested_song_one, sizeof(nested_song_one), nested_level_two, "/deep.sid"));
    CHECK(build_path(nested_song_two, sizeof(nested_song_two), nested_level_two, "/deeper.sid"));
    CHECK(make_dir(nested_root));
    CHECK(make_dir(nested_level_one));
    CHECK(make_dir(nested_level_two));
    CHECK(write_file(nested_song_one, "SID"));

    c64_automation_config_t nested_config = config;
    nested_config.include_subfolders = true;
    strncpy(nested_config.folder_path, nested_root, sizeof(nested_config.folder_path) - 1);
    CHECK(c64_automation_refresh_playlist(automation, &nested_config, 0, false));
    CHECK(c64_automation_get_playlist_count(automation) == 1);
    CHECK(c64_automation_get_playlist_item(automation, 0) != NULL);
    CHECK(strstr(c64_automation_get_playlist_item(automation, 0), "deep.sid") != NULL);

    CHECK(write_file(nested_song_two, "SID"));
    CHECK(c64_automation_refresh_playlist(automation, &nested_config, 0, false));
    CHECK(c64_automation_get_playlist_count(automation) == 1);
    CHECK(c64_automation_refresh_playlist(automation, &nested_config, 0, true));
    CHECK(c64_automation_get_playlist_count(automation) == 2);
    CHECK(strcmp(c64_automation_get_playlist_item(automation, 0), nested_song_one) == 0);
    CHECK(strcmp(c64_automation_get_playlist_item(automation, 1), nested_song_two) == 0);

#ifndef _WIN32
    char nested_cycle[C64_AUTOMATION_PATH_MAX];
    CHECK(build_path(nested_cycle, sizeof(nested_cycle), nested_level_two, "/back_to_root"));
    CHECK(symlink(nested_root, nested_cycle) == 0);
    CHECK(c64_automation_refresh_playlist(automation, &nested_config, 0, true));
    CHECK(c64_automation_get_playlist_count(automation) == 2);

    /* C64STR-015: mutual A<->B symlink cycle (two sibling directories each
     * linking to the other, not an ancestor self-cycle). The scan must
     * terminate with the single real file counted exactly once. */
    char mutual_root[C64_AUTOMATION_PATH_MAX];
    char mutual_a[C64_AUTOMATION_PATH_MAX];
    char mutual_b[C64_AUTOMATION_PATH_MAX];
    char mutual_song[C64_AUTOMATION_PATH_MAX];
    char mutual_a_to_b[C64_AUTOMATION_PATH_MAX];
    char mutual_b_to_a[C64_AUTOMATION_PATH_MAX];
    CHECK(build_path(mutual_root, sizeof(mutual_root), temp_dir, "/mutual"));
    CHECK(build_path(mutual_a, sizeof(mutual_a), mutual_root, "/A"));
    CHECK(build_path(mutual_b, sizeof(mutual_b), mutual_root, "/B"));
    CHECK(make_dir(mutual_root));
    CHECK(make_dir(mutual_a));
    CHECK(make_dir(mutual_b));
    CHECK(build_path(mutual_song, sizeof(mutual_song), mutual_a, "/only.sid"));
    CHECK(write_file(mutual_song, "SID"));
    CHECK(build_path(mutual_a_to_b, sizeof(mutual_a_to_b), mutual_a, "/to_b"));
    CHECK(build_path(mutual_b_to_a, sizeof(mutual_b_to_a), mutual_b, "/to_a"));
    CHECK(symlink(mutual_b, mutual_a_to_b) == 0);
    CHECK(symlink(mutual_a, mutual_b_to_a) == 0);
    c64_automation_config_t mutual_config = config;
    mutual_config.include_subfolders = true;
    strncpy(mutual_config.folder_path, mutual_root, sizeof(mutual_config.folder_path) - 1);
    CHECK(c64_automation_refresh_playlist(automation, &mutual_config, 0, true));
    CHECK(c64_automation_get_playlist_count(automation) == 1);
    /* The one real file may be recorded via A/only.sid or via the equivalent
     * B/to_a/only.sid depending on directory read order; both denote the same
     * file. The guarantee under a mutual cycle is that it is counted exactly
     * once, so assert on the basename rather than a read-order-dependent path. */
    CHECK(strstr(c64_automation_get_playlist_item(automation, 0), "only.sid") != NULL);
    remove(mutual_a_to_b);
    remove(mutual_b_to_a);
    remove(mutual_song);
    cleanup_empty_dir(mutual_a);
    cleanup_empty_dir(mutual_b);
    cleanup_empty_dir(mutual_root);
#endif

    /* C64STR-015: depth cap bounds a legitimately very deep tree. A chain deeper
     * than the scan depth cap must terminate promptly, include files within the
     * cap, and exclude files below it. */
    {
        char deep_root[C64_AUTOMATION_PATH_MAX];
        char deep_shallow[C64_AUTOMATION_PATH_MAX];
        CHECK(build_path(deep_root, sizeof(deep_root), temp_dir, "/deep"));
        CHECK(make_dir(deep_root));
        CHECK(build_path(deep_shallow, sizeof(deep_shallow), deep_root, "/shallow.sid"));
        CHECK(write_file(deep_shallow, "SID"));

        const int deep_levels = 72; /* > C64_AUTOMATION_LOCAL_SCAN_MAX_DEPTH (64) */
        char cur[C64_AUTOMATION_PATH_MAX];
        strncpy(cur, deep_root, sizeof(cur) - 1);
        cur[sizeof(cur) - 1] = '\0';
        for (int i = 0; i < deep_levels; i++) {
            char next[C64_AUTOMATION_PATH_MAX];
            CHECK(build_path(next, sizeof(next), cur, "/d"));
            CHECK(make_dir(next));
            strncpy(cur, next, sizeof(cur) - 1);
            cur[sizeof(cur) - 1] = '\0';
        }
        char deep_song[C64_AUTOMATION_PATH_MAX];
        CHECK(build_path(deep_song, sizeof(deep_song), cur, "/deep.sid"));
        CHECK(write_file(deep_song, "SID"));

        c64_automation_config_t deep_config = config;
        deep_config.include_subfolders = true;
        strncpy(deep_config.folder_path, deep_root, sizeof(deep_config.folder_path) - 1);
        CHECK(c64_automation_refresh_playlist(automation, &deep_config, 0, true));
        /* Only the file within the depth cap is included; the beyond-cap file is
         * excluded and the scan terminated (no hang). */
        CHECK(c64_automation_get_playlist_count(automation) == 1);
        CHECK(strcmp(c64_automation_get_playlist_item(automation, 0), deep_shallow) == 0);

        remove(deep_song);
        remove(deep_shallow);
        for (int i = 0; i < deep_levels; i++) {
            cleanup_empty_dir(cur);
            char *slash = strrchr(cur, '/');
            if (slash) {
                *slash = '\0';
            }
        }
        cleanup_empty_dir(deep_root);
    }

    char hvsc_root[C64_AUTOMATION_PATH_MAX];
    char c64music_root[C64_AUTOMATION_PATH_MAX];
    char demos_root[C64_AUTOMATION_PATH_MAX];
    char docs_dir[C64_AUTOMATION_PATH_MAX];
    char songlengths_path[C64_AUTOMATION_PATH_MAX];
    char hvsc_song[C64_AUTOMATION_PATH_MAX];
    CHECK(build_path(hvsc_root, sizeof(hvsc_root), temp_dir, "/hvsc"));
    CHECK(build_path(c64music_root, sizeof(c64music_root), hvsc_root, "/C64Music"));
    CHECK(build_path(demos_root, sizeof(demos_root), c64music_root, "/DEMOS"));
    CHECK(build_path(docs_dir, sizeof(docs_dir), hvsc_root, "/DOCUMENTS"));
    CHECK(build_path(songlengths_path, sizeof(songlengths_path), docs_dir, "/Songlengths.md5"));
    CHECK(build_path(hvsc_song, sizeof(hvsc_song), demos_root, "/demo.sid"));
    CHECK(make_dir(hvsc_root));
    CHECK(make_dir(c64music_root));
    CHECK(make_dir(demos_root));
    CHECK(make_dir(docs_dir));
    CHECK(write_file(songlengths_path, "0123456789abcdef0123456789abcdef=1:23\n"));
    CHECK(write_file(hvsc_song, "SID"));

    c64_automation_config_t songlength_config = config;
    strncpy(songlength_config.folder_path, hvsc_root, sizeof(songlength_config.folder_path) - 1);
    songlength_config.include_subfolders = true;
    songlength_config.use_songlengths = true;
    songlength_config.songlengths_path[0] = '\0';
    c64_automation_configure(automation, &songlength_config);
    CHECK(strcmp(c64_automation_get_songlengths_path(automation), songlengths_path) == 0);

    c64_automation_config_t nested_songlength_config = songlength_config;
    strncpy(nested_songlength_config.folder_path, c64music_root, sizeof(nested_songlength_config.folder_path) - 1);
    nested_songlength_config.songlengths_path[0] = '\0';
    c64_automation_configure(automation, &nested_songlength_config);
    CHECK(strcmp(c64_automation_get_songlengths_path(automation), songlengths_path) == 0);

    const int dir_count = 32;
    const int files_per_dir = 12;
    char large_root[C64_AUTOMATION_PATH_MAX];
    CHECK(build_path(large_root, sizeof(large_root), temp_dir, "/large_root"));
    CHECK(make_dir(large_root));
    for (int dir_index = 0; dir_index < dir_count; dir_index++) {
        char subdir[C64_AUTOMATION_PATH_MAX];
        char suffix[32];
        if (!build_indexed_suffix(suffix, sizeof(suffix), "/library_%03d", dir_index)) {
            return 1;
        }
        CHECK(build_path(subdir, sizeof(subdir), large_root, suffix));
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
    strncpy(large_config.folder_path, large_root, sizeof(large_config.folder_path) - 1);
    large_config.include_subfolders = true;
    large_config.use_songlengths = false;
    CHECK(c64_automation_refresh_playlist(automation, &large_config, 0, false));
    int actual_playlist_count = c64_automation_get_playlist_count(automation);
    int expected_playlist_count = dir_count * files_per_dir;
    if (actual_playlist_count < expected_playlist_count) {
        fprintf(stderr, "Expected playlist count at least %d, got %d\n", expected_playlist_count,
                actual_playlist_count);
        return 1;
    }

    c64_automation_t *preload_automation = c64_automation_create(rest_client, keyboard, NULL);
    CHECK(preload_automation != NULL);
    c64_automation_configure(preload_automation, &large_config);

    CHECK(c64_automation_preload_playlist_async(preload_automation, &large_config, 0));
    CHECK(c64_automation_preload_playlist_async(preload_automation, &large_config, 0));

    uint64_t preload_deadline = os_gettime_ns() + 30000000000ULL;
    while (c64_automation_is_preloading(preload_automation) && os_gettime_ns() < preload_deadline) {
        os_sleep_ms(10);
    }
    CHECK(!c64_automation_is_preloading(preload_automation));
    CHECK(c64_automation_get_playlist_count(preload_automation) == expected_playlist_count);

    CHECK(c64_automation_refresh_playlist(preload_automation, &large_config, 0, false));
    CHECK(c64_automation_get_playlist_count(preload_automation) == expected_playlist_count);

    c64_automation_destroy(automation);
    c64_automation_destroy(preload_automation);
    c64script_test_keyboard_destroy(keyboard);
    c64script_test_rest_destroy(rest_client);

    cleanup_file_range(large_root, dir_count, files_per_dir);
    cleanup_empty_dir(large_root);
    cleanup_file(hvsc_song);
    cleanup_file(songlengths_path);
    cleanup_file(nested_song_two);
    cleanup_file(nested_song_one);
#ifndef _WIN32
    cleanup_file(nested_cycle);
#endif
    cleanup_empty_dir(nested_level_two);
    cleanup_empty_dir(nested_level_one);
    cleanup_empty_dir(nested_root);
    cleanup_empty_dir(demos_root);
    cleanup_empty_dir(c64music_root);
    cleanup_empty_dir(docs_dir);
    cleanup_empty_dir(hvsc_root);

    cleanup_dir(temp_dir, file_sid, file_prg);
    return 0;
}
