/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-dispatch-config.h"

#include "c64-rest-client.h"

#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

typedef struct {
    const char *target;
    const char *category;
    const char *item;
} config_target_mapping_t;

static const config_target_mapping_t sid_model_map[] = {{"ULTI1", "SID Sockets Configuration", "UltiSID 1 Model"},
                                                        {"ULTI2", "SID Sockets Configuration", "UltiSID 2 Model"}};

static const config_target_mapping_t sid_enable_map[] = {{"SOCKET1", "SID Sockets Configuration", "SID Socket 1"},
                                                         {"SOCKET2", "SID Sockets Configuration", "SID Socket 2"}};

static const config_target_mapping_t sid_vol_map[] = {{"SOCKET1", "Audio Mixer", "Vol Sid Socket 1"},
                                                      {"SOCKET2", "Audio Mixer", "Vol Sid Socket 2"},
                                                      {"ULTI1", "Audio Mixer", "Vol UltiSid 1"},
                                                      {"ULTI2", "Audio Mixer", "Vol UltiSid 2"}};

static const config_target_mapping_t sid_filter_curve_map[] = {
    {"ULTI1", "UltiSID Configuration", "UltiSID 1 Filter Curve"},
    {"ULTI2", "UltiSID Configuration", "UltiSID 2 Filter Curve"}};

static const config_target_mapping_t sid_resonance_map[] = {{"ULTI1", "UltiSID Configuration", "UltiSID 1 Resonance"},
                                                            {"ULTI2", "UltiSID Configuration", "UltiSID 2 Resonance"}};

static const config_target_mapping_t sid_combined_map[] = {
    {"ULTI1", "UltiSID Configuration", "UltiSID 1 Combined Waveforms"},
    {"ULTI2", "UltiSID Configuration", "UltiSID 2 Combined Waveforms"}};

static const config_target_mapping_t sid_digis_map[] = {{"ULTI1", "UltiSID Configuration", "UltiSID 1 Digis"},
                                                        {"ULTI2", "UltiSID Configuration", "UltiSID 2 Digis"}};

static const char *find_config_mapping(const config_target_mapping_t *map, size_t count, const char *target,
                                       const char **out_item)
{
    if (!map || !target || !out_item) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(map[i].target, target) == 0) {
            *out_item = map[i].item;
            return map[i].category;
        }
    }
    return NULL;
}

bool c64script_dispatch_set_config_value(c64script_runtime_t *runtime, const char *category, const char *item,
                                         const char *value, const char *what)
{
    if (!c64_rest_config_set_value((c64_rest_client_t *)runtime->rest_client, category, item, value)) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s failed: %s", what,
                 c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
        return false;
    }
    return true;
}

