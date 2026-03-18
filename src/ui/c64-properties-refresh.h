/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include <stdbool.h>

struct c64_source;

void c64_properties_mark_script_ui_refresh(struct c64_source *context);
bool c64_properties_should_request_playlist_rebuild(struct c64_source *context);
