/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_AUTOMATION_HVSC_H
#define C64_AUTOMATION_HVSC_H

#include <stdbool.h>
#include <stddef.h>

#ifndef C64_AUTOMATION_PATH_MAX
#define C64_AUTOMATION_PATH_MAX 4096
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char md5[33];
    double seconds;
} c64_hvsc_songlength_entry_t;

typedef struct {
    bool loaded;
    char source_path[C64_AUTOMATION_PATH_MAX];
    c64_hvsc_songlength_entry_t *entries;
    size_t count;
} c64_hvsc_songlength_db_t;

void c64_hvsc_songlength_db_clear(c64_hvsc_songlength_db_t *db);
bool c64_hvsc_songlength_db_load(c64_hvsc_songlength_db_t *db, const char *path);
bool c64_hvsc_songlength_db_lookup(const c64_hvsc_songlength_db_t *db, const char *md5_hex, double *out_seconds);

bool c64_hvsc_find_songlengths_file_local(const char *root_path, char *out_path, size_t out_size);
bool c64_hvsc_md5_file_hex(const char *path, char out_hex[33]);

#ifdef __cplusplus
}
#endif

#endif // C64_AUTOMATION_HVSC_H
