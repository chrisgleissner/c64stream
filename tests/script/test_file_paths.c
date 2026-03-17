#include "../../src/c64-file.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <stdlib.h>
#include <unistd.h>
#endif

#define CHECK(expr)                                                                                                       \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                  \
            return 1;                                                                                                     \
        }                                                                                                                 \
    } while (0)

static bool make_temp_dir(char *buffer, size_t size)
{
#ifdef _WIN32
    char temp_path[MAX_PATH] = {0};
    DWORD path_len = GetTempPathA((DWORD)sizeof(temp_path), temp_path);
    if (path_len == 0 || path_len >= sizeof(temp_path) || size < MAX_PATH) {
        return false;
    }

    if (GetTempFileNameA(temp_path, "c64", 0, buffer) == 0) {
        return false;
    }

    if (!DeleteFileA(buffer)) {
        return false;
    }

    return _mkdir(buffer) == 0;
#else
    if (snprintf(buffer, size, "/tmp/c64stream_paths_XXXXXX") < 0) {
        return false;
    }

    return mkdtemp(buffer) != NULL;
#endif
}

static void cleanup_dir(const char *path)
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

int main(void)
{
    char temp_dir[1024] = {0};
    CHECK(make_temp_dir(temp_dir, sizeof(temp_dir)));

    c64_path_kind_t kind = C64_PATH_KIND_OTHER;
    CHECK(c64_get_path_kind(temp_dir, &kind));
    CHECK(kind == C64_PATH_KIND_DIRECTORY);

    char with_trailing_separator[1024] = {0};
    CHECK(snprintf(with_trailing_separator, sizeof(with_trailing_separator), "%s%c", temp_dir,
#ifdef _WIN32
                   '\\'
#else
                   '/'
#endif
                   ) > 0);
    CHECK(c64_get_path_kind(with_trailing_separator, &kind));
    CHECK(kind == C64_PATH_KIND_DIRECTORY);

    char missing_dir[1024] = {0};
    CHECK(snprintf(missing_dir, sizeof(missing_dir), "%s%cdoes_not_exist%c", temp_dir,
#ifdef _WIN32
                   '\\', '\\'
#else
                   '/', '/'
#endif
                   ) > 0);
    CHECK(c64_get_path_kind(missing_dir, &kind));
    CHECK(kind == C64_PATH_KIND_MISSING);

    cleanup_dir(temp_dir);
    return 0;
}