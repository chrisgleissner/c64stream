/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-dispatch-effects.h"

#include "c64-logging.h"

#include <obs-module.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    C64_SCRIPT_UPDATE_STRING,
    C64_SCRIPT_UPDATE_DOUBLE,
    C64_SCRIPT_UPDATE_INT,
    C64_SCRIPT_UPDATE_BOOL,
} c64_script_update_type_t;

typedef struct {
    obs_source_t *source;
    c64_script_update_type_t type;
    char key[128];
    char string_value[256];
    double number_value;
    int64_t int_value;
    bool bool_value;
} c64_script_source_update_t;

static void c64_script_apply_source_update(void *data)
{
    c64_script_source_update_t *update = (c64_script_source_update_t *)data;
    if (!update || !update->source) {
        if (update) {
            free(update);
        }
        return;
    }

    obs_data_t *settings = obs_source_get_settings(update->source);
    if (settings) {
        switch (update->type) {
        case C64_SCRIPT_UPDATE_STRING:
            obs_data_set_string(settings, update->key, update->string_value);
            break;
        case C64_SCRIPT_UPDATE_DOUBLE:
            obs_data_set_double(settings, update->key, update->number_value);
            break;
        case C64_SCRIPT_UPDATE_INT:
            obs_data_set_int(settings, update->key, update->int_value);
            break;
        case C64_SCRIPT_UPDATE_BOOL:
            obs_data_set_bool(settings, update->key, update->bool_value);
            break;
        }
        obs_source_update(update->source, settings);
        if (c64script_debug_logging_enabled()) {
            blog(LOG_DEBUG, "[c64script] EFFECT apply: key=%s value=%s (OBS UI thread)", update->key,
                 update->type == C64_SCRIPT_UPDATE_STRING ? update->string_value : "<numeric>");
        }
        obs_data_release(settings);
    }

    obs_source_release(update->source);
    free(update);
}

static bool c64_script_queue_source_update(obs_source_t *source, c64_script_update_type_t type, const char *key,
                                           const char *string_value, double number_value, int64_t int_value,
                                           bool bool_value)
{
    if (!source || !key || key[0] == '\0') {
        return false;
    }

    c64_script_source_update_t *update = calloc(1, sizeof(c64_script_source_update_t));
    if (!update) {
        return false;
    }

    update->source = source;
    update->type = type;
    strncpy(update->key, key, sizeof(update->key) - 1);
    update->key[sizeof(update->key) - 1] = '\0';

    if (string_value) {
        strncpy(update->string_value, string_value, sizeof(update->string_value) - 1);
        update->string_value[sizeof(update->string_value) - 1] = '\0';
    }
    update->number_value = number_value;
    update->int_value = int_value;
    update->bool_value = bool_value;

    update->source = obs_source_get_ref(source);
    if (!update->source) {
        free(update);
        return false;
    }

    obs_queue_task(OBS_TASK_UI, c64_script_apply_source_update, update, false);
    return true;
}

