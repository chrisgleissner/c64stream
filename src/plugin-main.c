/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include "plugin-support.h"
#include "c64-network.h"
#include "c64-logging.h"
#include "c64-protocol.h"
#include "c64-source.h"
#include "c64-version.h"
#include "c64-effect.h"
#include "c64-palette.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#endif

#include <string.h>
#include <errno.h>

// Logging control - define the global variable
// Default to quiet; users can enable debug logging via the properties checkbox.
bool c64_debug_logging = false;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// Copy demo scripts from plugin data folder to user's scripts folder
static void copy_demo_scripts(void)
{
    // Get source directory (plugin data/scripts)
    char *source_dir = obs_module_file("scripts");
    if (!source_dir) {
        C64_LOG_WARNING("Failed to get plugin scripts directory");
        return;
    }

    // Get destination directory (user documents)
    char dest_dir[2048];
    const char *home = getenv("HOME");
    if (!home) {
        home = getenv("USERPROFILE"); // Windows fallback
    }
    if (!home) {
        C64_LOG_WARNING("Could not determine home directory for script copying");
        bfree(source_dir);
        return;
    }

#ifdef _WIN32
    snprintf(dest_dir, sizeof(dest_dir), "%s\\Documents\\obs-studio\\c64stream\\scripts", home);
#else
    snprintf(dest_dir, sizeof(dest_dir), "%s/Documents/obs-studio/c64stream/scripts", home);
#endif

    // Create destination directory if it doesn't exist
#ifdef _WIN32
    char mkdir_cmd[2560];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir \"%s\"", dest_dir);
    if (system(mkdir_cmd) != 0) {
        C64_LOG_WARNING("Failed to create scripts directory: %s", dest_dir);
    }
#else
    char mkdir_cmd[2560];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\"", dest_dir);
    if (system(mkdir_cmd) != 0) {
        C64_LOG_WARNING("Failed to create scripts directory: %s", dest_dir);
    }
#endif

    // Copy all .c64script files
    C64_LOG_INFO("Copying demo scripts from %s to %s", source_dir, dest_dir);

    int copied_count = 0;
    int found_count = 0;

#ifdef _WIN32
    // Windows directory enumeration using FindFirstFile/FindNextFile
    char search_path[2560];
    snprintf(search_path, sizeof(search_path), "%s\\*.c64script", source_dir);

    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                found_count++;
                char src_path[2560], dst_path[2560];
                snprintf(src_path, sizeof(src_path), "%s\\%s", source_dir, find_data.cFileName);
                snprintf(dst_path, sizeof(dst_path), "%s\\%s", dest_dir, find_data.cFileName);

                // Check if destination file exists and is newer
                WIN32_FILE_ATTRIBUTE_DATA src_attr, dst_attr;
                bool should_copy = true;
                if (GetFileAttributesExA(dst_path, GetFileExInfoStandard, &dst_attr) &&
                    GetFileAttributesExA(src_path, GetFileExInfoStandard, &src_attr)) {
                    // Compare file times (only copy if source is newer)
                    if (CompareFileTime(&src_attr.ftLastWriteTime, &dst_attr.ftLastWriteTime) <= 0) {
                        should_copy = false;
                    }
                }

                if (should_copy) {
                    // Copy file
                    FILE *src_file = fopen(src_path, "rb");
                    FILE *dst_file = fopen(dst_path, "wb");
                    if (src_file && dst_file) {
                        char buffer[4096];
                        size_t bytes;
                        while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
                            fwrite(buffer, 1, bytes, dst_file);
                        }
                        copied_count++;
                    } else {
                        C64_LOG_WARNING("Failed to open files for copying: %s -> %s (src=%p, dst=%p)", src_path,
                                        dst_path, (void *)src_file, (void *)dst_file);
                    }
                    if (src_file)
                        fclose(src_file);
                    if (dst_file)
                        fclose(dst_file);
                }
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    } else {
        C64_LOG_WARNING("Failed to open source scripts directory: %s", source_dir);
        bfree(source_dir);
        return;
    }
#else
    // POSIX directory enumeration using opendir/readdir
    DIR *dir = opendir(source_dir);
    if (!dir) {
        C64_LOG_WARNING("Failed to open source scripts directory: %s", source_dir);
        bfree(source_dir);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && strcmp(ext, ".c64script") == 0) {
            found_count++;
            char src_path[2560], dst_path[2560];
            snprintf(src_path, sizeof(src_path), "%s/%s", source_dir, entry->d_name);
            snprintf(dst_path, sizeof(dst_path), "%s/%s", dest_dir, entry->d_name);

            // Check if destination file exists and is newer
            struct stat src_stat, dst_stat;
            bool should_copy = true;
            if (stat(dst_path, &dst_stat) == 0 && stat(src_path, &src_stat) == 0) {
                // Only copy if source is newer
                if (src_stat.st_mtime <= dst_stat.st_mtime) {
                    should_copy = false;
                }
            }

            if (should_copy) {
                // Copy file
                FILE *src_file = fopen(src_path, "rb");
                FILE *dst_file = fopen(dst_path, "wb");
                if (src_file && dst_file) {
                    char buffer[4096];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
                        fwrite(buffer, 1, bytes, dst_file);
                    }
                    copied_count++;
                } else {
                    C64_LOG_WARNING("Failed to open files for copying: %s -> %s (src=%p, dst=%p)", src_path, dst_path,
                                    (void *)src_file, (void *)dst_file);
                }
                if (src_file)
                    fclose(src_file);
                if (dst_file)
                    fclose(dst_file);
            }
        }
    }
    closedir(dir);
#endif

    C64_LOG_INFO("Found %d script files, copied %d to user directory", found_count, copied_count);
    bfree(source_dir);

    if (copied_count > 0) {
        C64_LOG_INFO("Copied %d demo script(s) to %s", copied_count, dest_dir);
    }
}

bool obs_module_load(void)
{
    C64_LOG_INFO("Loading %s", c64_get_version_string());
    C64_LOG_INFO("Build info: %s", c64_get_build_info());

    // Initialize the presets system
    if (!c64_effect_init()) {
        C64_LOG_WARNING("" EFFECT_LOG_PREFIX " Failed to load CRT effect presets - continuing without presets");
    }

    // Initialize the palette system
    if (!c64_palette_init()) {
        C64_LOG_WARNING("Failed to initialize palette system - using default palette");
    }

    // Copy demo scripts to user's documents folder
    copy_demo_scripts();

    struct obs_source_info c64_info = {.id = "c64_source",
                                       .type = OBS_SOURCE_TYPE_INPUT,
                                       .output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO |
                                                       OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION,
                                       .get_name = c64_get_name,
                                       .create = c64_create,
                                       .destroy = c64_destroy,
                                       .update = c64_update,
                                       .get_defaults = c64_defaults,
                                       .get_properties = c64_properties,
                                       .video_render = c64_video_render,
                                       .video_tick = c64_video_tick,
                                       .get_width = c64_get_width,
                                       .get_height = c64_get_height,
                                       .mouse_click = c64_mouse_click,
                                       .mouse_move = c64_mouse_move,
                                       .mouse_wheel = c64_mouse_wheel,
                                       .focus = c64_focus,
                                       .key_click = c64_key_click,
                                       .audio_render = NULL}; // Audio pushed via obs_source_output_audio

    obs_register_source(&c64_info);
    C64_LOG_INFO("C64 Stream plugin loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    C64_LOG_INFO("Unloading C64 Stream plugin");
    c64_palette_cleanup();
    c64_effect_cleanup();
    c64_cleanup_networking();
}
