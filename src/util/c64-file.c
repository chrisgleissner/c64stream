/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-file.h"
#include "c64-logging.h"

#include <sys/stat.h>
#include <util/platform.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif
#endif

static bool c64_is_path_separator(char ch)
{
    return ch == '/' || ch == '\\';
}

#ifdef _WIN32
static bool c64_is_drive_root_prefix(const char *path, size_t length)
{
    return length == 3 && isalpha((unsigned char)path[0]) && path[1] == ':' && c64_is_path_separator(path[2]);
}

static size_t c64_unc_root_length(const char *path)
{
    if (!path || !c64_is_path_separator(path[0]) || !c64_is_path_separator(path[1])) {
        return 0;
    }

    size_t index = 2;
    while (path[index] != '\0' && !c64_is_path_separator(path[index])) {
        index++;
    }
    if (index == 2 || !c64_is_path_separator(path[index])) {
        return 0;
    }

    index++;
    const size_t share_start = index;
    while (path[index] != '\0' && !c64_is_path_separator(path[index])) {
        index++;
    }
    if (index == share_start) {
        return 0;
    }

    return index;
}
#endif

static bool c64_is_root_path_prefix(const char *path, size_t length)
{
    if (!path || length == 0) {
        return false;
    }

#ifdef _WIN32
    if (c64_is_drive_root_prefix(path, length)) {
        return true;
    }

    const size_t unc_root_length = c64_unc_root_length(path);
    if (unc_root_length > 0 && length == unc_root_length) {
        return true;
    }

    return length == 2 && c64_is_path_separator(path[0]) && c64_is_path_separator(path[1]);
#else
    return length == 1 && path[0] == '/';
#endif
}

static bool c64_normalize_path_for_query(const char *path, char *buffer, size_t buffer_size)
{
    if (!path || !buffer || buffer_size == 0) {
        return false;
    }

    size_t length = strnlen(path, buffer_size);
    if (length == 0 || length >= buffer_size) {
        return false;
    }

    memcpy(buffer, path, length);
    buffer[length] = '\0';

    while (length > 0 && c64_is_path_separator(buffer[length - 1]) && !c64_is_root_path_prefix(buffer, length)) {
        buffer[length - 1] = '\0';
        length--;
    }

    return length > 0;
}

/**
 * Create directory recursively (equivalent to mkdir -p)
 * @param path Directory path to create
 * @return true if successful or directory exists, false on error
 */
bool c64_create_directory_recursive(const char *path)
{
    char tmp[1024];
    char *p = NULL;
    size_t len;

    if (!c64_normalize_path_for_query(path, tmp, sizeof(tmp))) {
        return false;
    }

    len = strlen(tmp);

    // Start from the beginning, but skip drive letters on Windows (e.g., "C:")
    p = tmp;
    if (len > 1 && tmp[1] == ':') {
        p = tmp + 2; // Skip "C:" part on Windows
    }
    if (*p == '/' || *p == '\\') {
        p++; // Skip leading slash
    }

    // Create each directory in the path
    for (; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = 0;
            if (os_mkdir(tmp) != 0) {
                // Check if it already exists (ignore error if it does)
                struct stat st;
                if (os_stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    // Directory creation failed and it doesn't exist
                    return false;
                }
            }
            *p = '/'; // Use forward slash consistently (works on Windows too)
        }
    }

    // Create the final directory
    if (os_mkdir(tmp) != 0) {
        struct stat st;
        if (os_stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
            return false;
        }
    }

    return true;
}

bool c64_get_path_kind(const char *path, c64_path_kind_t *kind)
{
    if (!path || !kind) {
        return false;
    }

    char normalized[1024];
    if (!c64_normalize_path_for_query(path, normalized, sizeof(normalized))) {
        return false;
    }

#ifdef _WIN32
    int wide_length = MultiByteToWideChar(CP_UTF8, 0, normalized, -1, NULL, 0);
    if (wide_length <= 0) {
        return false;
    }

    WCHAR *normalized_wide = calloc((size_t)wide_length, sizeof(WCHAR));
    if (!normalized_wide) {
        return false;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, normalized, -1, normalized_wide, wide_length) <= 0) {
        free(normalized_wide);
        return false;
    }

    DWORD attributes = GetFileAttributesW(normalized_wide);
    free(normalized_wide);

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME) {
            *kind = C64_PATH_KIND_MISSING;
        } else {
            *kind = C64_PATH_KIND_OTHER;
        }
        return true;
    }

    *kind = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? C64_PATH_KIND_DIRECTORY : C64_PATH_KIND_FILE;
    return true;
#else
    struct stat st;
    if (stat(normalized, &st) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            *kind = C64_PATH_KIND_MISSING;
        } else {
            *kind = C64_PATH_KIND_OTHER;
        }
        return true;
    }

    if (S_ISDIR(st.st_mode)) {
        *kind = C64_PATH_KIND_DIRECTORY;
    } else if (S_ISREG(st.st_mode)) {
        *kind = C64_PATH_KIND_FILE;
    } else {
        *kind = C64_PATH_KIND_OTHER;
    }
    return true;
#endif
}

/**
 * Get the current user's Documents folder path
 * @param path_buffer Buffer to store the path
 * @param buffer_size Size of the buffer
 * @return true if successful, false on error
 */
