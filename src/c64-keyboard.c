/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-keyboard.h"
#include "c64-logging.h"
#include "c64-rest-client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEYBOARD_LOG_PREFIX "[c64-keyboard] "
#define MAX_KEYMAP_ENTRIES 512

// C64 memory locations
#define C64_KEYBOARD_BUFFER 0x0277
#define C64_KEYBOARD_LENGTH 0x00C6
#define C64_KEYBOARD_BUFFER_SIZE 10

// Keymap entry
typedef struct {
    char key[64];     // Input key name (e.g., "a", "return", "f1")
    uint8_t value;    // PETSCII code or special value
    bool is_symbolic; // True if value is symbolic name
    char symbol[32];  // Symbolic name (e.g., "c64:RETURN")
} keymap_entry_t;

// Symbolic key definitions
typedef struct {
    const char *name;
    uint8_t code;
} symbolic_key_t;

static const symbolic_key_t symbolic_keys[] = {{"c64:RETURN", 0x0D},
                                               {"c64:RUNSTOP", 0x03},
                                               {"c64:INSTDEL", 0x14},
                                               {"c64:TAB", 0x09},
                                               {"c64:F1", 0x85},
                                               {"c64:F3", 0x86},
                                               {"c64:F5", 0x87},
                                               {"c64:F7", 0x88},
                                               {"c64:F2", 0x89},
                                               {"c64:F4", 0x8A},
                                               {"c64:F6", 0x8B},
                                               {"c64:F8", 0x8C},
                                               {"c64:CURSOR_UP", 0x91},
                                               {"c64:CURSOR_DOWN", 0x11},
                                               {"c64:CURSOR_LEFT", 0x9D},
                                               {"c64:CURSOR_RIGHT", 0x1D},
                                               {"c64:HOME", 0x13},
                                               {"c64:CLEAR", 0x93},
                                               {"c64:SHIFT_RETURN", 0x8D},
                                               {"c64:SHIFT_SPACE", 0xA0},
                                               {"c64:CLR_HOME", 0x13},
                                               {"c64:INSERT", 0x94},
                                               {"c64:DELETE", 0x14},
                                               {"c64:RVS_ON", 0x12},
                                               {"c64:RVS_OFF", 0x92},
                                               {"c64:COLOR_BLACK", 0x90},
                                               {"c64:COLOR_WHITE", 0x05},
                                               {"c64:COLOR_RED", 0x1C},
                                               {"c64:COLOR_CYAN", 0x9F},
                                               {"c64:COLOR_PURPLE", 0x9C},
                                               {"c64:COLOR_GREEN", 0x1E},
                                               {"c64:COLOR_BLUE", 0x1F},
                                               {"c64:COLOR_YELLOW", 0x9E},
                                               {"c64:COLOR_ORANGE", 0x81},
                                               {"c64:COLOR_BROWN", 0x95},
                                               {"c64:COLOR_LT_RED", 0x96},
                                               {"c64:COLOR_DK_GREY", 0x97},
                                               {"c64:COLOR_GREY", 0x98},
                                               {"c64:COLOR_LT_GREEN", 0x99},
                                               {"c64:COLOR_LT_BLUE", 0x9A},
                                               {"c64:COLOR_LT_GREY", 0x9B},
                                               {NULL, 0}};

struct c64_keymap {
    char name[128];
    char description[256];
    keymap_entry_t entries[MAX_KEYMAP_ENTRIES];
    int num_entries;
};

struct c64_keyboard {
    c64_rest_client_t *rest_client;
    c64_keymap_t *keymap;
    bool capturing;
    char status[64];
    // TODO: Add FIFO queue
    // TODO: Add worker thread
};

static uint8_t lookup_symbolic_key(const char *name)
{
    for (int i = 0; symbolic_keys[i].name; i++) {
        if (strcmp(symbolic_keys[i].name, name) == 0) {
            return symbolic_keys[i].code;
        }
    }
    return 0;
}

