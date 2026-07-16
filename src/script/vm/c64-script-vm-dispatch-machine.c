/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-dispatch-machine.h"

#include "device/c64-device.h"
#include "device/c64-device-scan.h"
#include "c64-keyboard.h"
#include "c64-logging.h"
#include "c64-rest-client.h"
#include "c64-script-vm-internal.h"
#include "c64-source.h"

#include <obs-module.h>
#include <stdint.h>
#include <string.h>
#include <util/platform.h>
#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#ifdef ENABLE_FRONTEND_API
static void c64_script_recording_start_task(void *data)
{
    (void)data;
    if (!obs_frontend_recording_active()) {
        obs_frontend_recording_start();
    }
}

static void c64_script_recording_stop_task(void *data)
{
    (void)data;
    if (obs_frontend_recording_active()) {
        obs_frontend_recording_stop();
    }
}

static void c64_script_recording_query_task(void *data)
{
    bool *active = (bool *)data;
    if (!active) {
        return;
    }

    *active = obs_frontend_recording_active();
}

static bool c64_script_wait_for_recording_state(bool expected_active, char *error_msg, size_t error_size)
{
    const uint64_t deadline_ns = os_gettime_ns() + 10000000000ull;
    while (os_gettime_ns() < deadline_ns) {
        bool active = false;
        obs_queue_task(OBS_TASK_UI, c64_script_recording_query_task, &active, true);
        if (active == expected_active) {
            return true;
        }
        os_sleep_ms(20);
    }

    if (error_msg && error_size > 0) {
        snprintf(error_msg, error_size, "Timed out waiting for OBS recording state");
    }
    return false;
}
#endif

