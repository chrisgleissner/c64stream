/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "../../src/c64-automation.h"
#include "c64script_test_stubs.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
static bool make_temp_dir(char *buffer, size_t size)
{
    char temp_path[MAX_PATH] = {0};
    if (GetTempPathA((DWORD)sizeof(temp_path), temp_path) == 0) {
        return false;
    }

    if (snprintf(buffer, size, "%sc64stream_test_XXXXXX", temp_path) < 0) {
        return false;
    }

    if (!_mktemp(buffer)) {
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
    char temp_dir[256] = {0};
    assert(make_temp_dir(temp_dir, sizeof(temp_dir)));

    char file_sid[512];
    char file_prg[512];
#ifdef _WIN32
    snprintf(file_sid, sizeof(file_sid), "%s\\a.sid", temp_dir);
    snprintf(file_prg, sizeof(file_prg), "%s\\b.prg", temp_dir);
#else
    snprintf(file_sid, sizeof(file_sid), "%s/a.sid", temp_dir);
    snprintf(file_prg, sizeof(file_prg), "%s/b.prg", temp_dir);
#endif

    assert(write_file(file_sid, "SID"));
    assert(write_file(file_prg, "PRG"));

    c64_rest_client_t *rest_client = c64script_test_rest_create();
    c64_keyboard_t *keyboard = c64script_test_keyboard_create();
    assert(rest_client);

    c64_automation_t *automation = c64_automation_create(rest_client, keyboard, NULL);
    assert(automation);

    c64_automation_config_t config = {0};
    config.mode = C64_AUTO_MODE_FOLDER;
    config.file_source = C64_FILE_SOURCE_LOCAL;
    strncpy(config.folder_path, temp_dir, sizeof(config.folder_path) - 1);
    config.shuffle = false;
    config.include_subfolders = false;
    config.duration_seconds = 1;
    config.reset_between_items = false;
    strncpy(config.d64_autostart_template, "LOAD\"*\",8,1\rRUN\r", sizeof(config.d64_autostart_template) - 1);

    assert(c64_automation_refresh_playlist(automation, &config, 1));
    assert(c64_automation_get_playlist_count(automation) == 2);
    assert(c64_automation_get_current_index(automation) == 1);

    assert(c64_automation_jump_to_index(automation, 0));
    assert(c64_automation_get_current_index(automation) == 0);

    assert(c64_automation_refresh_playlist(automation, &config, 5));
    assert(c64_automation_get_current_index(automation) == 0);

    c64_automation_destroy(automation);
    c64script_test_keyboard_destroy(keyboard);
    c64script_test_rest_destroy(rest_client);

    cleanup_dir(temp_dir, file_sid, file_prg);
    return 0;
}
