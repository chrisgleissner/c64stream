/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-interact-key.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>

static bool copy_key_code(char key_code[64], const char *value)
{
    if (!key_code || !value || value[0] == '\0') {
        return false;
    }

    snprintf(key_code, 64, "%s", value);
    return true;
}

c64_interact_key_result_t c64_interact_translate_key_code(uint32_t native_vkey, const char *text, char key_code[64])
{
    if (!key_code) {
        return C64_INTERACT_KEY_NONE;
    }

    key_code[0] = '\0';

    const bool has_text = (text && text[0] != '\0');
    const bool single_char = (has_text && text[1] == '\0');
    const bool text_is_space = (single_char && text[0] == ' ');
    const bool text_is_printable = (single_char && isprint((unsigned char)text[0]) && !text_is_space);

    if (text_is_printable) {
        return copy_key_code(key_code, text) ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    }

    switch (native_vkey) {
    case 0x0D:
    case 0xFF0D:
        return copy_key_code(key_code, "return") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    case 0x08:
    case 0xFF08:
        return copy_key_code(key_code, "backspace") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    case 0x2E:
    case 0xFFFF:
        if (!has_text) {
            return copy_key_code(key_code, "delete") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
        }
        break;
    case 0x20:
        return copy_key_code(key_code, "space") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    case 0x09:
    case 0xFF09:
        return copy_key_code(key_code, "tab") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    case 0x1B:
    case 0xFF1B:
        return C64_INTERACT_KEY_WARM_START;
    case 0x24:
    case 0xFF50:
        return copy_key_code(key_code, "home") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    case 0x2D:
    case 0xFF63:
        return copy_key_code(key_code, "insert") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    case 0x26:
    case 0xFF52:
        return copy_key_code(key_code, "ArrowUp") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    case 0x28:
    case 0xFF54:
        return copy_key_code(key_code, "ArrowDown") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    case 0x25:
    case 0xFF51:
        return copy_key_code(key_code, "ArrowLeft") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    case 0x27:
    case 0xFF53:
        return copy_key_code(key_code, "ArrowRight") ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    default:
        break;
    }

    if (has_text) {
        return copy_key_code(key_code, text) ? C64_INTERACT_KEY_TRANSLATED : C64_INTERACT_KEY_NONE;
    }

    if ((native_vkey >= '0' && native_vkey <= '9') || (native_vkey >= 'A' && native_vkey <= 'Z') ||
        (native_vkey >= 'a' && native_vkey <= 'z')) {
        key_code[0] = (char)native_vkey;
        key_code[1] = '\0';
        return C64_INTERACT_KEY_TRANSLATED;
    }

    return C64_INTERACT_KEY_NONE;
}
