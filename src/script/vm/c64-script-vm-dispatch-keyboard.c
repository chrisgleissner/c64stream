/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-dispatch-keyboard.h"

#include "c64-keyboard.h"

#include <ctype.h>
#include <string.h>

bool c64script_dispatch_keyboard(c64script_runtime_t *runtime, const c64script_instruction_t *instr)
{
    switch (instr->opcode) {
    case OP_AUTOSTART:
        if (!runtime->keyboard) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Keyboard not available");
            return false;
        }
        {
            c64_output_t output = {0};
            output.mode = C64_OUTPUT_TEXT;
            snprintf(output.data.text, sizeof(output.data.text), "%s", "LOAD\"*\",8,1\rRUN\r");
            c64_keyboard_queue_output((c64_keyboard_t *)runtime->keyboard, &output);
        }
        break;

    case OP_TYPE: {
        c64script_value_t text;
        if (!c64script_runtime_pop(runtime, &text))
            return false;
        if (text.type != VALUE_STRING) {
            c64script_value_free(&text);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (TYPE)");
            return false;
        }

        if (!runtime->keyboard) {
            c64script_value_free(&text);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Keyboard not available");
            return false;
        }

        const char *s = text.as.string ? text.as.string : "";
        while (*s) {
            c64_output_t output = {0};
            output.mode = C64_OUTPUT_TEXT;
            size_t chunk = strlen(s);
            if (chunk >= sizeof(output.data.text)) {
                chunk = sizeof(output.data.text) - 1;
            }
            memcpy(output.data.text, s, chunk);
            output.data.text[chunk] = '\0';
            c64_keyboard_queue_output((c64_keyboard_t *)runtime->keyboard, &output);
            s += chunk;
        }
        c64script_value_free(&text);
        break;
    }

    case OP_KEY: {
        c64script_value_t key;
        if (!c64script_runtime_pop(runtime, &key))
            return false;
        if (!runtime->keyboard) {
            c64script_value_free(&key);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Keyboard not available");
            return false;
        }

        c64_output_t output = {0};
        if (key.type == VALUE_STRING) {
            output.mode = C64_OUTPUT_SYMBOLIC;
            const char *in = key.as.string ? key.as.string : "";
            size_t len = strlen(in);
            if (len >= sizeof(output.data.symbol)) {
                len = sizeof(output.data.symbol) - 1;
            }
            for (size_t i = 0; i < len; i++) {
                output.data.symbol[i] = (char)toupper((unsigned char)in[i]);
            }
            output.data.symbol[len] = '\0';
        } else if (key.type == VALUE_NUMBER) {
            output.mode = C64_OUTPUT_PETSCII;
            output.data.petscii = (uint8_t)((int)key.as.number & 0xFF);
        } else {
            c64script_value_free(&key);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (KEY)");
            return false;
        }

        c64_keyboard_queue_output((c64_keyboard_t *)runtime->keyboard, &output);
        c64script_value_free(&key);
        break;
    }

    default:
        return false;
    }

    return true;
}
