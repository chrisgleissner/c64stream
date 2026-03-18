/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-effect.h"
#include "c64-logging.h"
#include <obs-module.h>
#include <util/dstr.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_PRESETS 50
#define MAX_PRESET_NAME_LEN 64
#define MAX_KEY_LEN 64
#define MAX_VALUE_LEN 64
#define MAX_SETTINGS_PER_PRESET 20

// Structure to hold a single setting within a preset
struct preset_setting {
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
};

// Structure to hold a complete preset
struct preset {
    char name[MAX_PRESET_NAME_LEN];
    struct preset_setting settings[MAX_SETTINGS_PER_PRESET];
    int setting_count;
};

// Global storage for presets
static struct preset presets[MAX_PRESETS];
static int preset_count = 0;

// Helper function to trim whitespace from both ends of a string
static void trim_string(char *str)
{
    if (!str || !*str)
        return;

    // Trim leading whitespace
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n'))
        start++;

    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        end--;

    // Copy trimmed string back
    size_t len = (end - start) + 1;
    memmove(str, start, len);
    str[len] = '\0';
}

// Parse an INI file and load presets
static bool parse_presets_file(const char *filepath)
{
    FILE *file = fopen(filepath, "r");
    if (!file) {
        C64_LOG_WARNING("" EFFECT_LOG_PREFIX " Failed to open presets file: %s", filepath);
        return false;
    }

    char line[256];
    int current_preset = -1;

    while (fgets(line, sizeof(line), file)) {
        trim_string(line);

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#')
            continue;

        // Check for section header [PresetName]
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end && preset_count < MAX_PRESETS) {
                *end = '\0';
                current_preset = preset_count++;
                strncpy(presets[current_preset].name, line + 1, MAX_PRESET_NAME_LEN - 1);
                presets[current_preset].name[MAX_PRESET_NAME_LEN - 1] = '\0';
                presets[current_preset].setting_count = 0;
                C64_LOG_INFO("" EFFECT_LOG_PREFIX " Loaded preset: %s", presets[current_preset].name);
            }
            continue;
        }

        // Parse key=value pairs
        if (current_preset >= 0) {
            char *equals = strchr(line, '=');
            if (equals && presets[current_preset].setting_count < MAX_SETTINGS_PER_PRESET) {
                *equals = '\0';
                char *key = line;
                char *value = equals + 1;

                trim_string(key);
                trim_string(value);

                int idx = presets[current_preset].setting_count++;
                strncpy(presets[current_preset].settings[idx].key, key, MAX_KEY_LEN - 1);
                presets[current_preset].settings[idx].key[MAX_KEY_LEN - 1] = '\0';
                strncpy(presets[current_preset].settings[idx].value, value, MAX_VALUE_LEN - 1);
                presets[current_preset].settings[idx].value[MAX_VALUE_LEN - 1] = '\0';
            }
        }
    }

    fclose(file);
    C64_LOG_INFO("" EFFECT_LOG_PREFIX " Loaded %d presets from %s", preset_count, filepath);
    return preset_count > 0;
}

bool c64_effect_init(void)
{
    // Reset preset storage
    preset_count = 0;
    memset(presets, 0, sizeof(presets));

    // Get the path to the presets.ini file
    char *filepath = obs_module_file("effect_presets.ini");
    if (!filepath) {
        C64_LOG_WARNING("" EFFECT_LOG_PREFIX " Failed to get effect_presets.ini path");
        return false;
    }

    bool success = parse_presets_file(filepath);
    bfree(filepath);

    if (!success) {
        C64_LOG_WARNING("" EFFECT_LOG_PREFIX " No presets loaded - using defaults only");
    }

    return success;
}

void c64_effect_cleanup(void)
{
    preset_count = 0;
    memset(presets, 0, sizeof(presets));
}

void c64_effect_populate_list(obs_property_t *preset_prop)
{
    if (!preset_prop)
        return;

    // Add all loaded presets to the dropdown
    for (int i = 0; i < preset_count; i++) {
        obs_property_list_add_string(preset_prop, presets[i].name, presets[i].name);
    }
}

bool c64_effect_apply(obs_data_t *settings, const char *preset_name)
{
    if (!settings || !preset_name)
        return false;

    // Find the preset by name
    int preset_idx = -1;
    for (int i = 0; i < preset_count; i++) {
        if (strcmp(presets[i].name, preset_name) == 0) {
            preset_idx = i;
            break;
        }
    }

    if (preset_idx < 0) {
        C64_LOG_WARNING("" EFFECT_LOG_PREFIX " Preset '%s' not found", preset_name);
        return false;
    }

    // Apply all settings from the preset
    struct preset *p = &presets[preset_idx];
    for (int i = 0; i < p->setting_count; i++) {
        const char *key = p->settings[i].key;
        const char *value = p->settings[i].value;

        // Try to parse the value and set it appropriately
        // Check if it's a floating point number
        if (strchr(value, '.') != NULL) {
            double dval = atof(value);
            obs_data_set_double(settings, key, dval);
        }
        // Check if it's an integer
        else if (value[0] == '-' || (value[0] >= '0' && value[0] <= '9')) {
            int ival = atoi(value);
            obs_data_set_int(settings, key, ival);
        }
        // Otherwise treat as string
        else {
            obs_data_set_string(settings, key, value);
        }
    }

    C64_LOG_INFO("" EFFECT_LOG_PREFIX " Applied preset: %s (%d settings)", preset_name, p->setting_count);
    return true;
}

int c64_effect_get_count(void)
{
    return preset_count;
}
