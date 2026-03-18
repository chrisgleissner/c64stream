/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-dispatch-machine.h"

#include "c64-keyboard.h"
#include "c64-logging.h"
#include "c64-rest-client.h"

#include <obs-module.h>
#include <stdint.h>
#include <string.h>
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

    case OP_RECORDSTART:
#ifdef ENABLE_FRONTEND_API
        obs_queue_task(OBS_TASK_UI, c64_script_recording_start_task, NULL, false);
#else
        if (c64script_debug_logging_enabled()) {
            blog(LOG_DEBUG, "[c64script] RECORDSTART: OBS frontend API not enabled, skipping");
        }
#endif
        break;

    case OP_RECORDSTOP:
#ifdef ENABLE_FRONTEND_API
        obs_queue_task(OBS_TASK_UI, c64_script_recording_stop_task, NULL, false);
#else
        if (c64script_debug_logging_enabled()) {
            blog(LOG_DEBUG, "[c64script] RECORDSTOP: OBS frontend API not enabled, skipping");
        }
#endif
        break;

    default:
        return false;
    }

    return true;
}