bool c64script_dispatch_config(c64script_runtime_t *runtime, const c64script_instruction_t *instr)
{
    switch (instr->opcode) {
    case OP_CFG_SET: {
        c64script_value_t value;
        c64script_value_t item;
        c64script_value_t category;
        if (!c64script_runtime_pop(runtime, &value) || !c64script_runtime_pop(runtime, &item) ||
            !c64script_runtime_pop(runtime, &category)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&value);
            c64script_value_free(&item);
            c64script_value_free(&category);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &category, "CFG") || !require_string(runtime, &item, "CFG") ||
            !require_string(runtime, &value, "CFG")) {
            c64script_value_free(&value);
            c64script_value_free(&item);
            c64script_value_free(&category);
            return false;
        }
        bool ok =
            c64script_dispatch_set_config_value(runtime, category.as.string, item.as.string, value.as.string, "CFG");
        c64script_value_free(&value);
        c64script_value_free(&item);
        c64script_value_free(&category);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_CFG_SAVE:
        if (!runtime->rest_client) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!c64_rest_config_save((c64_rest_client_t *)runtime->rest_client)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "CFGSAVE failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            return false;
        }
        break;

    case OP_CFG_LOAD:
        if (!runtime->rest_client) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!c64_rest_config_load((c64_rest_client_t *)runtime->rest_client)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "CFGLOAD failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            return false;
        }
        break;

    case OP_CFG_RESET:
        if (!runtime->rest_client) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!c64_rest_config_reset((c64_rest_client_t *)runtime->rest_client)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "CFGRESET failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            return false;
        }
        break;

    case OP_CFG_GET: {
        c64script_value_t item;
        c64script_value_t category;
        if (!c64script_runtime_pop(runtime, &item) || !c64script_runtime_pop(runtime, &category)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&item);
            c64script_value_free(&category);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &category, "CFG$") || !require_string(runtime, &item, "CFG$")) {
            c64script_value_free(&item);
            c64script_value_free(&category);
            return false;
        }

        char value[256] = {0};
        if (!c64_rest_config_get_value((c64_rest_client_t *)runtime->rest_client, category.as.string, item.as.string,
                                       value, sizeof(value))) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "CFG$ failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&item);
            c64script_value_free(&category);
            return false;
        }

        c64script_value_t result_val = c64script_value_string(value);
        if (!c64script_runtime_push(runtime, result_val)) {
            c64script_value_free(&result_val);
            c64script_value_free(&item);
            c64script_value_free(&category);
            return false;
        }
        c64script_value_free(&item);
        c64script_value_free(&category);
        break;
    }

    case OP_CFG_ITEM: {
        c64script_value_t array_name;
        c64script_value_t path;
        if (!c64script_runtime_pop(runtime, &array_name) || !c64script_runtime_pop(runtime, &path)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&array_name);
            c64script_value_free(&path);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &array_name, "CFG_ITEM$") || !require_string(runtime, &path, "CFG_ITEM$")) {
            c64script_value_free(&array_name);
            c64script_value_free(&path);
            return false;
        }

        c64script_value_t array_var;
        if (!c64script_runtime_get_var(runtime, array_name.as.string, &array_var)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' not found", array_name.as.string);
            c64script_value_free(&array_name);
            c64script_value_free(&path);
            return false;
        }
        if (array_var.type != VALUE_ARRAY) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' is not an array",
                     array_name.as.string);
            c64script_value_free(&array_var);
            c64script_value_free(&array_name);
            c64script_value_free(&path);
            return false;
        }

        char **items = NULL;
        size_t count = 0;
        const char *path_arg = (path.as.string && path.as.string[0]) ? path.as.string : NULL;
        if (!c64_rest_config_list((c64_rest_client_t *)runtime->rest_client, path_arg, &items, &count)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "CFG_ITEM$ failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&array_var);
            c64script_value_free(&array_name);
            c64script_value_free(&path);
            return false;
        }

        if (count > array_var.as.array->size) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Array index out of bounds");
            c64_rest_string_list_free(items, count);
            c64script_value_free(&array_var);
            c64script_value_free(&array_name);
            c64script_value_free(&path);
            return false;
        }

        for (size_t i = 0; i < count; i++) {
            c64script_value_t entry_val = c64script_value_string(items[i]);
            c64script_array_set(array_var.as.array, i, entry_val);
            c64script_value_free(&entry_val);
        }

        bool ok = c64script_runtime_set_var(runtime, array_name.as.string, array_var);
        c64script_value_free(&array_var);
        c64script_value_free(&array_name);
        c64script_value_free(&path);
        c64_rest_string_list_free(items, count);
        if (!ok) {
            return false;
        }

        c64script_value_t count_val = c64script_value_number((double)count);
        if (!c64script_runtime_push(runtime, count_val)) {
            c64script_value_free(&count_val);
            return false;
        }
        break;
    }

    case OP_CFG_OPTIONS: {
        c64script_value_t array_name;
        c64script_value_t item;
        c64script_value_t category;
        if (!c64script_runtime_pop(runtime, &array_name) || !c64script_runtime_pop(runtime, &item) ||
            !c64script_runtime_pop(runtime, &category)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&array_name);
            c64script_value_free(&item);
            c64script_value_free(&category);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &category, "CFG_OPTIONS$") || !require_string(runtime, &item, "CFG_OPTIONS$") ||
            !require_string(runtime, &array_name, "CFG_OPTIONS$")) {
            c64script_value_free(&array_name);
            c64script_value_free(&item);
            c64script_value_free(&category);
            return false;
        }

        c64script_value_t array_var;
        if (!c64script_runtime_get_var(runtime, array_name.as.string, &array_var)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' not found", array_name.as.string);
            c64script_value_free(&array_name);
            c64script_value_free(&item);
            c64script_value_free(&category);
            return false;
        }
        if (array_var.type != VALUE_ARRAY) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Variable '%s' is not an array",
                     array_name.as.string);
            c64script_value_free(&array_var);
            c64script_value_free(&array_name);
            c64script_value_free(&item);
            c64script_value_free(&category);
            return false;
        }

        char **options = NULL;
        size_t count = 0;
        if (!c64_rest_config_list_options((c64_rest_client_t *)runtime->rest_client, category.as.string, item.as.string,
                                          &options, &count)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "CFG_OPTIONS$ failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&array_var);
            c64script_value_free(&array_name);
            c64script_value_free(&item);
            c64script_value_free(&category);
            return false;
        }

        if (count > array_var.as.array->size) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Array index out of bounds");
            c64_rest_string_list_free(options, count);
            c64script_value_free(&array_var);
            c64script_value_free(&array_name);
            c64script_value_free(&item);
            c64script_value_free(&category);
            return false;
        }

        for (size_t i = 0; i < count; i++) {
            c64script_value_t entry_val = c64script_value_string(options[i]);
            c64script_array_set(array_var.as.array, i, entry_val);
            c64script_value_free(&entry_val);
        }

        bool ok = c64script_runtime_set_var(runtime, array_name.as.string, array_var);
        c64script_value_free(&array_var);
        c64script_value_free(&array_name);
        c64script_value_free(&item);
        c64script_value_free(&category);
        c64_rest_string_list_free(options, count);
        if (!ok) {
            return false;
        }

        c64script_value_t count_val = c64script_value_number((double)count);
        if (!c64script_runtime_push(runtime, count_val)) {
            c64script_value_free(&count_val);
            return false;
        }
        break;
    }

    case OP_SID_MODEL: {
        c64script_value_t model;
        c64script_value_t target;
        if (!c64script_runtime_pop(runtime, &model) || !c64script_runtime_pop(runtime, &target)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&model);
            c64script_value_free(&target);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &target, "SID_MODEL") || !require_string(runtime, &model, "SID_MODEL")) {
            c64script_value_free(&model);
            c64script_value_free(&target);
            return false;
        }

        const char *item = NULL;
        const char *category = find_config_mapping(sid_model_map, sizeof(sid_model_map) / sizeof(sid_model_map[0]),
                                                   target.as.string, &item);
        if (!category) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "SID_MODEL target is read-only");
            c64script_value_free(&model);
            c64script_value_free(&target);
            return false;
        }

        bool ok = c64script_dispatch_set_config_value(runtime, category, item, model.as.string, "SID_MODEL");
        c64script_value_free(&model);
        c64script_value_free(&target);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_SID_ENABLE: {
        c64script_value_t enabled;
        c64script_value_t target;
        if (!c64script_runtime_pop(runtime, &enabled) || !c64script_runtime_pop(runtime, &target)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&enabled);
            c64script_value_free(&target);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &target, "SID_ENABLE") || !require_number(runtime, &enabled, "SID_ENABLE")) {
            c64script_value_free(&enabled);
            c64script_value_free(&target);
            return false;
        }

        const char *item = NULL;
        const char *category = find_config_mapping(sid_enable_map, sizeof(sid_enable_map) / sizeof(sid_enable_map[0]),
                                                   target.as.string, &item);
        if (!category) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid SID target");
            c64script_value_free(&enabled);
            c64script_value_free(&target);
            return false;
        }

        const char *value = (enabled.as.number != 0.0) ? "Enabled" : "Disabled";
        bool ok = c64script_dispatch_set_config_value(runtime, category, item, value, "SID_ENABLE");
        c64script_value_free(&enabled);
        c64script_value_free(&target);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_SID_VOL: {
        c64script_value_t level;
        c64script_value_t target;
        if (!c64script_runtime_pop(runtime, &level) || !c64script_runtime_pop(runtime, &target)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&level);
            c64script_value_free(&target);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &target, "SID_VOL") || !require_string(runtime, &level, "SID_VOL")) {
            c64script_value_free(&level);
            c64script_value_free(&target);
            return false;
        }

        const char *item = NULL;
        const char *category =
            find_config_mapping(sid_vol_map, sizeof(sid_vol_map) / sizeof(sid_vol_map[0]), target.as.string, &item);
        if (!category) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid SID target");
            c64script_value_free(&level);
            c64script_value_free(&target);
            return false;
        }

        bool ok = c64script_dispatch_set_config_value(runtime, category, item, level.as.string, "SID_VOL");
        c64script_value_free(&level);
        c64script_value_free(&target);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_SID_FILTER_CURVE: {
        c64script_value_t curve;
        c64script_value_t target;
        if (!c64script_runtime_pop(runtime, &curve) || !c64script_runtime_pop(runtime, &target)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&curve);
            c64script_value_free(&target);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &target, "SID_FILTER_CURVE") ||
            !require_string(runtime, &curve, "SID_FILTER_CURVE")) {
            c64script_value_free(&curve);
            c64script_value_free(&target);
            return false;
        }

        const char *item = NULL;
        const char *category = find_config_mapping(sid_filter_curve_map,
                                                   sizeof(sid_filter_curve_map) / sizeof(sid_filter_curve_map[0]),
                                                   target.as.string, &item);
        if (!category) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "SID_FILTER_CURVE supports ULTI1/ULTI2 only");
            c64script_value_free(&curve);
            c64script_value_free(&target);
            return false;
        }

        bool ok = c64script_dispatch_set_config_value(runtime, category, item, curve.as.string, "SID_FILTER_CURVE");
        c64script_value_free(&curve);
        c64script_value_free(&target);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_SID_RESONANCE: {
        c64script_value_t resonance;
        c64script_value_t target;
        if (!c64script_runtime_pop(runtime, &resonance) || !c64script_runtime_pop(runtime, &target)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&resonance);
            c64script_value_free(&target);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &target, "SID_RESONANCE") ||
            !require_string(runtime, &resonance, "SID_RESONANCE")) {
            c64script_value_free(&resonance);
            c64script_value_free(&target);
            return false;
        }

        const char *item = NULL;
        const char *category = find_config_mapping(
            sid_resonance_map, sizeof(sid_resonance_map) / sizeof(sid_resonance_map[0]), target.as.string, &item);
        if (!category) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "SID_RESONANCE supports ULTI1/ULTI2 only");
            c64script_value_free(&resonance);
            c64script_value_free(&target);
            return false;
        }

        bool ok = c64script_dispatch_set_config_value(runtime, category, item, resonance.as.string, "SID_RESONANCE");
        c64script_value_free(&resonance);
        c64script_value_free(&target);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_SID_COMBINED: {
        c64script_value_t combined;
        c64script_value_t target;
        if (!c64script_runtime_pop(runtime, &combined) || !c64script_runtime_pop(runtime, &target)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&combined);
            c64script_value_free(&target);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &target, "SID_COMBINED") || !require_string(runtime, &combined, "SID_COMBINED")) {
            c64script_value_free(&combined);
            c64script_value_free(&target);
            return false;
        }

        const char *item = NULL;
        const char *category = find_config_mapping(
            sid_combined_map, sizeof(sid_combined_map) / sizeof(sid_combined_map[0]), target.as.string, &item);
        if (!category) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "SID_COMBINED supports ULTI1/ULTI2 only");
            c64script_value_free(&combined);
            c64script_value_free(&target);
            return false;
        }

        bool ok = c64script_dispatch_set_config_value(runtime, category, item, combined.as.string, "SID_COMBINED");
        c64script_value_free(&combined);
        c64script_value_free(&target);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_SID_DIGIS: {
        c64script_value_t level;
        c64script_value_t target;
        if (!c64script_runtime_pop(runtime, &level) || !c64script_runtime_pop(runtime, &target)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&level);
            c64script_value_free(&target);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &target, "SID_DIGIS") || !require_string(runtime, &level, "SID_DIGIS")) {
            c64script_value_free(&level);
            c64script_value_free(&target);
            return false;
        }

        const char *item = NULL;
        const char *category = find_config_mapping(sid_digis_map, sizeof(sid_digis_map) / sizeof(sid_digis_map[0]),
                                                   target.as.string, &item);
        if (!category) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "SID_DIGIS supports ULTI1/ULTI2 only");
            c64script_value_free(&level);
            c64script_value_free(&target);
            return false;
        }

        bool ok = c64script_dispatch_set_config_value(runtime, category, item, level.as.string, "SID_DIGIS");
        c64script_value_free(&level);
        c64script_value_free(&target);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_VIC_MODE: {
        c64script_value_t mode;
        if (!c64script_runtime_pop(runtime, &mode)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&mode);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &mode, "VIC_MODE")) {
            c64script_value_free(&mode);
            return false;
        }
        bool ok = c64script_dispatch_set_config_value(runtime, "U64 Specific Settings", "System Mode", mode.as.string,
                                                      "VIC_MODE");
        c64script_value_free(&mode);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_CPU_SPEED: {
        c64script_value_t speed;
        if (!c64script_runtime_pop(runtime, &speed)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&speed);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &speed, "CPU_SPEED")) {
            c64script_value_free(&speed);
            return false;
        }
        bool ok = c64script_dispatch_set_config_value(runtime, "U64 Specific Settings", "CPU Speed", speed.as.string,
                                                      "CPU_SPEED");
        c64script_value_free(&speed);
        if (!ok) {
            return false;
        }
        break;
    }

    default:
        return false;
    }

    return true;
}
