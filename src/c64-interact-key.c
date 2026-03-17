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
#include <string.h>

static bool copy_key_code(char key_code[64], const char *value)
{
    if (!key_code || !value || value[0] == '\0') {
        return false;
    }

    snprintf(key_code, 64, "%s", value);
    return true;
}

static bool copy_identifier(char identifier[64], const char *value)
{
    if (!identifier) {
        return false;
    }

    if (!value || value[0] == '\0') {
        identifier[0] = '\0';
        return false;
    }

    snprintf(identifier, 64, "%s", value);
    return true;
}

static bool is_single_printable_text(const char *text)
{
    return text && text[0] != '\0' && text[1] == '\0' && isprint((unsigned char)text[0]) && text[0] != ' ';
}

static bool has_visible_text(const char *text)
{
    return text && text[0] != '\0' && !iscntrl((unsigned char)text[0]);
}

static bool lookup_key_code_from_vkey(uint32_t native_vkey, char key_code[64])
{
    if (!key_code) {
        return false;
    }

    if ((native_vkey >= '0' && native_vkey <= '9') || (native_vkey >= 'A' && native_vkey <= 'Z')) {
        if (native_vkey >= '0' && native_vkey <= '9') {
            return snprintf(key_code, 64, "Digit%c", (char)native_vkey) > 0;
        }

        return snprintf(key_code, 64, "Key%c", (char)native_vkey) > 0;
    }

    if (native_vkey >= 'a' && native_vkey <= 'z') {
        return snprintf(key_code, 64, "Key%c", (char)toupper((int)native_vkey)) > 0;
    }

    switch (native_vkey) {
    case 0x08:
    case 0xFF08:
        return copy_identifier(key_code, "Backspace");
    case 0x09:
    case 0xFF09:
        return copy_identifier(key_code, "Tab");
    case 0x0D:
    case 0xFF0D:
    case 0xFF8D:
        return copy_identifier(key_code, "Enter");
    case 0x1B:
    case 0xFF1B:
        return copy_identifier(key_code, "Escape");
    case 0x13:
    case 0xFF13:
        return copy_identifier(key_code, "Pause");
    case 0x14:
    case 0xFFE5:
        return copy_identifier(key_code, "CapsLock");
    case 0x20:
        return copy_identifier(key_code, "Space");
    case 0x21:
    case 0xFF55:
        return copy_identifier(key_code, "PageUp");
    case 0x22:
    case 0xFF56:
        return copy_identifier(key_code, "PageDown");
    case 0x23:
    case 0xFF57:
        return copy_identifier(key_code, "End");
    case 0x24:
    case 0xFF50:
        return copy_identifier(key_code, "Home");
    case 0x25:
    case 0xFF51:
        return copy_identifier(key_code, "ArrowLeft");
    case 0x26:
    case 0xFF52:
        return copy_identifier(key_code, "ArrowUp");
    case 0x27:
    case 0xFF53:
        return copy_identifier(key_code, "ArrowRight");
    case 0x28:
    case 0xFF54:
        return copy_identifier(key_code, "ArrowDown");
    case 0x2D:
    case 0xFF63:
        return copy_identifier(key_code, "Insert");
    case 0x2E:
    case 0xFFFF:
        return copy_identifier(key_code, "Delete");
    case 0x70:
    case 0xFFBE:
        return copy_identifier(key_code, "F1");
    case 0x71:
    case 0xFFBF:
        return copy_identifier(key_code, "F2");
    case 0x72:
    case 0xFFC0:
        return copy_identifier(key_code, "F3");
    case 0x73:
    case 0xFFC1:
        return copy_identifier(key_code, "F4");
    case 0x74:
    case 0xFFC2:
        return copy_identifier(key_code, "F5");
    case 0x75:
    case 0xFFC3:
        return copy_identifier(key_code, "F6");
    case 0x76:
    case 0xFFC4:
        return copy_identifier(key_code, "F7");
    case 0x77:
    case 0xFFC5:
        return copy_identifier(key_code, "F8");
    case 0x78:
    case 0xFFC6:
        return copy_identifier(key_code, "F9");
    case 0x79:
    case 0xFFC7:
        return copy_identifier(key_code, "F10");
    case 0x7A:
    case 0xFFC8:
        return copy_identifier(key_code, "F11");
    case 0x7B:
    case 0xFFC9:
        return copy_identifier(key_code, "F12");
    case 0xBA:
    case ';':
        return copy_identifier(key_code, "Semicolon");
    case 0xBB:
    case '=':
        return copy_identifier(key_code, "Equal");
    case 0xBC:
    case ',':
        return copy_identifier(key_code, "Comma");
    case 0xBD:
        return copy_identifier(key_code, "Minus");
    case 0xBE:
        return copy_identifier(key_code, "Period");
    case 0xBF:
    case '/':
        return copy_identifier(key_code, "Slash");
    case 0xC0:
    case '`':
        return copy_identifier(key_code, "Backquote");
    case 0xDB:
    case '[':
        return copy_identifier(key_code, "BracketLeft");
    case 0xDC:
    case '\\':
        return copy_identifier(key_code, "Backslash");
    case 0xDD:
    case ']':
        return copy_identifier(key_code, "BracketRight");
    case 0xDE:
        return copy_identifier(key_code, "Quote");
    default:
        break;
    }

    key_code[0] = '\0';
    return false;
}

