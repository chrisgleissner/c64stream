/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-keyboard.h"

bool c64_keyboard_queue_output(c64_keyboard_t *keyboard, const c64_output_t *output)
{
    (void)keyboard;
    (void)output;
    return true;
}
