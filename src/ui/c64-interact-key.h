/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    C64_INTERACT_KEY_NONE = 0,
    C64_INTERACT_KEY_TRANSLATED,
    C64_INTERACT_KEY_WARM_START,
} c64_interact_key_result_t;

typedef struct {
    char code[64];
    char text[64];
} c64_interact_key_t;

c64_interact_key_result_t c64_interact_translate_key_event(uint32_t native_vkey, const char *text,
                                                           c64_interact_key_t *key);
c64_interact_key_result_t c64_interact_translate_key_code(uint32_t native_vkey, const char *text, char key_code[64]);

bool c64_interact_key_is_escape(uint32_t native_vkey, uint32_t native_scancode);
bool c64_interact_key_is_tab(uint32_t native_vkey, uint32_t native_scancode);
bool c64_interact_should_reboot_chord(uint32_t native_vkey, uint32_t native_scancode, bool key_up, bool shift_down,
                                      bool ctrl_down, bool alt_down, bool meta_down, bool escape_down, bool tab_down);

/* Joystick emulation mode maps cursor keys and space to matrix "joystick"
 * input names ("up"/"down"/"left"/"right"/"fire"); NULL if vkey isn't one of
 * those. Needed on both key-down and key-up (held movement), unlike the
 * tap-oriented keyboard path. */
const char *c64_interact_joystick_input_for_vkey(uint32_t native_vkey);
