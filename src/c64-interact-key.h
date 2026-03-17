/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

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