static bool lookup_key_code_from_text_char(char ch, char key_code[64])
{
    switch (ch) {
    case '!':
        return copy_identifier(key_code, "Digit1");
    case '"':
        return copy_identifier(key_code, "Quote");
    case '#':
        return copy_identifier(key_code, "Digit3");
    case '$':
        return copy_identifier(key_code, "Digit4");
    case '%':
        return copy_identifier(key_code, "Digit5");
    case '&':
        return copy_identifier(key_code, "Digit7");
    case '(':
        return copy_identifier(key_code, "Digit9");
    case ')':
        return copy_identifier(key_code, "Digit0");
    case '*':
        return copy_identifier(key_code, "Digit8");
    case '+':
        return copy_identifier(key_code, "Equal");
    case ':':
        return copy_identifier(key_code, "Semicolon");
    case '<':
        return copy_identifier(key_code, "Comma");
    case '>':
        return copy_identifier(key_code, "Period");
    case '?':
        return copy_identifier(key_code, "Slash");
    case '@':
        return copy_identifier(key_code, "Digit2");
    case '^':
        return copy_identifier(key_code, "Digit6");
    case '_':
        return copy_identifier(key_code, "Minus");
    case '[':
        return copy_identifier(key_code, "BracketLeft");
    case ']':
        return copy_identifier(key_code, "BracketRight");
    case '{':
        return copy_identifier(key_code, "BracketLeft");
    case '|':
        return copy_identifier(key_code, "Backslash");
    case '}':
        return copy_identifier(key_code, "BracketRight");
    case '~':
        return copy_identifier(key_code, "Backquote");
    case '-':
        return copy_identifier(key_code, "Minus");
    case '.':
        return copy_identifier(key_code, "Period");
    case '\'':
        return copy_identifier(key_code, "Quote");
    default:
        break;
    }

    return lookup_key_code_from_vkey((uint32_t)(unsigned char)ch, key_code);
}

c64_interact_key_result_t c64_interact_translate_key_event(uint32_t native_vkey, const char *text,
                                                           c64_interact_key_t *key)
{
    if (!key) {
        return C64_INTERACT_KEY_NONE;
    }

    key->code[0] = '\0';
    key->text[0] = '\0';

    const bool has_text = (text && text[0] != '\0');
    const bool single_char = (has_text && text[1] == '\0');
    const bool text_is_space = (single_char && text[0] == ' ');
    const bool text_is_printable = (is_single_printable_text(text) && !text_is_space);
    const bool text_is_visible = has_visible_text(text);

    lookup_key_code_from_vkey(native_vkey, key->code);

    if (native_vkey == 0x1B || native_vkey == 0xFF1B) {
        return C64_INTERACT_KEY_WARM_START;
    }

    if (single_char && (text[0] == '\r' || text[0] == '\n')) {
        copy_identifier(key->code, "Enter");
    }

    if (text_is_printable) {
        char text_code[64] = {0};
        if (lookup_key_code_from_text_char(text[0], text_code)) {
            copy_identifier(key->code, text_code);
        }
        copy_identifier(key->text, text);
    } else if (text_is_visible) {
        copy_identifier(key->text, text);
    } else if (!has_text && strcmp(key->code, "Space") == 0) {
        copy_identifier(key->text, " ");
    }

    if (key->code[0] != '\0' || key->text[0] != '\0') {
        return C64_INTERACT_KEY_TRANSLATED;
    }

    return C64_INTERACT_KEY_NONE;
}

c64_interact_key_result_t c64_interact_translate_key_code(uint32_t native_vkey, const char *text, char key_code[64])
{
    if (!key_code) {
        return C64_INTERACT_KEY_NONE;
    }

    c64_interact_key_t key = {{0}};
    c64_interact_key_result_t result = c64_interact_translate_key_event(native_vkey, text, &key);
    if (result != C64_INTERACT_KEY_TRANSLATED) {
        key_code[0] = '\0';
        return result;
    }

    if (key.code[0] != '\0') {
        return copy_key_code(key_code, key.code) ? result : C64_INTERACT_KEY_NONE;
    }

    return copy_key_code(key_code, key.text) ? result : C64_INTERACT_KEY_NONE;
}