bool c64script_dispatch_effects(c64script_runtime_t *runtime, const c64script_instruction_t *instr)
{
    switch (instr->opcode) {
    case OP_EFFECT: {
        // EFFECT preset_name - Apply effect preset
        c64script_value_t preset;
        if (!c64script_runtime_pop(runtime, &preset))
            return false;
        if (preset.type != VALUE_STRING) {
            c64script_value_free(&preset);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (EFFECT)");
            return false;
        }
        if (!runtime->obs_source) {
            if (c64script_debug_logging_enabled()) {
                blog(LOG_DEBUG, "[c64script] EFFECT: OBS source not available, skipping");
            }
            c64script_value_free(&preset);
            break;
        }

        obs_source_t *source = (obs_source_t *)runtime->obs_source;
        if (c64script_debug_logging_enabled()) {
            blog(LOG_DEBUG, "[c64script] EFFECT queue: preset=%s", preset.as.string ? preset.as.string : "");
        }
        if (!c64_script_queue_source_update(source, C64_SCRIPT_UPDATE_STRING, "crt_preset",
                                            preset.as.string ? preset.as.string : "", 0.0, 0, false)) {
            c64script_value_free(&preset);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to queue OBS update");
            return false;
        }
        c64script_value_free(&preset);
        break;
    }

    case OP_EFFECTPARAM: {
        // EFFECTPARAM param_name value - Set effect parameter
        c64script_value_t value, param;
        if (!c64script_runtime_pop(runtime, &value) || !c64script_runtime_pop(runtime, &param))
            return false;
        if (param.type != VALUE_STRING || value.type != VALUE_NUMBER) {
            c64script_value_free(&param);
            c64script_value_free(&value);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (EFFECTPARAM)");
            return false;
        }
        if (!runtime->obs_source) {
            if (c64script_debug_logging_enabled()) {
                blog(LOG_DEBUG, "[c64script] EFFECTPARAM: OBS source not available, skipping");
            }
            c64script_value_free(&param);
            c64script_value_free(&value);
            break;
        }

        obs_source_t *source = (obs_source_t *)runtime->obs_source;
        const char *param_name = param.as.string ? param.as.string : "";
        const bool is_preserve_size = strcmp(param_name, "preserve_size") == 0;
        const c64_script_update_type_t update_type = is_preserve_size ? C64_SCRIPT_UPDATE_BOOL
                                                                      : C64_SCRIPT_UPDATE_DOUBLE;
        if (!c64_script_queue_source_update(source, update_type, param_name, NULL, value.as.number, 0,
                                            value.as.number != 0.0)) {
            c64script_value_free(&param);
            c64script_value_free(&value);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to queue OBS update");
            return false;
        }
        c64script_value_free(&param);
        c64script_value_free(&value);
        break;
    }

    case OP_PALETTE: {
        // PALETTE palette_name - Load palette
        c64script_value_t palette;
        if (!c64script_runtime_pop(runtime, &palette))
            return false;
        if (palette.type != VALUE_STRING) {
            c64script_value_free(&palette);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (PALETTE)");
            return false;
        }
        if (!runtime->obs_source) {
            if (c64script_debug_logging_enabled()) {
                blog(LOG_DEBUG, "[c64script] PALETTE: OBS source not available, skipping");
            }
            c64script_value_free(&palette);
            break;
        }

        obs_source_t *source = (obs_source_t *)runtime->obs_source;
        if (!c64_script_queue_source_update(source, C64_SCRIPT_UPDATE_STRING, "palette",
                                            palette.as.string ? palette.as.string : "", 0.0, 0, false)) {
            c64script_value_free(&palette);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to queue OBS update");
            return false;
        }
        c64script_value_free(&palette);
        break;
    }

    case OP_PALETTECOLOR: {
        // PALETTECOLOR index, r, g, b - Set individual palette color
        c64script_value_t b_val, g_val, r_val, index_val;
        if (!c64script_runtime_pop(runtime, &b_val))
            return false;
        if (!c64script_runtime_pop(runtime, &g_val)) {
            c64script_value_free(&b_val);
            return false;
        }
        if (!c64script_runtime_pop(runtime, &r_val)) {
            c64script_value_free(&b_val);
            c64script_value_free(&g_val);
            return false;
        }
        if (!c64script_runtime_pop(runtime, &index_val)) {
            c64script_value_free(&b_val);
            c64script_value_free(&g_val);
            c64script_value_free(&r_val);
            return false;
        }

        if (index_val.type != VALUE_NUMBER || r_val.type != VALUE_NUMBER || g_val.type != VALUE_NUMBER ||
            b_val.type != VALUE_NUMBER) {
            c64script_value_free(&index_val);
            c64script_value_free(&r_val);
            c64script_value_free(&g_val);
            c64script_value_free(&b_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (PALETTECOLOR)");
            return false;
        }

        int index = (int)index_val.as.number;
        int r = (int)r_val.as.number;
        int g = (int)g_val.as.number;
        int b = (int)b_val.as.number;

        c64script_value_free(&index_val);
        c64script_value_free(&r_val);
        c64script_value_free(&g_val);
        c64script_value_free(&b_val);

        if (index < 0 || index > 15) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY (palette index)");
            return false;
        }
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY (RGB values)");
            return false;
        }

        if (!runtime->obs_source) {
            if (c64script_debug_logging_enabled()) {
                blog(LOG_DEBUG, "[c64script] PALETTECOLOR: OBS source not available, skipping");
            }
            break;
        }

        obs_source_t *source = (obs_source_t *)runtime->obs_source;
        char color_key[32];
        snprintf(color_key, sizeof(color_key), "custom_color_%d", index);
        uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        if (!c64_script_queue_source_update(source, C64_SCRIPT_UPDATE_INT, color_key, NULL, 0.0, (int64_t)rgb, false)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to queue OBS update");
            return false;
        }
        break;
    }

    default:
        return false;
    }

    return true;
}
