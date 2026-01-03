/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-keyboard.h"
#include "c64-logging.h"
#include "c64-rest-client.h"

#include <stdlib.h>
#include <string.h>

#define KEYBOARD_LOG_PREFIX "[c64-keyboard] "

// C64 memory locations
#define C64_KEYBOARD_BUFFER 0x0277
#define C64_KEYBOARD_LENGTH 0x00C6
#define C64_KEYBOARD_BUFFER_SIZE 10

struct c64_keymap {
    char name[128];
    char type[32];
    char fallback[32];
    // TODO: Add mapping table
};

struct c64_keyboard {
    c64_rest_client_t *rest_client;
    c64_keymap_t *keymap;
    bool capturing;
    char status[64];
    // TODO: Add FIFO queue
    // TODO: Add worker thread
};

c64_keymap_t *c64_keymap_load(const char *path)
{
    if (!path) {
        return NULL;
    }

    // TODO: Implement .c64keymap.ini parser
    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Load keymap %s (stub)", path);

    c64_keymap_t *keymap = calloc(1, sizeof(c64_keymap_t));
    if (!keymap) {
        return NULL;
    }

    strncpy(keymap->name, "Default", sizeof(keymap->name) - 1);
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

    // TODO: Implement keymap lookup and conversion
    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Convert %s mod=%d (stub)", key_code, modifiers);
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
