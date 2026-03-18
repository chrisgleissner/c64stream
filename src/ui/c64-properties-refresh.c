/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-properties-refresh.h"
#include "c64-types.h"

void c64_properties_mark_script_ui_refresh(struct c64_source *context)
{
    if (!context) {
        return;
    }

    context->force_ui_update = true;
    context->playlist_refresh_suppressed_once = true;
}

bool c64_properties_should_request_playlist_rebuild(struct c64_source *context)
{
    if (!context) {
        return true;
    }

    if (context->playlist_refresh_suppressed_once) {
        context->playlist_refresh_suppressed_once = false;
        return false;
    }

    return true;
}
