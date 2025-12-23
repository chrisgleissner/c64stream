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
#include "c64-network.h" // Include network header first to avoid Windows header conflicts
#include "c64-logging.h"
#include "c64-protocol.h"
#include "c64-source.h"
#include "c64-version.h"
#include "c64-presets.h"

// Logging control - define the global variable
bool c64_debug_logging = true;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en")

bool obs_module_load(void)
{
    C64_LOG_INFO("Loading %s", c64_get_version_string());
    C64_LOG_INFO("Build info: %s", c64_get_build_info());

    // Initialize the presets system
    if (!c64_presets_init()) {
        C64_LOG_WARNING("Failed to load CRT effect presets - continuing without presets");
    }

    struct obs_source_info c64_info = {.id = "c64_source",
                                       .type = OBS_SOURCE_TYPE_INPUT,
                                       .output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO |
                                                       OBS_SOURCE_CUSTOM_DRAW,
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
                                       .audio_render = NULL}; // Audio pushed via obs_source_output_audio

    obs_register_source(&c64_info);
    C64_LOG_INFO("C64 Stream plugin loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    C64_LOG_INFO("Unloading C64 Stream plugin");
    c64_presets_cleanup();
    c64_cleanup_networking();
}
