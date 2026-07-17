/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-joystick-emulation.h"

#include <string.h>

#include "c64-interact-key.h"

c64_joystick_hotkey_t c64_joystick_classify_hotkey(const char *key_code)
{
    if (!key_code) {
        return C64_JOYSTICK_HOTKEY_NONE;
    }
    if (strcmp(key_code, "F10") == 0) {
        return C64_JOYSTICK_HOTKEY_F10;
    }
    if (strcmp(key_code, "F11") == 0) {
        return C64_JOYSTICK_HOTKEY_F11;
    }
    return C64_JOYSTICK_HOTKEY_NONE;
}

const char *c64_joystick_input_for_vkey(uint32_t native_vkey)
{
    return c64_interact_joystick_input_for_vkey(native_vkey);
}
