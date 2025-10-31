/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_PRESETS_H
#define C64_PRESETS_H

#include <obs-module.h>
#include <stdbool.h>

/**
 * Initialize the presets system by loading presets from the data file
 * @return true if presets were loaded successfully, false otherwise
 */
bool c64_presets_init(void);

/**
 * Clean up the presets system and free all allocated memory
 */
void c64_presets_cleanup(void);

/**
 * Populate a dropdown property with all available presets
 * @param preset_prop The OBS property to populate
 */
void c64_presets_populate_list(obs_property_t *preset_prop);

/**
 * Apply a preset by name to the given settings
 * @param settings The settings object to update
 * @param preset_name The name of the preset to apply
 * @return true if the preset was found and applied, false otherwise
 */
bool c64_presets_apply(obs_data_t *settings, const char *preset_name);

/**
 * Get the count of available presets
 * @return Number of loaded presets
 */
int c64_presets_get_count(void);

#endif // C64_PRESETS_H
