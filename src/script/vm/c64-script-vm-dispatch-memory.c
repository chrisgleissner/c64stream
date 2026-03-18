/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-dispatch-memory.h"

#include "c64-rest-client.h"
#include "c64-script-builtins.h"

#include <stdint.h>
#include <stdlib.h>

bool c64script_dispatch_memory(c64script_runtime_t *runtime, const c64script_instruction_t *instr)
{
    switch (instr->opcode) {
    case OP_CALL_PEEK: {
        c64script_value_t addr_val;
        if (!c64script_runtime_pop(runtime, &addr_val))
            return false;
        if (!require_number(runtime, &addr_val, "PEEK")) {
            c64script_value_free(&addr_val);
            return false;
        }

        if (addr_val.as.number < 0.0 || addr_val.as.number > 65535.0) {
            c64script_value_free(&addr_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
            return false;
        }

        uint16_t address = (uint16_t)addr_val.as.number;

        double result;
        if (!c64script_builtin_peek(runtime, address, &result)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "PEEK failed");
            c64script_value_free(&addr_val);
            return false;
        }

        c64script_value_t result_val = {.type = VALUE_NUMBER, .as.number = result};
        c64script_value_free(&addr_val);
        if (!c64script_runtime_push(runtime, result_val))
            return false;
        break;
    }

    case OP_POKE_SINGLE: {
        c64script_value_t value, address;
        if (!c64script_runtime_pop(runtime, &value) || !c64script_runtime_pop(runtime, &address))
            return false;
        if (!runtime->rest_client) {
            c64script_value_free(&address);
            c64script_value_free(&value);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_number(runtime, &address, "POKE") || !require_number(runtime, &value, "POKE")) {
            c64script_value_free(&address);
            c64script_value_free(&value);
            return false;
        }
        if (address.as.number < 0.0 || address.as.number > 65535.0) {
            c64script_value_free(&address);
            c64script_value_free(&value);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
            return false;
        }
        uint16_t addr = (uint16_t)(uint32_t)address.as.number;
        uint8_t byte = (uint8_t)((int)value.as.number & 0xFF);
        bool ok = c64_rest_write_memory((c64_rest_client_t *)runtime->rest_client, addr, &byte, 1);
        if (!ok) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "POKE failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&address);
            c64script_value_free(&value);
            return false;
        }
        c64script_value_free(&address);
        c64script_value_free(&value);
        break;
    }

    case OP_POKE_ARRAY: {
        uint32_t count = instr->operand;
        if (count > runtime->stack_size) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "POKE_ARRAY: not enough values on stack");
            return false;
        }

        c64script_value_t *values = malloc(count * sizeof(c64script_value_t));
        for (int i = (int)count - 1; i >= 0; i--) {
            if (!c64script_runtime_pop(runtime, &values[i])) {
                free(values);
                return false;
            }
        }

        c64script_value_t address;
        if (!c64script_runtime_pop(runtime, &address)) {
            for (uint32_t i = 0; i < count; i++) {
                c64script_value_free(&values[i]);
            }
            free(values);
            return false;
        }

        if (!runtime->rest_client) {
            c64script_value_free(&address);
            for (uint32_t i = 0; i < count; i++) {
                c64script_value_free(&values[i]);
            }
            free(values);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }

        if (!require_number(runtime, &address, "POKE")) {
            c64script_value_free(&address);
            for (uint32_t i = 0; i < count; i++) {
                c64script_value_free(&values[i]);
            }
            free(values);
            return false;
        }

        if (address.as.number < 0.0 || address.as.number > 65535.0) {
            c64script_value_free(&address);
            for (uint32_t i = 0; i < count; i++) {
                c64script_value_free(&values[i]);
            }
            free(values);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
            return false;
        }
        uint16_t base_addr = (uint16_t)(uint32_t)address.as.number;
        uint8_t buf[128];
        uint32_t offset = 0;

        while (offset < count) {
            uint32_t chunk = count - offset;
            if (chunk > sizeof(buf)) {
                chunk = (uint32_t)sizeof(buf);
            }
            for (uint32_t i = 0; i < chunk; i++) {
                if (!require_number(runtime, &values[offset + i], "POKE")) {
                    c64script_value_free(&address);
                    for (uint32_t j = 0; j < count; j++) {
                        c64script_value_free(&values[j]);
                    }
                    free(values);
                    return false;
                }
                buf[i] = (uint8_t)((int)values[offset + i].as.number & 0xFF);
            }

            bool ok = c64_rest_write_memory((c64_rest_client_t *)runtime->rest_client, (uint16_t)(base_addr + offset),
                                            buf, chunk);
            if (!ok) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "POKE failed: %s",
                         c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
                c64script_value_free(&address);
                for (uint32_t j = 0; j < count; j++) {
                    c64script_value_free(&values[j]);
                }
                free(values);
                return false;
            }

            offset += chunk;
        }
        c64script_value_free(&address);
        for (uint32_t i = 0; i < count; i++) {
            c64script_value_free(&values[i]);
        }
        free(values);
        break;
    }

    default:
        return false;
    }

    return true;
}