bool c64script_dispatch_machine(c64script_runtime_t *runtime, const c64script_instruction_t *instr)
{
    switch (instr->opcode) {
    case OP_PLAYSID: {
        c64script_value_t song_nr, sid_file;
        if (!c64script_runtime_pop(runtime, &song_nr) || !c64script_runtime_pop(runtime, &sid_file))
            return false;
        if (!runtime->rest_client) {
            c64script_value_free(&sid_file);
            c64script_value_free(&song_nr);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (sid_file.type != VALUE_STRING || song_nr.type != VALUE_NUMBER) {
            c64script_value_free(&sid_file);
            c64script_value_free(&song_nr);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (PLAYSID)");
            return false;
        }

        const char *c64u_path = NULL;
        bool ok = false;
        if (is_c64u_path(sid_file.as.string, &c64u_path)) {
            ok = c64_rest_play_sid_path((c64_rest_client_t *)runtime->rest_client, c64u_path, (int)song_nr.as.number);
        } else {
            uint8_t *data = NULL;
            size_t size = 0;
            char err[256] = {0};
            if (!load_binary_file(sid_file.as.string, &data, &size, err, sizeof(err))) {
                c64script_value_free(&sid_file);
                c64script_value_free(&song_nr);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to load SID");
                return false;
            }
            ok = c64_rest_play_sid((c64_rest_client_t *)runtime->rest_client, data, size, (int)song_nr.as.number, NULL,
                                   0);
            free(data);
        }
        if (!ok) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "PLAYSID failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&sid_file);
            c64script_value_free(&song_nr);
            return false;
        }
        c64script_value_free(&sid_file);
        c64script_value_free(&song_nr);
        break;
    }

    case OP_RUNPRG: {
        c64script_value_t prg_file;
        if (!c64script_runtime_pop(runtime, &prg_file))
            return false;
        if (!runtime->rest_client) {
            c64script_value_free(&prg_file);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (prg_file.type != VALUE_STRING) {
            c64script_value_free(&prg_file);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (RUNPRG)");
            return false;
        }

        const char *c64u_path = NULL;
        bool ok = false;
        if (is_c64u_path(prg_file.as.string, &c64u_path)) {
            ok = c64_rest_run_prg_path((c64_rest_client_t *)runtime->rest_client, c64u_path);
        } else {
            uint8_t *data = NULL;
            size_t size = 0;
            char err[256] = {0};
            if (!load_binary_file(prg_file.as.string, &data, &size, err, sizeof(err))) {
                c64script_value_free(&prg_file);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to load PRG");
                return false;
            }
            ok = c64_rest_run_prg((c64_rest_client_t *)runtime->rest_client, data, size);
            free(data);
        }
        if (!ok) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RUNPRG failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&prg_file);
            return false;
        }
        c64script_value_free(&prg_file);
        break;
    }

    case OP_MOUNTDISK: {
        c64script_value_t disk_file;
        if (!c64script_runtime_pop(runtime, &disk_file))
            return false;
        if (!runtime->rest_client) {
            c64script_value_free(&disk_file);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (disk_file.type != VALUE_STRING) {
            c64script_value_free(&disk_file);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (MOUNTDISK)");
            return false;
        }

        const char *c64u_path = NULL;
        bool ok = false;
        if (is_c64u_path(disk_file.as.string, &c64u_path)) {
            ok = c64_rest_mount_disk_path((c64_rest_client_t *)runtime->rest_client, 'a', c64u_path);
        } else {
            const char *ext = file_extension_lower(disk_file.as.string);
            const char *type = NULL;
            if (strcasecmp(ext, "d64") == 0) {
                type = "d64";
            } else if (strcasecmp(ext, "d81") == 0) {
                type = "d81";
            } else {
                c64script_value_free(&disk_file);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Unsupported disk type");
                return false;
            }

            uint8_t *data = NULL;
            size_t size = 0;
            char err[256] = {0};
            if (!load_binary_file(disk_file.as.string, &data, &size, err, sizeof(err))) {
                c64script_value_free(&disk_file);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to load disk");
                return false;
            }
            ok = c64_rest_mount_disk((c64_rest_client_t *)runtime->rest_client, 'a', type, "readonly", data, size);
            free(data);
        }
        if (!ok) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "MOUNTDISK failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            c64script_value_free(&disk_file);
            return false;
        }
        c64script_value_free(&disk_file);
        break;
    }

    case OP_SWITCH_DEVICE: {
        c64script_value_t device_ref;
        if (!c64script_runtime_pop(runtime, &device_ref))
            return false;
        if (device_ref.type != VALUE_STRING) {
            c64script_value_free(&device_ref);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (SWITCH_DEVICE)");
            return false;
        }
        if (!runtime->obs_source) {
            if (c64script_debug_logging_enabled()) {
                blog(LOG_DEBUG, "[c64script] SWITCH_DEVICE: OBS source not available, skipping");
            }
            c64script_value_free(&device_ref);
            break;
        }

        const c64_device_t *device = c64_device_registry_get(device_ref.as.string);
        if (!device) {
            device = c64_device_registry_find_by_host(device_ref.as.string);
        }
        if (!device) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "SWITCH_DEVICE: unknown device '%s'",
                     device_ref.as.string ? device_ref.as.string : "");
            c64script_value_free(&device_ref);
            return false;
        }

        obs_source_t *source = (obs_source_t *)runtime->obs_source;
        bool ok =
            c64_script_queue_source_update(source, C64_SCRIPT_UPDATE_STRING, "c64_device", device->id, 0.0, 0, false);
        c64script_value_free(&device_ref);
        if (!ok) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Failed to queue device switch");
            return false;
        }
        break;
    }

    case OP_DISCOVER_DEVICES: {
        uint16_t port = 0;
        if (instr->operand == 1) {
            c64script_value_t port_value;
            if (!c64script_runtime_pop(runtime, &port_value))
                return false;
            bool port_ok = number_to_uint16(runtime, &port_value, &port, "DISCOVER_DEVICES");
            c64script_value_free(&port_value);
            if (!port_ok) {
                return false;
            }
        }
        if (!runtime->source_data) {
            if (c64script_debug_logging_enabled()) {
                blog(LOG_DEBUG, "[c64script] DISCOVER_DEVICES: OBS source not available, skipping");
            }
            break;
        }
        if (!c64_device_scan_sync((struct c64_source *)runtime->source_data, port)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "DISCOVER_DEVICES failed");
            return false;
        }
        break;
    }

    case OP_RESET:
        if (!runtime->rest_client) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!c64_rest_reset((c64_rest_client_t *)runtime->rest_client)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RESET failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            return false;
        }
        break;

    case OP_REBOOT:
        if (!runtime->rest_client) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!c64_rest_reboot((c64_rest_client_t *)runtime->rest_client)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REBOOT failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            return false;
        }
        break;

    case OP_PAUSE:
        if (!runtime->rest_client) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!c64_rest_pause((c64_rest_client_t *)runtime->rest_client)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "PAUSE failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            return false;
        }
        break;

    case OP_RESUME:
        if (!runtime->rest_client) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!c64_rest_resume((c64_rest_client_t *)runtime->rest_client)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "RESUME failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            return false;
        }
        break;

    case OP_POWEROFF:
        if (!runtime->rest_client) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "REST client not available");
            return false;
        }
        if (!c64_rest_poweroff((c64_rest_client_t *)runtime->rest_client)) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "POWEROFF failed: %s",
                     c64_rest_get_error((c64_rest_client_t *)runtime->rest_client));
            return false;
        }
        break;

    case OP_OBS_SCREENSHOT: {
        c64script_value_t output_path;
        if (!c64script_runtime_pop(runtime, &output_path)) {
            return false;
        }
        if (output_path.type != VALUE_STRING) {
            c64script_value_free(&output_path);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (OBS SCREENSHOT PATH)");
            return false;
        }
        if (!runtime->source_data) {
            c64script_value_free(&output_path);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "OBS source context not available");
            return false;
        }

        char resolved_path[1024];
        if (!c64script_resolve_script_path(runtime, output_path.as.string, resolved_path, sizeof(resolved_path))) {
            c64script_value_free(&output_path);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Screenshot path too long");
            return false;
        }

        const bool preview = instr->operand == (uint32_t)C64SCRIPT_OBS_TARGET_PREVIEW;
        bool ok = c64_source_script_take_frontend_screenshot((struct c64_source *)runtime->source_data, preview,
                                                             resolved_path, runtime->error_msg,
                                                             sizeof(runtime->error_msg));
        c64script_value_free(&output_path);
        if (!ok) {
            return false;
        }
        break;
    }

    case OP_OBS_WAIT_FRAMES: {
        c64script_value_t frame_count;
        if (!c64script_runtime_pop(runtime, &frame_count)) {
            return false;
        }
        if (!require_number(runtime, &frame_count, "OBS WAIT FRAMES")) {
            c64script_value_free(&frame_count);
            return false;
        }

        int frames = 0;
        if (!number_to_int(runtime, &frame_count, &frames, "OBS WAIT FRAMES")) {
            c64script_value_free(&frame_count);
            return false;
        }
        c64script_value_free(&frame_count);

        if (frames < 0) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY (OBS WAIT FRAMES)");
            return false;
        }
        if (!runtime->source_data) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "OBS source context not available");
            return false;
        }

        if (!c64_source_script_wait_rendered_frames((struct c64_source *)runtime->source_data, (uint32_t)frames,
                                                    runtime->error_msg, sizeof(runtime->error_msg))) {
            return false;
        }
        break;
    }

    case OP_OBS_RECORDING_START:
#ifdef ENABLE_FRONTEND_API
        obs_queue_task(OBS_TASK_UI, c64_script_recording_start_task, NULL, true);
        if (!c64_script_wait_for_recording_state(true, runtime->error_msg, sizeof(runtime->error_msg))) {
            return false;
        }
#else
        // Non-frontend test builds still need to execute script fixtures that exercise recording control.
        // Treat recording state changes as successful no-ops when frontend APIs are unavailable.
        (void)runtime;
#endif
        break;

    case OP_OBS_RECORDING_STOP:
#ifdef ENABLE_FRONTEND_API
        obs_queue_task(OBS_TASK_UI, c64_script_recording_stop_task, NULL, true);
        if (!c64_script_wait_for_recording_state(false, runtime->error_msg, sizeof(runtime->error_msg))) {
            return false;
        }
#else
        (void)runtime;
#endif
        break;

    default:
        return false;
    }

    return true;
}
