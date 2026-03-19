/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_EFFECT_H
#define C64_EFFECT_H

#include <obs-module.h>
#include <stdbool.h>

// Logging prefix for effect operations
#define EFFECT_LOG_PREFIX "✨ EFFECT:"

/**
 * Initialize the presets system by loading presets from the data file
 * @return true if presets were loaded successfully, false otherwise
 */
bool c64_effect_init(void);

/**
 * Clean up the presets system and free all allocated memory
 */
void c64_effect_cleanup(void);

/**
 * Populate a dropdown property with all available presets
 * @param preset_prop The OBS property to populate
 */
void c64_effect_populate_list(obs_property_t *preset_prop);

/**
 * Apply a preset by name to the given settings
 * @param settings The settings object to update
 * @param preset_name The name of the preset to apply
 * @return true if the preset was found and applied, false otherwise
 */
bool c64_effect_apply(obs_data_t *settings, const char *preset_name);

/**
 * Check whether the current settings already match a preset's visual parameters.
 * @param settings The settings object to inspect
 * @param preset_name The name of the preset to compare against
 * @return true if all preset-controlled effect parameters already match
 */
bool c64_effect_matches_preset(obs_data_t *settings, const char *preset_name);

/**
 * Get the count of available presets
 * @return Number of loaded presets
 */
int c64_effect_get_count(void);

#endif // C64_EFFECT_H
