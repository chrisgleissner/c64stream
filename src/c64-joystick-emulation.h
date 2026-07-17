/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_JOYSTICK_EMULATION_H
#define C64_JOYSTICK_EMULATION_H

#include <stdbool.h>
#include <stdint.h>

/* Joystick-emulation hotkeys. F10 toggles keyboard-vs-joystick mode, F11
 * toggles the target port. These are identified by the named key code the
 * OBS event translator produces -- not by the platform virtual key, since the
 * hotkeys are identical across Windows / Linux / macOS in OBS. */
typedef enum {
    C64_JOYSTICK_HOTKEY_NONE = 0,
    C64_JOYSTICK_HOTKEY_F10,
    C64_JOYSTICK_HOTKEY_F11,
} c64_joystick_hotkey_t;

/* Classify a key code as one of the joystick hotkeys, or NONE. */
c64_joystick_hotkey_t c64_joystick_classify_hotkey(const char *key_code);

/* Resolve the platform virtual key for a joystick input direction or fire.
 * Returns "up"/"down"/"left"/"right"/"fire" for the matching vkey, or NULL
 * if the vkey is not part of the joystick map. */
const char *c64_joystick_input_for_vkey(uint32_t native_vkey);

#endif // C64_JOYSTICK_EMULATION_H
