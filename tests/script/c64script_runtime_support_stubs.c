/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-file.h"
#include "c64-source.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

bool c64_source_script_wait_rendered_frames(struct c64_source *context, uint32_t frame_count, char *error_msg,
                                            size_t error_size)
{
    (void)context;
    (void)frame_count;
    if (error_msg && error_size > 0) {
        error_msg[0] = '\0';
    }
    return true;
}

bool c64_source_script_take_frontend_screenshot(struct c64_source *context, bool preview, const char *output_path,
                                                char *error_msg, size_t error_size)
{
    (void)context;
    (void)preview;
    (void)output_path;
    if (error_msg && error_size > 0) {
        snprintf(error_msg, error_size, "Screenshot capture not available in rest-network tests");
    }
    return false;
}

bool c64_create_directory_recursive(const char *path)
{
    (void)path;
    return true;
}

bool c64_get_user_dir(c64_user_dir_type type, char *path_buffer, size_t buffer_size)
{
    (void)type;
    (void)path_buffer;
    (void)buffer_size;
    return false;
}

bool c64_ini_foreach(const char *path, c64_ini_entry_cb callback, void *opaque)
{
    (void)path;
    (void)callback;
    (void)opaque;
    return false;
}
