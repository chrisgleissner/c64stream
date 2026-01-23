/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-effect.h"
#include <obs-module.h>

const char *obs_module_text(const char *lookup)
{
    return lookup ? lookup : "";
}

obs_module_t *obs_current_module(void)
{
    return NULL;
}

void c64_effect_populate_list(obs_property_t *preset_prop)
{
    (void)preset_prop;
}

bool c64_effect_apply(obs_data_t *settings, const char *preset_name)
{
    (void)settings;
    (void)preset_name;
    return false;
}
