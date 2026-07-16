/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#define FILE_LOG_PREFIX "💾 FILE:"

#include <obs-module.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Create directory recursively (equivalent to mkdir -p)
 * @param path Directory path to create
 * @return true if successful or directory exists, false on error
 */
bool c64_create_directory_recursive(const char *path);

/**
 * Get the current user's Documents folder path
 * @param path_buffer Buffer to store the path
 * @param buffer_size Size of the buffer
 * @return true if successful, false on error
 */
bool c64_get_user_documents_path(char *path_buffer, size_t buffer_size);

/**
 * Types of user data directories under ~/Documents/obs-studio/c64stream/
 */
typedef enum {
    C64_USER_DIR_ROOT,       // ~/Documents/obs-studio/c64stream/
    C64_USER_DIR_RECORDINGS, // ~/Documents/obs-studio/c64stream/recordings/
    C64_USER_DIR_PALETTES,   // ~/Documents/obs-studio/c64stream/palettes/
    C64_USER_DIR_SETTINGS,   // ~/Documents/obs-studio/c64stream/settings/
    C64_USER_DIR_PRESETS,    // ~/Documents/obs-studio/c64stream/presets/
    C64_USER_DIR_SCRIPTS     // ~/Documents/obs-studio/c64stream/scripts/
} c64_user_dir_type;

typedef enum {
    C64_PATH_KIND_MISSING = 0,
    C64_PATH_KIND_FILE,
    C64_PATH_KIND_DIRECTORY,
    C64_PATH_KIND_OTHER,
} c64_path_kind_t;

/**
 * Get a user data directory path with automatic creation
 * @param type Directory type to retrieve
 * @param path_buffer Buffer to store the path
 * @param buffer_size Size of the buffer
 * @return true if successful and directory exists/created, false on error
 */
bool c64_get_user_dir(c64_user_dir_type type, char *path_buffer, size_t buffer_size);

/**
 * Query the normalized kind of a local filesystem path.
 * Trailing separators are ignored for non-root paths.
 * On Windows this uses wide-character Win32 APIs to avoid UTF-8/ANSI mismatches.
 * @param path Input path in UTF-8
 * @param kind Output path kind
 * @return true on successful classification, false on invalid input or normalization failure
 */
bool c64_get_path_kind(const char *path, c64_path_kind_t *kind);

/** Visit key/value entries in a simple settings INI file.  Lines without an
 * equals sign and comment/blank lines are ignored. */
typedef bool (*c64_ini_entry_cb)(const char *key, const char *value, void *opaque);
bool c64_ini_foreach(const char *path, c64_ini_entry_cb callback, void *opaque);