bool c64_get_user_documents_path(char *path_buffer, size_t buffer_size)
{
    if (!path_buffer || buffer_size < 32) {
        return false;
    }

#ifdef _WIN32
    // Windows: Use SHGetFolderPath to get the current user's Documents folder
    WCHAR documents_path_w[MAX_PATH];
    char documents_path[MAX_PATH];

    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, documents_path_w);
    if (SUCCEEDED(hr)) {
        // Convert wide string to multi-byte string
        int result =
            WideCharToMultiByte(CP_UTF8, 0, documents_path_w, -1, documents_path, sizeof(documents_path), NULL, NULL);
        if (result > 0) {
            strncpy(path_buffer, documents_path, buffer_size - 1);
            path_buffer[buffer_size - 1] = '\0';
            C64_LOG_DEBUG("" FILE_LOG_PREFIX " Retrieved Windows Documents path: %s", path_buffer);
            return true;
        } else {
            C64_LOG_WARNING("" FILE_LOG_PREFIX " Failed to convert Windows Documents path to UTF-8");
        }
    } else {
        C64_LOG_WARNING("" FILE_LOG_PREFIX " Failed to get Windows Documents folder path (HRESULT: 0x%08X)", hr);
    }

    // Fallback to Public Documents if personal Documents fails
    strcpy(path_buffer, "C:\\Users\\Public\\Documents");
    C64_LOG_INFO("" FILE_LOG_PREFIX " Using fallback Windows Documents path: %s", path_buffer);
    return true;

#elif defined(__APPLE__)
    // macOS: Use NSDocumentDirectory
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path_buffer, buffer_size, "%s/Documents", home);
        C64_LOG_DEBUG("" FILE_LOG_PREFIX " Retrieved macOS Documents path: %s", path_buffer);
        return true;
    } else {
        C64_LOG_WARNING("" FILE_LOG_PREFIX " Failed to get macOS home directory");
        strcpy(path_buffer, "/Users/Shared/Documents");
        C64_LOG_INFO("" FILE_LOG_PREFIX " Using fallback macOS Documents path: %s", path_buffer);
        return true;
    }

#else
    // Linux/Unix: Use XDG_DOCUMENTS_DIR or fallback to ~/Documents
    const char *xdg_documents = getenv("XDG_DOCUMENTS_DIR");
    if (xdg_documents) {
        strncpy(path_buffer, xdg_documents, buffer_size - 1);
        path_buffer[buffer_size - 1] = '\0';
        C64_LOG_DEBUG("" FILE_LOG_PREFIX " Retrieved Linux XDG Documents path: %s", path_buffer);
        return true;
    }

    const char *home = getenv("HOME");
    if (home) {
        snprintf(path_buffer, buffer_size, "%s/Documents", home);
        C64_LOG_DEBUG("" FILE_LOG_PREFIX " Retrieved Linux Documents path: %s", path_buffer);
        return true;
    } else {
        C64_LOG_WARNING("" FILE_LOG_PREFIX " Failed to get Linux home directory");
        strcpy(path_buffer, "/tmp");
        C64_LOG_INFO("" FILE_LOG_PREFIX " Using fallback Linux Documents path: %s", path_buffer);
        return true;
    }
#endif
}

bool c64_get_user_dir(c64_user_dir_type type, char *path_buffer, size_t buffer_size)
{
    if (!path_buffer || buffer_size < 64) {
        return false;
    }

    // Get base Documents folder
    char documents[256];
    if (!c64_get_user_documents_path(documents, sizeof(documents))) {
        C64_LOG_WARNING("" FILE_LOG_PREFIX " Failed to get user documents path");
        return false;
    }

    // Construct path based on type
    const char *subdir = NULL;
    switch (type) {
    case C64_USER_DIR_ROOT:
        subdir = NULL; // Just obs-studio/c64stream
        break;
    case C64_USER_DIR_RECORDINGS:
        subdir = "recordings";
        break;
    case C64_USER_DIR_PALETTES:
        subdir = "palettes";
        break;
    case C64_USER_DIR_SETTINGS:
        subdir = "settings";
        break;
    case C64_USER_DIR_PRESETS:
        subdir = "presets";
        break;
    case C64_USER_DIR_SCRIPTS:
        subdir = "scripts";
        break;
    default:
        C64_LOG_WARNING("" FILE_LOG_PREFIX " Invalid user directory type: %d", type);
        return false;
    }

    // Build path with platform-appropriate separators
#ifdef _WIN32
    if (subdir) {
        snprintf(path_buffer, buffer_size, "%s\\obs-studio\\c64stream\\%s", documents, subdir);
    } else {
        snprintf(path_buffer, buffer_size, "%s\\obs-studio\\c64stream", documents);
    }
#else
    if (subdir) {
        snprintf(path_buffer, buffer_size, "%s/obs-studio/c64stream/%s", documents, subdir);
    } else {
        snprintf(path_buffer, buffer_size, "%s/obs-studio/c64stream", documents);
    }
#endif

    // Create directory if it doesn't exist
    if (!c64_create_directory_recursive(path_buffer)) {
        C64_LOG_WARNING("" FILE_LOG_PREFIX " Failed to create user directory: %s", path_buffer);
        return false;
    }

    return true;
}

bool c64_ini_foreach(const char *path, c64_ini_entry_cb callback, void *opaque)
{
    if (!path || !callback) {
        return false;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    char line[1024];
    bool ok = true;
    while (fgets(line, sizeof(line), file)) {
        char *key = line;
        while (isspace((unsigned char)*key))
            key++;
        if (*key == '\0' || *key == '#' || *key == ';')
            continue;
        char *equals = strchr(key, '=');
        if (!equals)
            continue;
        *equals++ = '\0';
        char *key_end = key + strlen(key);
        while (key_end > key && isspace((unsigned char)key_end[-1]))
            *--key_end = '\0';
        char *value_end = equals + strlen(equals);
        while (value_end > equals && isspace((unsigned char)value_end[-1]))
            *--value_end = '\0';
        if (!callback(key, equals, opaque)) {
            ok = false;
            break;
        }
    }
    fclose(file);
    return ok;
}
