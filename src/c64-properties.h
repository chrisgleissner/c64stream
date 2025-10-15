/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_PROPERTIES_H
#define C64_PROPERTIES_H

#include <obs-module.h>

/**
 * Create and populate OBS properties for the C64 Ultimate source
 * @param data Source data (unused, can be NULL)
 * @return Populated properties structure
 */
obs_properties_t *c64_create_properties(void *data);

/**
 * Set default values for all C64 Ultimate source properties
 * Loads values from properties.ini if available, otherwise uses hardcoded defaults
 * @param settings Settings object to populate with defaults
 */
void c64_set_property_defaults(obs_data_t *settings);

/**
 * Load configuration values from properties.ini file
 * @param settings Settings object to populate with loaded values
 * @return true if configuration file was loaded successfully, false otherwise
 */
bool c64_load_configuration(obs_data_t *settings);

#endif  // C64_PROPERTIES_H
