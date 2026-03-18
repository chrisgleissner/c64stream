/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-version.h"
#include "c64-logging.h"
#include <stdio.h>
#include <time.h>

static char version_string[256] = {0};
static char build_info_string[512] = {0};

const char *c64_get_version_string(void)
{
    if (version_string[0] == '\0') {
        snprintf(version_string, sizeof(version_string), "C64 Stream Plugin %s (%s)", C64_VERSION_TAG, C64_GIT_HASH);
    }
    return version_string;
}

const char *c64_get_build_info(void)
{
    if (build_info_string[0] == '\0') {
        snprintf(build_info_string, sizeof(build_info_string), "%s (Git ID: %s, Built: %s UTC)", C64_VERSION_TAG,
                 C64_GIT_HASH, C64_BUILD_TIME);
    }
    return build_info_string;
}
