/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-dispatch-drives.h"

#include "c64-keyboard.h"
#include "c64-rest-client.h"
#include "c64-script-vm-dispatch-config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static bool keyword_equals(const char *value, const char *keyword)
{
    if (!value || !keyword) {
        return false;
    }
    return strcasecmp(value, keyword) == 0;
}

static bool drive_keyword_to_id(c64script_runtime_t *runtime, const char *value, const char **out_drive,
                                bool allow_default)
{
    if (!out_drive) {
        return false;
    }

    if (!value || value[0] == '\0') {
        if (allow_default) {
            *out_drive = "a";
            return true;
        }
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid drive selector");
        return false;
    }

    if (keyword_equals(value, "DRIVE_A")) {
        *out_drive = "a";
        return true;
    }
    if (keyword_equals(value, "DRIVE_B")) {
        *out_drive = "b";
        return true;
    }
    if (keyword_equals(value, "DRIVE_SOFTIEC")) {
        *out_drive = "softiec";
        return true;
    }

    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid drive selector");
    return false;
}

static const char *drive_type_keyword_to_value(c64script_runtime_t *runtime, const char *value, bool allow_empty)
{
    if (!value || value[0] == '\0') {
        return allow_empty ? NULL : "";
    }
    if (keyword_equals(value, "TYPE_D64")) {
        return "d64";
    }
    if (keyword_equals(value, "TYPE_G64")) {
        return "g64";
    }
    if (keyword_equals(value, "TYPE_D71")) {
        return "d71";
    }
    if (keyword_equals(value, "TYPE_G71")) {
        return "g71";
    }
    if (keyword_equals(value, "TYPE_D81")) {
        return "d81";
    }

    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid drive image type");
    return NULL;
}

static const char *drive_mount_mode_keyword_to_value(c64script_runtime_t *runtime, const char *value, bool allow_empty)
{
    if (!value || value[0] == '\0') {
        return allow_empty ? NULL : "";
    }
    if (keyword_equals(value, "MODE_READWRITE")) {
        return "readwrite";
    }
    if (keyword_equals(value, "MODE_READONLY")) {
        return "readonly";
    }
    if (keyword_equals(value, "MODE_UNLINKED")) {
        return "unlinked";
    }

    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid drive mount mode");
    return NULL;
}

static const char *drive_mode_keyword_to_value(c64script_runtime_t *runtime, const char *value)
{
    if (keyword_equals(value, "MODE_1541")) {
        return "1541";
    }
    if (keyword_equals(value, "MODE_1571")) {
        return "1571";
    }
    if (keyword_equals(value, "MODE_1581")) {
        return "1581";
    }

    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid drive mode");
    return NULL;
}

static const char *drive_property_keyword_to_key(c64script_runtime_t *runtime, const char *value)
{
    if (keyword_equals(value, "PROP_ENABLED")) {
        return "enabled";
    }
    if (keyword_equals(value, "PROP_BUS_ID")) {
        return "bus_id";
    }
    if (keyword_equals(value, "PROP_TYPE")) {
        return "type";
    }
    if (keyword_equals(value, "PROP_ROM")) {
        return "rom";
    }
    if (keyword_equals(value, "PROP_IMAGE_FILE")) {
        return "image_file";
    }
    if (keyword_equals(value, "PROP_IMAGE_PATH")) {
        return "image_path";
    }

    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid drive property");
    return NULL;
}