static void trim(char *str)
{
    // Trim leading whitespace
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';

    // Move trimmed string to start
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

c64_keymap_t *c64_keymap_load(const char *path)
{
    if (!path) {
        return NULL;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to open keymap: %s", path);
        return NULL;
    }

    c64_keymap_t *keymap = calloc(1, sizeof(c64_keymap_t));
    if (!keymap) {
        fclose(file);
        return NULL;
    }

    strncpy(keymap->name, "Default", sizeof(keymap->name) - 1);

    char line[512];
    char section[64] = "";
    int line_num = 0;

    while (fgets(line, sizeof(line), file)) {
        line_num++;
        trim(line);

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        // Section header
        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            strncpy(section, line + 1, sizeof(section) - 1);
            section[strlen(section) - 1] = '\0';
            continue;
        }

        // Key=value pair
        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }

        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        trim(key);
        trim(value);

        if (strcmp(section, "meta") == 0) {
            // Meta section
            if (strcmp(key, "name") == 0) {
                strncpy(keymap->name, value, sizeof(keymap->name) - 1);
            } else if (strcmp(key, "description") == 0) {
                strncpy(keymap->description, value, sizeof(keymap->description) - 1);
            }
        } else if (strcmp(section, "map") == 0) {
            // Map section
            if (keymap->num_entries >= MAX_KEYMAP_ENTRIES) {
                C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Too many keymap entries, ignoring: %s=%s", key, value);
                continue;
            }

            keymap_entry_t *entry = &keymap->entries[keymap->num_entries];
            strncpy(entry->key, key, sizeof(entry->key) - 1);

            // Parse value
            if (strncmp(value, "c64:", 4) == 0) {
                // Symbolic key
                entry->is_symbolic = true;
                strncpy(entry->symbol, value, sizeof(entry->symbol) - 1);
                entry->value = lookup_symbolic_key(value);
                if (entry->value == 0) {
                    C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Unknown symbolic key: %s", value);
                    continue;
                }
            } else if (strncmp(value, "0x", 2) == 0 || strncmp(value, "0X", 2) == 0) {
                // Hex value
                entry->is_symbolic = false;
                entry->value = (uint8_t)strtol(value, NULL, 16);
            } else if (isdigit((unsigned char)value[0])) {
                // Decimal value
                entry->is_symbolic = false;
                entry->value = (uint8_t)atoi(value);
            } else {
                // Literal character
                entry->is_symbolic = false;
                entry->value = (uint8_t)value[0];
            }

            keymap->num_entries++;
        }
    }

    fclose(file);

    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Loaded keymap: %s (%d entries)", keymap->name, keymap->num_entries);
    return keymap;
}

void c64_keymap_destroy(c64_keymap_t *keymap)
{
    if (!keymap) {
        return;
    }
    free(keymap);
}

const char *c64_keymap_get_name(c64_keymap_t *keymap)
{
    if (!keymap) {
        return "";
    }
    return keymap->name;
}

bool c64_keymap_convert(c64_keymap_t *keymap, const char *key_code, int modifiers, c64_output_t *output)
{
    if (!keymap || !key_code || !output) {
        return false;
    }

    (void)modifiers; // Unused for now

    // Search keymap for matching entry
    for (int i = 0; i < keymap->num_entries; i++) {
        if (strcmp(keymap->entries[i].key, key_code) == 0) {
            if (keymap->entries[i].is_symbolic) {
                // Symbolic output
                output->mode = C64_OUTPUT_SYMBOLIC;
                strncpy(output->data.symbol, keymap->entries[i].symbol, sizeof(output->data.symbol) - 1);
                output->data.symbol[sizeof(output->data.symbol) - 1] = '\0';
            } else {
                // PETSCII output
                output->mode = C64_OUTPUT_PETSCII;
                output->data.petscii = keymap->entries[i].value;
            }

            return true;
        }
    }

    // No mapping found
    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "No mapping for key: %s", key_code);
    return false;
}

c64_keyboard_t *c64_keyboard_create(void *rest_client)
{
    if (!rest_client) {
        return NULL;
    }

    c64_keyboard_t *keyboard = calloc(1, sizeof(c64_keyboard_t));
    if (!keyboard) {
        return NULL;
    }

    keyboard->rest_client = (c64_rest_client_t *)rest_client;
    keyboard->capturing = false;
    strncpy(keyboard->status, "idle", sizeof(keyboard->status) - 1);

    // TODO: Create worker thread
    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Created keyboard controller");
    return keyboard;
}

void c64_keyboard_destroy(c64_keyboard_t *keyboard)
{
    if (!keyboard) {
        return;
    }

    // TODO: Stop worker thread
    // TODO: Free queue
    free(keyboard);
}

void c64_keyboard_set_keymap(c64_keyboard_t *keyboard, c64_keymap_t *keymap)
{
    if (!keyboard) {
        return;
    }
    keyboard->keymap = keymap;
}

void c64_keyboard_set_capture(c64_keyboard_t *keyboard, bool enabled)
{
    if (!keyboard) {
        return;
    }

    keyboard->capturing = enabled;
    if (!enabled) {
        // TODO: Flush queue, stop injection
        C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Capture disabled, flushing queue");
    } else {
        C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Capture enabled");
    }
}

bool c64_keyboard_is_capturing(c64_keyboard_t *keyboard)
{
    if (!keyboard) {
        return false;
    }
    return keyboard->capturing;
}

void c64_keyboard_queue_output(c64_keyboard_t *keyboard, const c64_output_t *output)
{
    if (!keyboard || !output) {
        return;
    }

    // TODO: Add to FIFO queue
    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Queue output mode=%d (stub)", output->mode);
}

const char *c64_keyboard_get_status(c64_keyboard_t *keyboard)
{
    if (!keyboard) {
        return "invalid";
    }
    return keyboard->status;
}

bool c64_keyboard_discover_keymaps(char ***paths, size_t *count)
{
    if (!paths || !count) {
        return false;
    }

    // TODO: Scan builtin and user keymap directories
    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Discover keymaps (stub)");
    *paths = NULL;
    *count = 0;
    return false;
}
