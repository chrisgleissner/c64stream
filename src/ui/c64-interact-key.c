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
    if (!text || text[0] == '\0' || text[0] == ' ') {
        return false;
    }
    // Single-byte ASCII printable (0x21-0x7E).
    if (text[1] == '\0') {
        return (unsigned char)text[0] > 0x20 && (unsigned char)text[0] != 0x7F;
    }
    return false;
}

static bool has_visible_text(const char *text)
{
    if (!text || text[0] == '\0') {
        return false;
    }
    // UTF-8-aware: printable if first byte is not a C0 control (0x00-0x1F) or DEL (0x7F).
    // Multi-byte UTF-8 lead bytes (0x80+) are always non-control and thus visible.
    return (unsigned char)text[0] > 0x1F && (unsigned char)text[0] != 0x7F;
}

bool c64_interact_key_is_escape(uint32_t native_vkey, uint32_t native_scancode)
{
#if defined(__APPLE__) || defined(C64_TEST_MACOS_KEYCODES)
    /* Carbon kVK_Escape.  Keep this branch separate: 0x35 is not a Windows
     * virtual key and macOS 0x7B..0x7E collide with Windows F12/etc. */
    if (native_vkey == 0x35) {
        return true;
    }
#endif
    return native_scancode == 0x01 || native_vkey == 0x1B || native_vkey == 0xFF1B;
}

bool c64_interact_key_is_tab(uint32_t native_vkey, uint32_t native_scancode)
{
#if defined(__APPLE__) || defined(C64_TEST_MACOS_KEYCODES)
    if (native_vkey == 0x30) { /* Carbon kVK_Tab */
        return true;
    }
#endif
    return native_scancode == 0x0F || native_vkey == 0x09 || native_vkey == 0xFF09;
}

bool c64_interact_should_reboot_chord(uint32_t native_vkey, uint32_t native_scancode, bool key_up, bool shift_down,
                                      bool ctrl_down, bool alt_down, bool meta_down, bool escape_down, bool tab_down)
{
    (void)shift_down;

    if (key_up || ctrl_down || alt_down || meta_down || !escape_down || !tab_down) {
        return false;
    }

    return c64_interact_key_is_escape(native_vkey, native_scancode) ||
           c64_interact_key_is_tab(native_vkey, native_scancode);
}

const char *c64_interact_joystick_input_for_vkey(uint32_t native_vkey)
{
#if defined(__APPLE__) || defined(C64_TEST_MACOS_KEYCODES)
    switch (native_vkey) {
    case 0x7B: /* kVK_LeftArrow */
        return "left";
    case 0x7E: /* kVK_UpArrow */
        return "up";
    case 0x7C: /* kVK_RightArrow */
        return "right";
    case 0x7D: /* kVK_DownArrow */
        return "down";
    case 0x31: /* kVK_Space */
        return "fire";
    default:
        return NULL;
    }
#else
    // Same vkey values as the ArrowLeft/Up/Right/Down/Space cases in
    // lookup_key_code_from_vkey() below (Windows VK_* and X11 keysym forms).
    switch (native_vkey) {
    case 0x25:
    case 0xFF51:
        return "left";
    case 0x26:
    case 0xFF52:
        return "up";
    case 0x27:
    case 0xFF53:
        return "right";
    case 0x28:
    case 0xFF54:
        return "down";
    case 0x20:
        return "fire";
    default:
        return NULL;
    }
#endif
}

static bool lookup_key_code_from_vkey(uint32_t native_vkey, char key_code[64])
{
    if (!key_code) {
        return false;
    }

#if defined(__APPLE__) || defined(C64_TEST_MACOS_KEYCODES)
    /* Carbon virtual keycodes are physical key positions, not ASCII/Windows
     * VKs.  Resolve all special keys before the generic tables so 0x7B maps
     * to ArrowLeft rather than Windows F12. */
    switch (native_vkey) {
    case 0x33:
        return copy_identifier(key_code, "Backspace");
    case 0x30:
        return copy_identifier(key_code, "Tab");
    case 0x24:
        return copy_identifier(key_code, "Enter");
    case 0x35:
        return copy_identifier(key_code, "Escape");
    case 0x31:
        return copy_identifier(key_code, "Space");
    case 0x7B:
        return copy_identifier(key_code, "ArrowLeft");
    case 0x7E:
        return copy_identifier(key_code, "ArrowUp");
    case 0x7C:
        return copy_identifier(key_code, "ArrowRight");
    case 0x7D:
        return copy_identifier(key_code, "ArrowDown");
    case 0x7A:
        return copy_identifier(key_code, "F1");
    case 0x78:
        return copy_identifier(key_code, "F2");
    case 0x63:
        return copy_identifier(key_code, "F3");
    case 0x76:
        return copy_identifier(key_code, "F4");
    case 0x60:
        return copy_identifier(key_code, "F5");
    case 0x61:
        return copy_identifier(key_code, "F6");
    case 0x62:
        return copy_identifier(key_code, "F7");
    case 0x64:
        return copy_identifier(key_code, "F8");
    case 0x65:
        return copy_identifier(key_code, "F9");
    case 0x6D:
        return copy_identifier(key_code, "F10");
    case 0x67:
        return copy_identifier(key_code, "F11");
    case 0x6F:
        return copy_identifier(key_code, "F12");
    default:
        break;
    }
    /* Carbon virtual keycodes are physical key positions, not ASCII/Windows
     * VKs. Any code not resolved above (letters, digits, keypad, punctuation)
     * must NOT fall through to the ASCII-range / Windows-VK tables below: e.g.
     * the keypad-decimal code 0x41 would be misread as "KeyA". On macOS those
     * keys resolve from the event's text in c64_interact_translate_key_event,
     * so return unresolved here and let the text path handle them. */
    return false;
#endif

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

    if (native_vkey == 0x1B || native_vkey == 0xFF1B
#if defined(__APPLE__) || defined(C64_TEST_MACOS_KEYCODES)
        || native_vkey == 0x35
#endif
    ) {
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