bool c64script_dispatch_drives(c64script_runtime_t *runtime, const c64script_instruction_t *instr)
{
    switch (instr->opcode) {
    case OP_DRIVE_GET: {
        c64script_value_t property;
        c64script_value_t drive;
        if (!c64script_runtime_pop(runtime, &property) || !c64script_runtime_pop(runtime, &drive)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&property);
            c64script_value_free(&drive);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &drive, "DRIVE$") || !require_string(runtime, &property, "DRIVE$")) {
            c64script_value_free(&property);
            c64script_value_free(&drive);
            return false;
        }

        const char *drive_id = NULL;
        if (!drive_keyword_to_id(runtime, drive.as.string, &drive_id, false)) {
            c64script_value_free(&property);
            c64script_value_free(&drive);
            return false;
        }
        const char *prop_key = drive_property_keyword_to_key(runtime, property.as.string);
        if (!prop_key) {
            c64script_value_free(&property);
            c64script_value_free(&drive);
            return false;
        }

        char value[256] = {0};
        if (!c64_rest_drive_get_property((c64_rest_client_t *)runtime->rest_client, drive_id, prop_key, value,
                                         sizeof(value))) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE$ failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&property);
            c64script_value_free(&drive);
            return false;
        }

        c64script_value_t result_val = c64script_value_string(value);
        if (!c64script_runtime_push(runtime, result_val)) {
            c64script_value_free(&result_val);
            c64script_value_free(&property);
            c64script_value_free(&drive);
            return false;
        }
        c64script_value_free(&property);
        c64script_value_free(&drive);
        break;
    }

    case OP_DRIVE_MOUNT: {
        c64script_value_t mode_val;
        c64script_value_t type_val;
        c64script_value_t image_val;
        c64script_value_t drive_val;
        if (!c64script_runtime_pop(runtime, &mode_val) || !c64script_runtime_pop(runtime, &type_val) ||
            !c64script_runtime_pop(runtime, &image_val) || !c64script_runtime_pop(runtime, &drive_val)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&mode_val);
            c64script_value_free(&type_val);
            c64script_value_free(&image_val);
            c64script_value_free(&drive_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &drive_val, "DRIVE_MOUNT") ||
            !require_string(runtime, &image_val, "DRIVE_MOUNT") || !require_string(runtime, &type_val, "DRIVE_MOUNT") ||
            !require_string(runtime, &mode_val, "DRIVE_MOUNT")) {
            c64script_value_free(&mode_val);
            c64script_value_free(&type_val);
            c64script_value_free(&image_val);
            c64script_value_free(&drive_val);
            return false;
        }

        const char *drive_id = NULL;
        if (!drive_keyword_to_id(runtime, drive_val.as.string, &drive_id, true)) {
            c64script_value_free(&mode_val);
            c64script_value_free(&type_val);
            c64script_value_free(&image_val);
            c64script_value_free(&drive_val);
            return false;
        }

        const char *type = drive_type_keyword_to_value(runtime, type_val.as.string, true);
        if (!type && type_val.as.string && type_val.as.string[0]) {
            c64script_value_free(&mode_val);
            c64script_value_free(&type_val);
            c64script_value_free(&image_val);
            c64script_value_free(&drive_val);
            return false;
        }

        const char *mode = drive_mount_mode_keyword_to_value(runtime, mode_val.as.string, true);
        if (!mode && mode_val.as.string && mode_val.as.string[0]) {
            c64script_value_free(&mode_val);
            c64script_value_free(&type_val);
            c64script_value_free(&image_val);
            c64script_value_free(&drive_val);
            return false;
        }

        const char *ext = file_extension_lower(image_val.as.string);
        if (!type || type[0] == '\0') {
            if (strcasecmp(ext, "d64") == 0) {
                type = "d64";
            } else if (strcasecmp(ext, "g64") == 0) {
                type = "g64";
            } else if (strcasecmp(ext, "d71") == 0) {
                type = "d71";
            } else if (strcasecmp(ext, "g71") == 0) {
                type = "g71";
            } else if (strcasecmp(ext, "d81") == 0) {
                type = "d81";
            } else {
                type = "d64";
            }
        }

        if (!mode || mode[0] == '\0') {
            mode = "readwrite";
        }

        const char *c64u_path = NULL;
        bool ok = false;
        if (is_c64u_path(image_val.as.string, &c64u_path)) {
            ok = c64_rest_drive_mount_image((c64_rest_client_t *)runtime->rest_client, drive_id, c64u_path, type, mode);
        } else {
            uint8_t *data = NULL;
            size_t size = 0;
            char err[256] = {0};
            if (!load_binary_file(image_val.as.string, &data, &size, err, sizeof(err))) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to load disk");
                c64script_value_free(&mode_val);
                c64script_value_free(&type_val);
                c64script_value_free(&image_val);
                c64script_value_free(&drive_val);
                return false;
            }
            ok = c64_rest_drive_mount_upload((c64_rest_client_t *)runtime->rest_client, drive_id, type, mode, data,
                                             size);
            free(data);
        }

        if (!ok) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE_MOUNT failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&mode_val);
            c64script_value_free(&type_val);
            c64script_value_free(&image_val);
            c64script_value_free(&drive_val);
            return false;
        }

        c64script_value_free(&mode_val);
        c64script_value_free(&type_val);
        c64script_value_free(&image_val);
        c64script_value_free(&drive_val);
        break;
    }

    case OP_DRIVE_UNMOUNT: {
        c64script_value_t drive_val;
        if (!c64script_runtime_pop(runtime, &drive_val)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&drive_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &drive_val, "DRIVE_UNMOUNT")) {
            c64script_value_free(&drive_val);
            return false;
        }
        const char *drive_id = NULL;
        if (!drive_keyword_to_id(runtime, drive_val.as.string, &drive_id, false)) {
            c64script_value_free(&drive_val);
            return false;
        }
        if (!c64_rest_drive_unmount((c64_rest_client_t *)runtime->rest_client, drive_id)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE_UNMOUNT failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&drive_val);
            return false;
        }
        c64script_value_free(&drive_val);
        break;
    }

    case OP_DRIVE_RESET: {
        c64script_value_t drive_val;
        if (!c64script_runtime_pop(runtime, &drive_val)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&drive_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &drive_val, "DRIVE_RESET")) {
            c64script_value_free(&drive_val);
            return false;
        }
        const char *drive_id = NULL;
        if (!drive_keyword_to_id(runtime, drive_val.as.string, &drive_id, false)) {
            c64script_value_free(&drive_val);
            return false;
        }
        if (!c64_rest_drive_reset((c64_rest_client_t *)runtime->rest_client, drive_id)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE_RESET failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&drive_val);
            return false;
        }
        c64script_value_free(&drive_val);
        break;
    }

    case OP_DRIVE_ON: {
        c64script_value_t drive_val;
        if (!c64script_runtime_pop(runtime, &drive_val)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&drive_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &drive_val, "DRIVE_ON")) {
            c64script_value_free(&drive_val);
            return false;
        }
        const char *drive_id = NULL;
        if (!drive_keyword_to_id(runtime, drive_val.as.string, &drive_id, false)) {
            c64script_value_free(&drive_val);
            return false;
        }
        if (!c64_rest_drive_on((c64_rest_client_t *)runtime->rest_client, drive_id)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE_ON failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&drive_val);
            return false;
        }
        c64script_value_free(&drive_val);
        break;
    }

    case OP_DRIVE_OFF: {
        c64script_value_t drive_val;
        if (!c64script_runtime_pop(runtime, &drive_val)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&drive_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &drive_val, "DRIVE_OFF")) {
            c64script_value_free(&drive_val);
            return false;
        }
        const char *drive_id = NULL;
        if (!drive_keyword_to_id(runtime, drive_val.as.string, &drive_id, false)) {
            c64script_value_free(&drive_val);
            return false;
        }
        if (!c64_rest_drive_off((c64_rest_client_t *)runtime->rest_client, drive_id)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE_OFF failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&drive_val);
            return false;
        }
        c64script_value_free(&drive_val);
        break;
    }

    case OP_DRIVE_ROM: {
        c64script_value_t file_val;
        c64script_value_t drive_val;
        if (!c64script_runtime_pop(runtime, &file_val) || !c64script_runtime_pop(runtime, &drive_val)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&file_val);
            c64script_value_free(&drive_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &drive_val, "DRIVE_ROM") || !require_string(runtime, &file_val, "DRIVE_ROM")) {
            c64script_value_free(&file_val);
            c64script_value_free(&drive_val);
            return false;
        }
        const char *drive_id = NULL;
        if (!drive_keyword_to_id(runtime, drive_val.as.string, &drive_id, true)) {
            c64script_value_free(&file_val);
            c64script_value_free(&drive_val);
            return false;
        }
        if (strcmp(drive_id, "softiec") == 0) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE_ROM supports drive A or drive B only");
            c64script_value_free(&file_val);
            c64script_value_free(&drive_val);
            return false;
        }

        const char *c64u_path = NULL;
        bool ok = false;
        if (is_c64u_path(file_val.as.string, &c64u_path)) {
            ok = c64_rest_drive_load_rom_image((c64_rest_client_t *)runtime->rest_client, drive_id, c64u_path);
        } else {
            uint8_t *data = NULL;
            size_t size = 0;
            char err[256] = {0};
            if (!load_binary_file(file_val.as.string, &data, &size, err, sizeof(err))) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to load ROM");
                c64script_value_free(&file_val);
                c64script_value_free(&drive_val);
                return false;
            }
            ok = c64_rest_drive_load_rom_upload((c64_rest_client_t *)runtime->rest_client, drive_id, data, size);
            free(data);
        }

        if (!ok) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE_ROM failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&file_val);
            c64script_value_free(&drive_val);
            return false;
        }

        c64script_value_free(&file_val);
        c64script_value_free(&drive_val);
        break;
    }

    case OP_DRIVE_MODE: {
        c64script_value_t mode_val;
        c64script_value_t drive_val;
        if (!c64script_runtime_pop(runtime, &mode_val) || !c64script_runtime_pop(runtime, &drive_val)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&mode_val);
            c64script_value_free(&drive_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &drive_val, "DRIVE_MODE") || !require_string(runtime, &mode_val, "DRIVE_MODE")) {
            c64script_value_free(&mode_val);
            c64script_value_free(&drive_val);
            return false;
        }
        const char *drive_id = NULL;
        if (!drive_keyword_to_id(runtime, drive_val.as.string, &drive_id, false)) {
            c64script_value_free(&mode_val);
            c64script_value_free(&drive_val);
            return false;
        }
        if (strcmp(drive_id, "softiec") == 0) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE_MODE supports drive A or drive B only");
            c64script_value_free(&mode_val);
            c64script_value_free(&drive_val);
            return false;
        }
        const char *mode = drive_mode_keyword_to_value(runtime, mode_val.as.string);
        if (!mode) {
            c64script_value_free(&mode_val);
            c64script_value_free(&drive_val);
            return false;
        }
        if (!c64_rest_drive_set_mode((c64_rest_client_t *)runtime->rest_client, drive_id, mode)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE_MODE failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&mode_val);
            c64script_value_free(&drive_val);
            return false;
        }
        c64script_value_free(&mode_val);
        c64script_value_free(&drive_val);
        break;
    }

    case OP_DRIVE_BUS_ID: {
        c64script_value_t bus_id;
        c64script_value_t drive_val;
        if (!c64script_runtime_pop(runtime, &bus_id) || !c64script_runtime_pop(runtime, &drive_val)) {
            return false;
        }
        if (!runtime->rest_client) {
            c64script_value_free(&bus_id);
            c64script_value_free(&drive_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!require_string(runtime, &drive_val, "DRIVE_BUS_ID") || !require_number(runtime, &bus_id, "DRIVE_BUS_ID")) {
            c64script_value_free(&bus_id);
            c64script_value_free(&drive_val);
            return false;
        }

        const char *drive_id = NULL;
        if (!drive_keyword_to_id(runtime, drive_val.as.string, &drive_id, false)) {
            c64script_value_free(&bus_id);
            c64script_value_free(&drive_val);
            return false;
        }
        if (strcmp(drive_id, "softiec") == 0) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DRIVE_BUS_ID supports drive A or drive B only");
            c64script_value_free(&bus_id);
            c64script_value_free(&drive_val);
            return false;
        }

        int bus_id_value = 0;
        if (!number_to_int(runtime, &bus_id, &bus_id_value, "DRIVE_BUS_ID")) {
            c64script_value_free(&bus_id);
            c64script_value_free(&drive_val);
            return false;
        }

        const char *category = keyword_equals(drive_val.as.string, "DRIVE_A") ? "Drive A Settings" : "Drive B Settings";
        char value_buf[16];
        snprintf(value_buf, sizeof(value_buf), "%d", bus_id_value);
        bool ok = c64script_dispatch_set_config_value(runtime, category, "Drive Bus ID", value_buf, "DRIVE_BUS_ID");
        c64script_value_free(&bus_id);
        c64script_value_free(&drive_val);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_LOAD: {
        c64script_value_t device_val;
        c64script_value_t filename;
        if (instr->operand != 0) {
            if (!c64script_runtime_pop(runtime, &device_val) || !c64script_runtime_pop(runtime, &filename)) {
                return false;
            }
        } else {
            if (!c64script_runtime_pop(runtime, &filename)) {
                return false;
            }
            device_val = c64script_value_number(8.0);
        }
        if (!require_string(runtime, &filename, "LOAD")) {
            c64script_value_free(&filename);
            if (instr->operand != 0) {
                c64script_value_free(&device_val);
            }
            return false;
        }
        if (instr->operand != 0 && !require_number(runtime, &device_val, "LOAD")) {
            c64script_value_free(&filename);
            c64script_value_free(&device_val);
            return false;
        }
        if (!runtime->keyboard) {
            c64script_value_free(&filename);
            if (instr->operand != 0) {
                c64script_value_free(&device_val);
            }
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Keyboard not available");
            return false;
        }

        int device_num = 8;
        if (instr->operand != 0) {
            if (!number_to_int(runtime, &device_val, &device_num, "LOAD")) {
                c64script_value_free(&filename);
                c64script_value_free(&device_val);
                return false;
            }
        }

        c64_output_t output = {0};
        output.mode = C64_OUTPUT_TEXT;
        snprintf(output.data.text, sizeof(output.data.text), "LOAD\"%s\",%d,1\r", filename.as.string, device_num);
        if (!c64script_queue_keyboard_output(runtime, &output)) {
            c64script_value_free(&filename);
            if (instr->operand != 0) {
                c64script_value_free(&device_val);
            }
            return false;
        }

        c64script_value_free(&filename);
        if (instr->operand != 0) {
            c64script_value_free(&device_val);
        }
        break;
    }

    case OP_RUN: {
        uint32_t flags = instr->operand;
        c64script_value_t device_val = {0};
        c64script_value_t filename = {0};

        if (flags & 1u) {
            if (flags & 2u) {
                if (!c64script_runtime_pop(runtime, &device_val) || !c64script_runtime_pop(runtime, &filename)) {
                    return false;
                }
            } else {
                if (!c64script_runtime_pop(runtime, &filename)) {
                    return false;
                }
                device_val = c64script_value_number(8.0);
            }

            if (!require_string(runtime, &filename, "RUN")) {
                c64script_value_free(&filename);
                if (flags & 2u) {
                    c64script_value_free(&device_val);
                }
                return false;
            }
            if (flags & 2u && !require_number(runtime, &device_val, "RUN")) {
                c64script_value_free(&filename);
                c64script_value_free(&device_val);
                return false;
            }
        }

        if (!runtime->keyboard) {
            if (flags & 1u) {
                c64script_value_free(&filename);
                if (flags & 2u) {
                    c64script_value_free(&device_val);
                }
            }
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Keyboard not available");
            return false;
        }

        c64_output_t output = {0};
        output.mode = C64_OUTPUT_TEXT;
        if (flags & 1u) {
            int device_num = 8;
            if (flags & 2u) {
                if (!number_to_int(runtime, &device_val, &device_num, "RUN")) {
                    c64script_value_free(&filename);
                    c64script_value_free(&device_val);
                    return false;
                }
            }
            snprintf(output.data.text, sizeof(output.data.text), "LOAD\"%s\",%d,1\rRUN\r", filename.as.string,
                     device_num);
        } else {
            snprintf(output.data.text, sizeof(output.data.text), "RUN\r");
        }
        if (!c64script_queue_keyboard_output(runtime, &output)) {
            if (flags & 1u) {
                c64script_value_free(&filename);
                if (flags & 2u) {
                    c64script_value_free(&device_val);
                }
            }
            return false;
        }

        if (flags & 1u) {
            c64script_value_free(&filename);
            if (flags & 2u) {
                c64script_value_free(&device_val);
            }
        }
        break;
    }

    case OP_SYS: {
        c64script_value_t address;
        if (!c64script_runtime_pop(runtime, &address)) {
            return false;
        }
        if (!require_number(runtime, &address, "SYS")) {
            c64script_value_free(&address);
            return false;
        }
        if (!runtime->keyboard) {
            c64script_value_free(&address);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Keyboard not available");
            return false;
        }

        uint16_t addr = 0;
        if (!number_to_uint16(runtime, &address, &addr, "SYS")) {
            c64script_value_free(&address);
            return false;
        }

        c64_output_t output = {0};
        output.mode = C64_OUTPUT_TEXT;
        snprintf(output.data.text, sizeof(output.data.text), "SYS %u\r", (unsigned)addr);
        if (!c64script_queue_keyboard_output(runtime, &output)) {
            c64script_value_free(&address);
            return false;
        }

        c64script_value_free(&address);
        break;
    }

    default:
        return false;
    }

    return true;
}
