/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-properties.h"
#include "c64-types.h"
#include "c64-version.h"
#include "c64-network.h"
#include "c64-protocol.h"
#include "c64-video.h"
#include "c64-logging.h"
#include "c64-file.h"
#include "c64-effect.h"
#include "c64-source.h"
#include "c64-palette.h"
#include "device/c64-device.h"
#include "c64-color.h"
#include "c64-keyboard.h"
#include "c64-playlist-window.h"
#include "c64-properties-refresh.h"
#include "c64-rest-client.h"
#include "c64-script-executor.h"
#include "c64-automation.h"
#include "c64-automation-playlist.h"
#include <obs-module.h>
#include <util/platform.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>

// Cross-platform strcasecmp and S_ISDIR
#ifdef _WIN32
#define strcasecmp _stricmp
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#endif
#include <util/dstr.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

#define PROPS_LOG_PREFIX "[props] "
#define PLAYLIST_UI_LIMIT 50
#define PLAYLIST_WINDOW_PREV_VALUE -2
#define PLAYLIST_WINDOW_NEXT_VALUE -3

typedef struct {
    int file_system;
    int playback_source;
    bool shuffle;
    bool include_subfolders;
    char path[C64_AUTOMATION_PATH_MAX];
    bool valid;
} c64_playlist_fingerprint_t;

// Forward declaration of callbacks
static bool crt_preset_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool config_import_path_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool config_export_path_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static void trim_config_string(char *str);

// Forward declaration of path helper functions
static void c64_default_palette_import_path(char *path, size_t path_size);
static void c64_default_palette_export_path(char *path, size_t path_size);

// Palette callbacks
static bool palette_changed(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool palette_import_path_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool palette_export_path_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool palette_delete_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool palette_color_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static void update_palette_color_properties(obs_data_t *settings);

// Script automation callbacks
static bool script_start_stop_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool script_pause_resume_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool script_step_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool script_log_variables_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool script_reload_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static const char *c64_script_basename(const char *path);

// Content automation callbacks
static bool automation_start_stop_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool automation_next_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool automation_refresh_playlist_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool playlist_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool automation_settings_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool automation_runtime_settings_changed(obs_properties_t *props, obs_property_t *property,
                                                obs_data_t *settings);
static void update_playlist_property(obs_property_t *prop, struct c64_source *context, obs_data_t *settings);
static bool refresh_playlist_from_settings(obs_properties_t *props, obs_data_t *settings, const char *reason,
                                           obs_property_t *property, bool force_rebuild);
static bool request_playlist_rebuild(obs_properties_t *props, obs_data_t *settings, const char *reason,
                                     obs_property_t *property, bool force_rebuild);
static bool file_source_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool playback_source_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);

// Reset controls
static bool reset_plugin_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool reset_c64u_clicked(obs_properties_t *props, obs_property_t *property, void *data);

static const char *script_status_to_text(c64_script_status_t status)
{
    switch (status) {
    case C64_SCRIPT_STATUS_IDLE:
        return "IDLE";
    case C64_SCRIPT_STATUS_RUNNING:
        return "RUNNING";
    case C64_SCRIPT_STATUS_PAUSED:
        return "PAUSED";
    case C64_SCRIPT_STATUS_WAITING:
        return "WAITING";
    case C64_SCRIPT_STATUS_ERROR:
        return "ERROR";
    case C64_SCRIPT_STATUS_COMPLETED:
        return "COMPLETED";
    default:
        return "UNKNOWN";
    }
}

static void update_script_status_property(obs_property_t *prop, struct c64_source *context)
{
    if (!prop || !context) {
        return;
    }

    char status[2048] = {0};

    // Convert nanosecond timestamps to local time strings
    char start_time_str[32] = "Never";
    char end_time_str[32] = "N/A";

    if (context->script_start_time > 0) {
        time_t start_sec = context->script_start_time / 1000000000;
        struct tm *tm_info = localtime(&start_sec);
        strftime(start_time_str, sizeof(start_time_str), "%H:%M:%S", tm_info);
    }

    if (context->script_end_time > 0) {
        time_t end_sec = context->script_end_time / 1000000000;
        struct tm *tm_info = localtime(&end_sec);
        strftime(end_time_str, sizeof(end_time_str), "%H:%M:%S", tm_info);
    }

    // Calculate elapsed time
    uint64_t elapsed_ms = 0;
    if (context->script_start_time > 0) {
        if (context->script_end_time > 0) {
            elapsed_ms = (context->script_end_time - context->script_start_time) / 1000000;
        } else {
            uint64_t now = os_gettime_ns();
            elapsed_ms = (now - context->script_start_time) / 1000000;
        }
    }

    // Get current script status if executor exists
    bool is_running = false;
    bool is_paused = false;
    const char *error_msg = NULL;
    int paused_line = 0;

    if (context->script_executor) {
        c64_script_status_t st = c64_script_executor_get_status(context->script_executor);
        is_running = (st == C64_SCRIPT_STATUS_RUNNING || st == C64_SCRIPT_STATUS_WAITING);
        is_paused = (st == C64_SCRIPT_STATUS_PAUSED);
        if (st == C64_SCRIPT_STATUS_ERROR) {
            error_msg = c64_script_executor_get_error(context->script_executor);
        }
        if (is_paused) {
            paused_line = c64_script_executor_get_last_executed_line(context->script_executor, NULL, 0);
        }
    }

    const char *script_name = c64_script_basename(context->script_file_path);

    // Format status based on current state
    if (is_running) {
        snprintf(status, sizeof(status), "▶️ Running %s\nStarted: %.64s (%.1fs ago)", script_name, start_time_str,
                 elapsed_ms / 1000.0);
        obs_property_text_set_info_type(prop, OBS_TEXT_INFO_NORMAL);
    } else if (is_paused) {
        if (paused_line > 0) {
            snprintf(status, sizeof(status), "⏸️ Paused %s at line %d\nStarted: %.64s (%.1fs ago)", script_name,
                     paused_line, start_time_str, elapsed_ms / 1000.0);
        } else {
            snprintf(status, sizeof(status), "⏸️ Paused %s\nStarted: %.64s (%.1fs ago)", script_name, start_time_str,
                     elapsed_ms / 1000.0);
        }
        obs_property_text_set_info_type(prop, OBS_TEXT_INFO_NORMAL);
    } else if (context->script_end_time > 0) {
        // Script has ended
        if (context->script_ended_successfully) {
            snprintf(status, sizeof(status), "✅ Completed %s\nStarted: %.64s | Ended: %.64s\nDuration: %.1fs",
                     script_name, start_time_str, end_time_str, elapsed_ms / 1000.0);
            obs_property_text_set_info_type(prop, OBS_TEXT_INFO_NORMAL);
        } else if (error_msg) {
            // Truncate error if too long
            char short_err[256] = {0};
            const char *safe_err = error_msg;
            if (strlen(error_msg) > sizeof(short_err) - 4) {
                strncpy(short_err, error_msg, sizeof(short_err) - 4);
                strcat(short_err, "...");
                safe_err = short_err;
            }
            snprintf(status, sizeof(status),
                     "❌ Error in %s\nStarted: %.64s | Ended: %.64s\nDuration: %.1fs\nError: %.200s", script_name,
                     start_time_str, end_time_str, elapsed_ms / 1000.0, safe_err);
            obs_property_text_set_info_type(prop, OBS_TEXT_INFO_ERROR);
        } else {
            snprintf(status, sizeof(status), "⏹️ Stopped %s\nStarted: %.64s | Ended: %.64s\nDuration: %.1fs",
                     script_name, start_time_str, end_time_str, elapsed_ms / 1000.0);
            obs_property_text_set_info_type(prop, OBS_TEXT_INFO_NORMAL);
        }
    } else if (context->script_start_time > 0) {
        // Script started but no end time recorded (abnormal state)
        snprintf(status, sizeof(status), "⚠️ Script state unclear (%s)\nStarted: %.64s\nNo end time recorded",
                 script_name, start_time_str);
        obs_property_text_set_info_type(prop, OBS_TEXT_INFO_WARNING);
    } else {
        // No script has been run yet
        snprintf(status, sizeof(status), "No script has been run yet");
        obs_property_text_set_info_type(prop, OBS_TEXT_INFO_NORMAL);
    }

    obs_property_set_long_description(prop, status);
}

// Internal key to track initialization state and prevent spurious auto-saves
static const char *C64_PALETTE_INITIALIZING_KEY = "_c64_palette_initializing";

// Internal settings key: used to prevent re-applying presets when reopening the Properties UI.
// (OBS may rebuild the properties view and trigger "modified" callbacks without a real user change.)
static const char *C64_PRESET_LAST_APPLIED_KEY = "crt_preset_last_applied";
static const char *C64_CONFIG_EXPORT_PATH_KEY = "config_export_path";
static const char *C64_CONFIG_IMPORT_PATH_KEY = "config_import_path";
static const char *C64_PALETTE_KEY = "palette";

// Internal key to prevent auto-import/export during properties UI initialization
static const char *C64_CONFIG_INITIALIZING_KEY = "_c64_config_initializing";

// Internal keys to track last processed paths (prevents duplicate processing)
static const char *C64_CONFIG_EXPORT_LAST_KEY = "_c64_config_export_last";
static const char *C64_CONFIG_IMPORT_LAST_KEY = "_c64_config_import_last";
static const char *C64_PALETTE_EXPORT_LAST_KEY = "_c64_palette_export_last";
static const char *C64_PALETTE_IMPORT_LAST_KEY = "_c64_palette_import_last";

typedef struct {
    obs_source_t *source;
} c64_properties_refresh_t;

static const char *c64_script_basename(const char *path)
{
    if (!path || path[0] == '\0') {
        return "(none)";
    }

    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *base = path;
    if (slash && backslash) {
        base = (slash > backslash) ? (slash + 1) : (backslash + 1);
    } else if (slash) {
        base = slash + 1;
    } else if (backslash) {
        base = backslash + 1;
    }

    return base[0] != '\0' ? base : "(none)";
}

static void c64_apply_properties_refresh(void *data)
{
    c64_properties_refresh_t *refresh = (c64_properties_refresh_t *)data;
    if (!refresh || !refresh->source) {
        if (refresh) {
            free(refresh);
        }
        return;
    }

    obs_source_update_properties(refresh->source);
    obs_source_release(refresh->source);
    free(refresh);
}

static void c64_queue_properties_refresh(struct c64_source *context)
{
    if (!context || !context->source) {
        return;
    }

    c64_properties_refresh_t *refresh = calloc(1, sizeof(c64_properties_refresh_t));
    if (!refresh) {
        return;
    }

    refresh->source = obs_source_get_ref(context->source);
    if (!refresh->source) {
        free(refresh);
        return;
    }

    obs_queue_task(OBS_TASK_UI, c64_apply_properties_refresh, refresh, false);
}

// Helpers: When enforce is true (CI), apply both default and direct values
static inline void c64_set_string(obs_data_t *settings, const char *key, const char *value, bool enforce)
{
    if (enforce) {
        obs_data_set_default_string(settings, key, value);
        obs_data_set_string(settings, key, value);
    } else {
        obs_data_set_default_string(settings, key, value);
    }
}

static inline void c64_set_int(obs_data_t *settings, const char *key, int value, bool enforce)
{
    if (enforce) {
        obs_data_set_default_int(settings, key, value);
        obs_data_set_int(settings, key, value);
    } else {
        obs_data_set_default_int(settings, key, value);
    }
}

static inline void c64_set_bool(obs_data_t *settings, const char *key, bool value, bool enforce)
{
    if (enforce) {
        obs_data_set_default_bool(settings, key, value);
        obs_data_set_bool(settings, key, value);
    } else {
        obs_data_set_default_bool(settings, key, value);
    }
}

// Script automation button callbacks
static bool script_start_stop_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    C64_LOG_INFO("Script start/stop button clicked");

    struct c64_source *context = (struct c64_source *)data;
    if (!context) {
        C64_LOG_ERROR("Script start/stop: null context");
        return false;
    }

    if (!context->source) {
        C64_LOG_ERROR("Script start/stop: null source");
        return false;
    }

    // Get current status
    c64_script_status_t status = C64_SCRIPT_STATUS_IDLE;
    if (context->script_executor) {
        status = c64_script_executor_get_status(context->script_executor);
        C64_LOG_INFO("Script executor exists with status: %d", (int)status);
    } else {
        C64_LOG_INFO("No script executor exists yet");
    }

    bool executor_running = context->script_executor && c64_script_executor_is_running(context->script_executor);

    // If running or paused, stop it
    if (status == C64_SCRIPT_STATUS_RUNNING || status == C64_SCRIPT_STATUS_PAUSED ||
        status == C64_SCRIPT_STATUS_WAITING || executor_running) {
        C64_LOG_INFO("Stopping script...");
        if (context->script_executor) {
            c64_script_executor_stop(context->script_executor);
            context->script_end_time = os_gettime_ns();
            context->script_ended_successfully = false; // User stopped it
            C64_LOG_INFO("Script stopped by user");
            c64_properties_mark_script_ui_refresh(context);
        } else {
            C64_LOG_ERROR("Script executor is null, cannot stop");
        }
    } else {
        // Otherwise, start the script
        if (context->script_file_path[0] == '\0') {
            C64_LOG_WARNING("No script file selected");
            return false;
        }

        C64_LOG_INFO("Starting script: %s", context->script_file_path);

        // Create executor if needed
        if (!context->script_executor) {
            C64_LOG_INFO("Creating script executor...");
            context->script_executor = c64_script_executor_create(context->source, context);
            if (!context->script_executor) {
                C64_LOG_ERROR("Failed to create script executor");
                return false;
            }
            C64_LOG_INFO("Script executor created successfully");
        }

        // Start execution
        C64_LOG_INFO("Calling c64_script_executor_start...");
        context->script_start_time = os_gettime_ns();
        context->script_end_time = 0;
        context->last_script_status = C64_SCRIPT_STATUS_IDLE;
        if (c64_script_executor_start(context->script_executor, context->script_file_path)) {
            C64_LOG_INFO("Script started: %s", context->script_file_path);
            context->last_script_status = C64_SCRIPT_STATUS_RUNNING;
            c64_properties_mark_script_ui_refresh(context);
        } else {
            const char *err = c64_script_executor_get_error(context->script_executor);
            C64_LOG_ERROR("Failed to start script: %s", err ? err : "unknown error");
            context->script_end_time = os_gettime_ns();
            context->script_ended_successfully = false;
            c64_properties_mark_script_ui_refresh(context);
        }
    }

    C64_LOG_INFO("Script start/stop handler complete");
    c64_queue_properties_refresh(context);
    return false;
}

static bool script_pause_resume_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context) {
        return false;
    }

    c64_script_status_t status = C64_SCRIPT_STATUS_IDLE;
    if (context->script_executor) {
        status = c64_script_executor_get_status(context->script_executor);
    }

    if (status == C64_SCRIPT_STATUS_RUNNING || status == C64_SCRIPT_STATUS_WAITING) {
        c64_script_executor_pause(context->script_executor);
        C64_LOG_INFO("Script paused");
        c64_properties_mark_script_ui_refresh(context);
    } else if (status == C64_SCRIPT_STATUS_PAUSED) {
        c64_script_executor_resume(context->script_executor);
        C64_LOG_INFO("Script resumed");
        c64_properties_mark_script_ui_refresh(context);
    } else {
        if (context->script_file_path[0] == '\0') {
            C64_LOG_WARNING("No script file selected");
            return false;
        }

        if (!context->script_executor) {
            context->script_executor = c64_script_executor_create(context->source, context);
            if (!context->script_executor) {
                C64_LOG_ERROR("Failed to create script executor");
                return false;
            }
        }

        context->script_start_time = os_gettime_ns();
        context->script_end_time = 0;
        context->last_script_status = C64_SCRIPT_STATUS_IDLE;
        if (c64_script_executor_start_debug(context->script_executor, context->script_file_path)) {
            C64_LOG_INFO("Script started in debug mode: %s", context->script_file_path);
            context->last_script_status = C64_SCRIPT_STATUS_PAUSED;
            c64_properties_mark_script_ui_refresh(context);
        } else {
            const char *err = c64_script_executor_get_error(context->script_executor);
            C64_LOG_ERROR("Failed to start script in debug mode: %s", err ? err : "unknown error");
            context->script_end_time = os_gettime_ns();
            context->script_ended_successfully = false;
            c64_properties_mark_script_ui_refresh(context);
        }
    }

    c64_queue_properties_refresh(context);
    return false;
}

static bool script_step_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context || !context->script_executor) {
        return false;
    }

    if (c64_script_executor_step(context->script_executor)) {
        C64_LOG_INFO("Stepping to next line");
        c64_properties_mark_script_ui_refresh(context);
    }

    c64_queue_properties_refresh(context);
    return false;
}

static bool script_log_variables_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context || !context->script_executor) {
        C64_LOG_INFO("No script executor available");
        return false;
    }

    c64_script_executor_log_variables(context->script_executor);
    return false; // No need to refresh properties
}

static bool script_reload_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context) {
        return false;
    }

    if (context->script_file_path[0] == '\0') {
        C64_LOG_WARNING("No script file selected");
        return false;
    }

    if (!context->script_executor) {
        context->script_executor = c64_script_executor_create(context->source, context);
        if (!context->script_executor) {
            C64_LOG_ERROR("Failed to create script executor");
            return false;
        }
    }

    bool ok = c64_script_executor_validate_file(context->script_executor, context->script_file_path);
    if (ok) {
        C64_LOG_INFO("Script validated: %s", context->script_file_path);
    } else {
        const char *err = c64_script_executor_get_error(context->script_executor);
        C64_LOG_ERROR("Script validation failed: %s", err ? err : "unknown error");
    }

    return true; // Refresh properties (status field)
}

static bool reset_plugin_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context) {
        return false;
    }

    C64_LOG_INFO("🔄 Resetting plugin properties to defaults...");

    // Get current settings object
    obs_data_t *settings = obs_source_get_settings(context->source);
    if (!settings) {
        C64_LOG_ERROR("Failed to get settings object");
        return false;
    }

    // Clear all current settings
    obs_data_clear(settings);

    // Reload defaults from properties.ini
    c64_load_configuration(settings);

    // Apply the reset settings to the source
    obs_source_update(context->source, settings);
    obs_data_release(settings);

    C64_LOG_INFO("✅ Plugin properties reset to defaults");

    return true; // Refresh properties UI
}

static bool reset_c64u_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context) {
        return false;
    }

    C64_LOG_INFO("🔄 Sending hard reset to C64U...");

    // Send hard reset (reboot) to C64U
    if (context->rest_client) {
        if (c64_rest_reboot(context->rest_client)) {
            C64_LOG_INFO("✅ C64U hard reset completed");
        } else {
            C64_LOG_WARNING("⚠️ Failed to send hard reset to C64U");
        }
    } else {
        C64_LOG_WARNING("⚠️ REST client not available, cannot reset C64U");
    }

    return false; // No UI refresh needed
}

// File system changed callback - update path property visibility
static bool file_source_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    int file_system = (int)obs_data_get_int(settings, "file_system");
    int playback_source = (int)obs_data_get_int(settings, "playback_source");
    bool use_songlengths = obs_data_get_bool(settings, "automation_use_songlengths");

    obs_property_t *playback_source_prop = obs_properties_get(props, "playback_source");
    if (file_system == 1) {
        if (playback_source != 0) {
            playback_source = 0;
            obs_data_set_int(settings, "playback_source", playback_source);
        }
        if (playback_source_prop) {
            obs_property_set_enabled(playback_source_prop, false);
        }
    } else if (playback_source_prop) {
        obs_property_set_enabled(playback_source_prop, true);
    }

    // Show/hide path properties based on file_system and playback_source combination
    obs_property_t *local_file_prop = obs_properties_get(props, "local_file_path");
    obs_property_t *local_folder_prop = obs_properties_get(props, "local_folder_path");
    obs_property_t *c64u_file_prop = obs_properties_get(props, "c64u_file_path");
    obs_property_t *c64u_folder_prop = obs_properties_get(props, "c64u_folder_path");

    if (local_file_prop) {
        obs_property_set_visible(local_file_prop, file_system == 0 && playback_source == 0);
    }
    if (local_folder_prop) {
        obs_property_set_visible(local_folder_prop, file_system == 0 && playback_source == 1);
    }
    if (c64u_file_prop) {
        obs_property_set_visible(c64u_file_prop, file_system == 1 && playback_source == 0);
    }
    if (c64u_folder_prop) {
        obs_property_set_visible(c64u_folder_prop, file_system == 1 && playback_source == 1);
    }

    // Show/hide folder options
    obs_property_t *subfolders_prop = obs_properties_get(props, "include_subfolders");
    obs_property_t *shuffle_prop = obs_properties_get(props, "shuffle_playback");
    if (subfolders_prop) {
        obs_property_set_visible(subfolders_prop, playback_source == 1);
    }
    if (shuffle_prop) {
        obs_property_set_visible(shuffle_prop, playback_source == 1);
    }

    obs_property_t *songlengths_path_prop = obs_properties_get(props, "automation_songlengths_path");
    if (songlengths_path_prop) {
        obs_property_set_visible(songlengths_path_prop, file_system == 0 && use_songlengths);
    }

    struct c64_source *context = obs_properties_get_param(props);
    if (context && context->playlist_properties_initializing) {
        return true;
    }

    if (context && !c64_properties_should_request_playlist_rebuild(context)) {
        return true;
    }

    return request_playlist_rebuild(props, settings, "file_source_changed", property, false);
}

// Playback source changed callback - update path property visibility and folder options
static bool playback_source_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    // Trigger same visibility logic as file_system change
    return file_source_changed(props, property, settings);
}

static bool build_automation_config_from_settings(obs_data_t *settings, c64_automation_config_t *config,
                                                  const char **out_path)
{
    if (!settings || !config) {
        return false;
    }

    int file_system = (int)obs_data_get_int(settings, "file_system");
    int playback_source = (int)obs_data_get_int(settings, "playback_source");

    const char *path = NULL;
    if (file_system == 0 && playback_source == 0) {
        path = obs_data_get_string(settings, "local_file_path");
    } else if (file_system == 0 && playback_source == 1) {
        path = obs_data_get_string(settings, "local_folder_path");
    } else if (file_system == 1 && playback_source == 0) {
        path = obs_data_get_string(settings, "c64u_file_path");
    } else if (file_system == 1 && playback_source == 1) {
        path = obs_data_get_string(settings, "c64u_folder_path");
    }

    if (!path || path[0] == '\0') {
        return false;
    }

    memset(config, 0, sizeof(*config));
    config->mode = (playback_source == 0) ? C64_AUTO_MODE_SINGLE : C64_AUTO_MODE_FOLDER;
    config->file_source = (file_system == 0) ? C64_FILE_SOURCE_LOCAL : C64_FILE_SOURCE_C64U;
    strncpy(config->folder_path, path, sizeof(config->folder_path) - 1);
    config->shuffle = obs_data_get_bool(settings, "shuffle_playback");
    config->include_subfolders = obs_data_get_bool(settings, "include_subfolders");
    config->duration_seconds = (int)obs_data_get_int(settings, "automation_duration");
    if (config->duration_seconds < 1) {
        config->duration_seconds = 1;
    }
    config->reset_between_items = obs_data_get_bool(settings, "automation_reset");
    config->use_songlengths = obs_data_get_bool(settings, "automation_use_songlengths");
    const char *songlengths_override = obs_data_get_string(settings, "automation_songlengths_path");
    if (songlengths_override && songlengths_override[0] != '\0') {
        strncpy(config->songlengths_path, songlengths_override, sizeof(config->songlengths_path) - 1);
    }
    strncpy(config->d64_autostart_template, "LOAD\"*\",8,1\rRUN\r", sizeof(config->d64_autostart_template) - 1);

    if (out_path) {
        *out_path = path;
    }

    return true;
}

static const char *get_playlist_path_from_settings(obs_data_t *settings, int file_system, int playback_source)
{
    if (!settings) {
        return NULL;
    }

    if (file_system == 0 && playback_source == 0) {
        return obs_data_get_string(settings, "local_file_path");
    }
    if (file_system == 0 && playback_source == 1) {
        return obs_data_get_string(settings, "local_folder_path");
    }
    if (file_system == 1 && playback_source == 0) {
        return obs_data_get_string(settings, "c64u_file_path");
    }
    if (file_system == 1 && playback_source == 1) {
        return obs_data_get_string(settings, "c64u_folder_path");
    }

    return NULL;
}

static void playlist_fingerprint_from_settings(obs_data_t *settings, c64_playlist_fingerprint_t *fingerprint)
{
    if (!fingerprint) {
        return;
    }

    memset(fingerprint, 0, sizeof(*fingerprint));
    fingerprint->file_system = settings ? (int)obs_data_get_int(settings, "file_system") : 0;
    fingerprint->playback_source = settings ? (int)obs_data_get_int(settings, "playback_source") : 0;
    fingerprint->shuffle = settings ? obs_data_get_bool(settings, "shuffle_playback") : false;
    fingerprint->include_subfolders = settings ? obs_data_get_bool(settings, "include_subfolders") : false;
    fingerprint->valid = false;

    if (!settings) {
        return;
    }

    c64_automation_config_t config = {0};
    const char *config_path = NULL;
    if (build_automation_config_from_settings(settings, &config, &config_path)) {
        fingerprint->file_system = (config.file_source == C64_FILE_SOURCE_LOCAL) ? 0 : 1;
        fingerprint->playback_source = (config.mode == C64_AUTO_MODE_SINGLE) ? 0 : 1;
        fingerprint->shuffle = config.shuffle;
        fingerprint->include_subfolders = config.include_subfolders;
        if (config_path) {
            strncpy(fingerprint->path, config_path, sizeof(fingerprint->path) - 1);
            fingerprint->path[sizeof(fingerprint->path) - 1] = '\0';
        }
        fingerprint->valid = true;
        return;
    }

    const char *path =
        get_playlist_path_from_settings(settings, fingerprint->file_system, fingerprint->playback_source);
    if (path) {
        strncpy(fingerprint->path, path, sizeof(fingerprint->path) - 1);
        fingerprint->path[sizeof(fingerprint->path) - 1] = '\0';
    }
}

static bool playlist_fingerprint_matches_context(struct c64_source *context,
                                                 const c64_playlist_fingerprint_t *fingerprint)
{
    if (!context || !fingerprint || !context->playlist_fingerprint_valid || !fingerprint->valid) {
        return false;
    }

    if (context->playlist_last_file_system != fingerprint->file_system ||
        context->playlist_last_playback_source != fingerprint->playback_source ||
        context->playlist_last_shuffle != fingerprint->shuffle ||
        context->playlist_last_include_subfolders != fingerprint->include_subfolders) {
        return false;
    }

    return strncmp(context->playlist_last_path, fingerprint->path, sizeof(context->playlist_last_path)) == 0;
}

static void playlist_fingerprint_store(struct c64_source *context, const c64_playlist_fingerprint_t *fingerprint)
{
    if (!context || !fingerprint || !fingerprint->valid) {
        return;
    }

    context->playlist_last_file_system = fingerprint->file_system;
    context->playlist_last_playback_source = fingerprint->playback_source;
    context->playlist_last_shuffle = fingerprint->shuffle;
    context->playlist_last_include_subfolders = fingerprint->include_subfolders;
    strncpy(context->playlist_last_path, fingerprint->path, sizeof(context->playlist_last_path) - 1);
    context->playlist_last_path[sizeof(context->playlist_last_path) - 1] = '\0';
    context->playlist_fingerprint_valid = true;
}

static bool playlist_copy_selected_path(struct c64_source *context, obs_data_t *settings, char *path, size_t path_size)
{
    if (!context || !context->automation || !settings || !path || path_size == 0) {
        return false;
    }

    int selected_index = (int)obs_data_get_int(settings, "playlist");
    if (selected_index < 0) {
        return false;
    }

    const char *selected_path = c64_automation_get_playlist_item(context->automation, selected_index);
    if (!selected_path || selected_path[0] == '\0') {
        return false;
    }

    strncpy(path, selected_path, path_size - 1);
    path[path_size - 1] = '\0';
    return true;
}

static int playlist_find_index_by_path(struct c64_source *context, const char *path)
{
    if (!context || !context->automation || !path || path[0] == '\0') {
        return -1;
    }

    int playlist_count = c64_automation_get_playlist_count(context->automation);
    for (int i = 0; i < playlist_count; i++) {
        const char *item_path = c64_automation_get_playlist_item(context->automation, i);
        if (item_path && strcmp(item_path, path) == 0) {
            return i;
        }
    }

    return -1;
}

static bool refresh_playlist_from_settings(obs_properties_t *props, obs_data_t *settings, const char *reason,
                                           obs_property_t *property, bool force_rebuild)
{
    if (!props || !settings) {
        return false;
    }

    struct c64_source *context = obs_properties_get_param(props);
    if (!context) {
        return false;
    }

    if (context->playlist_ui_update_in_progress) {
        C64_LOG_DEBUG(PROPS_LOG_PREFIX "Playlist rebuild skipped (re-entrant): reason=%s", reason ? reason : "unknown");
        return false;
    }

    bool was_in_progress = context->playlist_ui_update_in_progress;
    context->playlist_ui_update_in_progress = true;

    const char *property_name = property ? obs_property_name(property) : "(none)";
    c64_playlist_fingerprint_t fingerprint = {0};
    playlist_fingerprint_from_settings(settings, &fingerprint);

    int previous_selected_index = (int)obs_data_get_int(settings, "playlist");
    char previous_selected_path[C64_AUTOMATION_PATH_MAX] = {0};
    bool has_previous_path =
        playlist_copy_selected_path(context, settings, previous_selected_path, sizeof(previous_selected_path));
    int previous_playlist_count = context->automation ? c64_automation_get_playlist_count(context->automation) : 0;

    if (!context->automation) {
        if (!context->rest_client) {
            C64_LOG_DEBUG(PROPS_LOG_PREFIX
                          "Playlist rebuild skipped (no automation/rest client): reason=%s property=%s",
                          reason ? reason : "unknown", property_name);
            context->playlist_ui_update_in_progress = was_in_progress;
            return false;
        }
        context->automation = c64_automation_create(context->rest_client, context->keyboard, context->source);
        if (!context->automation) {
            C64_LOG_ERROR(PROPS_LOG_PREFIX "Failed to create automation engine for playlist refresh");
            context->playlist_ui_update_in_progress = was_in_progress;
            return false;
        }
    }

    if (c64_automation_is_running(context->automation)) {
        c64_automation_config_t runtime_config = {0};
        runtime_config.duration_seconds = (int)obs_data_get_int(settings, "automation_duration");
        runtime_config.reset_between_items = obs_data_get_bool(settings, "automation_reset");
        runtime_config.use_songlengths = obs_data_get_bool(settings, "automation_use_songlengths");
        const char *runtime_songlengths = obs_data_get_string(settings, "automation_songlengths_path");
        if (runtime_songlengths && runtime_songlengths[0] != '\0') {
            strncpy(runtime_config.songlengths_path, runtime_songlengths, sizeof(runtime_config.songlengths_path) - 1);
        }
        c64_automation_update_runtime_config(context->automation, &runtime_config);

        obs_property_t *playlist_prop = obs_properties_get(props, "playlist");
        if (playlist_prop) {
            update_playlist_property(playlist_prop, context, settings);
        }

        const char *songlengths_path = c64_automation_get_songlengths_path(context->automation);
        if (songlengths_path && songlengths_path[0] != '\0') {
            const char *existing = obs_data_get_string(settings, "automation_songlengths_path");
            if (!existing || existing[0] == '\0') {
                obs_data_set_string(settings, "automation_songlengths_path", songlengths_path);
            }
        }

        C64_LOG_DEBUG(PROPS_LOG_PREFIX
                      "Playlist rebuild skipped (automation running): reason=%s property=%s selected=%d count=%d",
                      reason ? reason : "unknown", property_name, previous_selected_index, previous_playlist_count);
        context->playlist_ui_update_in_progress = was_in_progress;
        return true;
    }

    c64_automation_config_t config = {0};
    if (!build_automation_config_from_settings(settings, &config, NULL)) {
        c64_automation_clear_playlist(context->automation);
        obs_property_t *playlist_prop = obs_properties_get(props, "playlist");
        if (playlist_prop) {
            update_playlist_property(playlist_prop, context, settings);
        }

        context->playlist_window_start = 0;
        context->playlist_window_follow_selection = true;
        context->playlist_fingerprint_valid = false;
        C64_LOG_DEBUG(PROPS_LOG_PREFIX "Playlist rebuild skipped (invalid config): reason=%s property=%s path=%s",
                      reason ? reason : "unknown", property_name, fingerprint.path[0] ? fingerprint.path : "(empty)");
        context->playlist_ui_update_in_progress = was_in_progress;
        return true;
    }

    // If an async preload is already in progress, never block the UI thread with a synchronous
    // filesystem scan.  Show whatever the cache already holds and let the preload completion
    // trigger a UI refresh via c64_automation_queue_ui_update.
    if (c64_automation_is_preloading(context->automation)) {
        obs_property_t *playlist_prop = obs_properties_get(props, "playlist");
        if (playlist_prop) {
            update_playlist_property(playlist_prop, context, settings);
        }
        C64_LOG_DEBUG(PROPS_LOG_PREFIX "Playlist rebuild deferred (preload in progress): reason=%s",
                      reason ? reason : "unknown");
        context->playlist_ui_update_in_progress = was_in_progress;
        return true;
    }

    int selected_index = previous_selected_index;
    bool fingerprint_same = playlist_fingerprint_matches_context(context, &fingerprint);
    bool should_rebuild = force_rebuild || !context->playlist_fingerprint_valid || !fingerprint_same ||
                          previous_playlist_count == 0;

    // Pass the caller's force_rebuild flag rather than the derived should_rebuild flag.
    // The automation layer has its own rebuild logic (playlist_config_equals, playlist_ready,
    // etc.) that avoids a redundant filesystem scan when an async preload already built a
    // matching list.  Escalating fingerprint-staleness into force_rebuild=true would re-scan
    // thousands of files on the UI thread unnecessarily.
    bool refreshed = c64_automation_refresh_playlist(context->automation, &config, selected_index, force_rebuild);
    if (!refreshed) {
        c64_automation_clear_playlist(context->automation);
    } else {
        playlist_fingerprint_store(context, &fingerprint);
    }

    bool selection_preserved = (selected_index == previous_selected_index);
    if (refreshed && has_previous_path) {
        int preserved_index = playlist_find_index_by_path(context, previous_selected_path);
        if (preserved_index >= 0) {
            selected_index = preserved_index;
            selection_preserved = true;
        }
    }

    int playlist_count = context->automation ? c64_automation_get_playlist_count(context->automation) : 0;
    if (playlist_count > 0) {
        if (selected_index < 0 || selected_index >= playlist_count) {
            selected_index = 0;
            selection_preserved = false;
        }
        context->playlist_window_start =
            c64_playlist_clamp_window_start(playlist_count, PLAYLIST_UI_LIMIT, selected_index);
        context->playlist_window_follow_selection = true;
        if (selected_index != previous_selected_index) {
            obs_data_set_int(settings, "playlist", selected_index);
        }
        (void)c64_automation_set_current_index(context->automation, selected_index);
    } else {
        context->playlist_window_start = 0;
        context->playlist_window_follow_selection = true;
    }

    obs_property_t *playlist_prop = obs_properties_get(props, "playlist");
    if (playlist_prop) {
        update_playlist_property(playlist_prop, context, settings);
    }

    const char *songlengths_path = c64_automation_get_songlengths_path(context->automation);
    if (songlengths_path && songlengths_path[0] != '\0') {
        const char *existing = obs_data_get_string(settings, "automation_songlengths_path");
        if (!existing || existing[0] == '\0') {
            obs_data_set_string(settings, "automation_songlengths_path", songlengths_path);
        }
    }

    C64_LOG_DEBUG(PROPS_LOG_PREFIX "Playlist rebuild %s: reason=%s property=%s count=%d selected=%d preserved=%d",
                  should_rebuild ? "completed" : "skipped", reason ? reason : "unknown", property_name, playlist_count,
                  selected_index, selection_preserved ? 1 : 0);

    context->playlist_ui_update_in_progress = was_in_progress;
    return refreshed;
}

static bool request_playlist_rebuild(obs_properties_t *props, obs_data_t *settings, const char *reason,
                                     obs_property_t *property, bool force_rebuild)
{
    if (!props || !settings) {
        return false;
    }

    struct c64_source *context = obs_properties_get_param(props);
    const char *property_name = property ? obs_property_name(property) : "(none)";
    c64_playlist_fingerprint_t fingerprint = {0};
    playlist_fingerprint_from_settings(settings, &fingerprint);

    if (context && context->playlist_fingerprint_valid) {
        C64_LOG_DEBUG(PROPS_LOG_PREFIX
                      "Playlist rebuild previous: file_system=%d playback_source=%d path=%s shuffle=%d recurse=%d",
                      context->playlist_last_file_system, context->playlist_last_playback_source,
                      context->playlist_last_path[0] ? context->playlist_last_path : "(empty)",
                      context->playlist_last_shuffle ? 1 : 0, context->playlist_last_include_subfolders ? 1 : 0);
    }

    C64_LOG_DEBUG(PROPS_LOG_PREFIX
                  "Playlist rebuild requested: reason=%s property=%s file_system=%d playback_source=%d path=%s "
                  "shuffle=%d recurse=%d force=%d valid=%d",
                  reason ? reason : "unknown", property_name, fingerprint.file_system, fingerprint.playback_source,
                  fingerprint.path[0] ? fingerprint.path : "(empty)", fingerprint.shuffle ? 1 : 0,
                  fingerprint.include_subfolders ? 1 : 0, force_rebuild ? 1 : 0, fingerprint.valid ? 1 : 0);

    return refresh_playlist_from_settings(props, settings, reason, property, force_rebuild);
}

// Content automation button callbacks
static bool automation_start_stop_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    struct c64_source *context = (struct c64_source *)data;
    if (!context) {
        return false;
    }

    // Check if automation is running
    bool is_running = context->automation && c64_automation_is_running(context->automation);

    if (is_running) {
        // Stop automation
        c64_automation_stop(context->automation);
        C64_LOG_INFO("Content automation stopped by user");
    } else {
        // Start automation - validate configuration
        obs_data_t *settings = obs_source_get_settings(context->source);
        if (!settings) {
            return false;
        }

        c64_automation_config_t config = {0};
        if (!build_automation_config_from_settings(settings, &config, NULL)) {
            C64_LOG_WARNING("Invalid or missing file/folder path for automation");
            (void)request_playlist_rebuild(props, settings, "start_button_invalid", property, true);
            obs_data_release(settings);
            return false;
        }

        if (!context->automation) {
            context->automation = c64_automation_create(context->rest_client, context->keyboard, context->source);
            if (!context->automation) {
                C64_LOG_ERROR("Failed to create automation engine");
                obs_data_release(settings);
                return false;
            }
        }

        c64_automation_configure(context->automation, &config);

        bool refreshed = request_playlist_rebuild(props, settings, "start_button", property, false);
        if (!refreshed) {
            C64_LOG_WARNING("Playlist refresh failed before starting automation");
        }

        int selected_index = (int)obs_data_get_int(settings, "playlist");
        obs_data_release(settings);

        // Start automation
        if (c64_automation_start(context->automation, selected_index)) {
            C64_LOG_INFO("Content automation started");
        } else {
            C64_LOG_ERROR("Failed to start content automation");
        }
    }

    // Update button state immediately
    bool new_state = context->automation && c64_automation_is_running(context->automation);
    obs_property_t *button_prop = obs_properties_get(props, "automation_start_stop");
    if (button_prop) {
        const char *new_label = new_state ? obs_module_text("AutomationStopContent")
                                          : obs_module_text("AutomationPlayContent");
        const char *new_desc = new_state ? obs_module_text("AutomationStopContent.Description")
                                         : obs_module_text("AutomationPlayContent.Description");
        obs_property_set_description(button_prop, new_label);
        obs_property_set_long_description(button_prop, new_desc);
    }

    // Get current settings to pass to playlist update
    obs_data_t *current_settings = obs_source_get_settings(context->source);

    // Update playlist dropdown
    obs_property_t *playlist_prop = obs_properties_get(props, "playlist");
    if (playlist_prop) {
        update_playlist_property(playlist_prop, context, current_settings);
    }

    obs_data_release(current_settings);

    // Update Next button state
    obs_property_t *next_prop = obs_properties_get(props, "automation_next");
    if (next_prop) {
        obs_property_set_enabled(next_prop, new_state);
    }

    obs_property_t *refresh_prop = obs_properties_get(props, "automation_refresh_playlist");
    if (refresh_prop) {
        obs_property_set_enabled(refresh_prop, !new_state);
    }

    return true; // Refresh properties
}

static bool automation_next_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context || !context->automation) {
        return false;
    }

    if (c64_automation_skip_next(context->automation)) {
        C64_LOG_INFO("Skipped to next item");

        // Small delay to allow worker thread to update current_index
        os_sleep_ms(150);

        // Get current settings
        obs_data_t *settings = obs_source_get_settings(context->source);

        // Update playlist dropdown
        obs_property_t *playlist_prop = obs_properties_get(props, "playlist");
        if (playlist_prop) {
            update_playlist_property(playlist_prop, context, settings);
        }

        obs_data_release(settings);
    }

    return true; // Refresh properties
}

static bool automation_settings_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    const char *property_name = property ? obs_property_name(property) : "(none)";
    C64_LOG_DEBUG(PROPS_LOG_PREFIX "Automation settings changed: property=%s", property_name);

    struct c64_source *context = obs_properties_get_param(props);
    if (context && context->playlist_properties_initializing) {
        return false;
    }

    if (context && !c64_properties_should_request_playlist_rebuild(context)) {
        return false;
    }

    int file_system = (int)obs_data_get_int(settings, "file_system");
    if (file_system == 1 &&
        (strcmp(property_name, "c64u_file_path") == 0 || strcmp(property_name, "c64u_folder_path") == 0)) {
        return false;
    }

    return request_playlist_rebuild(props, settings, "automation_setting_changed", property, false);
}

static bool automation_runtime_settings_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    const char *property_name = property ? obs_property_name(property) : "(none)";
    struct c64_source *context = obs_properties_get_param(props);
    if (!context || !settings) {
        return false;
    }

    bool use_songlengths = obs_data_get_bool(settings, "automation_use_songlengths");
    int file_system = (int)obs_data_get_int(settings, "file_system");
    obs_property_t *songlengths_path_prop = obs_properties_get(props, "automation_songlengths_path");
    if (songlengths_path_prop) {
        obs_property_set_visible(songlengths_path_prop, file_system == 0 && use_songlengths);
    }

    c64_automation_config_t runtime_config = {0};
    runtime_config.duration_seconds = (int)obs_data_get_int(settings, "automation_duration");
    runtime_config.reset_between_items = obs_data_get_bool(settings, "automation_reset");
    runtime_config.use_songlengths = obs_data_get_bool(settings, "automation_use_songlengths");
    const char *songlengths_path = obs_data_get_string(settings, "automation_songlengths_path");
    if (songlengths_path && songlengths_path[0] != '\0') {
        strncpy(runtime_config.songlengths_path, songlengths_path, sizeof(runtime_config.songlengths_path) - 1);
    }
    if (runtime_config.duration_seconds < 1) {
        runtime_config.duration_seconds = 1;
    }

    if (context->automation) {
        c64_automation_update_runtime_config(context->automation, &runtime_config);
    }

    C64_LOG_DEBUG(PROPS_LOG_PREFIX
                  "Automation runtime changed: property=%s duration=%d reset=%d songlengths=%d songlengths_path=%s",
                  property_name, runtime_config.duration_seconds, runtime_config.reset_between_items ? 1 : 0,
                  runtime_config.use_songlengths ? 1 : 0,
                  runtime_config.songlengths_path[0] ? runtime_config.songlengths_path : "(auto)");

    if (songlengths_path_prop && strcmp(property_name, "automation_use_songlengths") == 0) {
        return true;
    }

    return false; // No UI refresh
}

static bool automation_refresh_playlist_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    struct c64_source *context = (struct c64_source *)data;
    if (!context) {
        return false;
    }

    obs_data_t *settings = obs_source_get_settings(context->source);
    if (!settings) {
        return false;
    }

    bool refreshed = request_playlist_rebuild(props, settings, "manual_refresh", property, true);
    obs_data_release(settings);
    return refreshed;
}

static bool playlist_path_is_playable(const char *path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

    const char *last_sep = strrchr(path, '/');
    const char *last_backslash = strrchr(path, '\\');
    if (last_backslash && (!last_sep || last_backslash > last_sep)) {
        last_sep = last_backslash;
    }

    const char *filename = last_sep ? last_sep + 1 : path;
    if (!filename || filename[0] == '\0') {
        return false;
    }

    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return false;
    }

    if (strcasecmp(dot, ".sid") == 0 || strcasecmp(dot, ".mod") == 0 || strcasecmp(dot, ".prg") == 0 ||
        strcasecmp(dot, ".crt") == 0 || strcasecmp(dot, ".d64") == 0 || strcasecmp(dot, ".g64") == 0 ||
        strcasecmp(dot, ".d71") == 0 || strcasecmp(dot, ".g71") == 0 || strcasecmp(dot, ".d81") == 0) {
        return true;
    }

    return false;
}

static bool playlist_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(property);

    struct c64_source *context = obs_properties_get_param(props);
    if (!context) {
        return false;
    }

    if (context->playlist_ui_update_in_progress) {
        C64_LOG_DEBUG(PROPS_LOG_PREFIX "Playlist selection ignored (UI update in progress)");
        return false;
    }

    if (context->playlist_ignore_changes > 0) {
        context->playlist_ignore_changes--;
        C64_LOG_DEBUG(PROPS_LOG_PREFIX "Playlist selection ignored (programmatic update)");
        return false;
    }

    int selected_index = (int)obs_data_get_int(settings, "playlist");
    int previous_index = context->playlist_last_selected_valid ? context->playlist_last_selected_index : -1;
    if (previous_index < 0 && context->automation) {
        previous_index = c64_automation_get_current_index(context->automation);
    }

    int playlist_count = context->automation ? c64_automation_get_playlist_count(context->automation) : 0;
    if (!context->automation || playlist_count <= 0) {
        return false;
    }

    if (selected_index == PLAYLIST_WINDOW_PREV_VALUE || selected_index == PLAYLIST_WINDOW_NEXT_VALUE) {
        int window_delta = (selected_index == PLAYLIST_WINDOW_PREV_VALUE) ? -PLAYLIST_UI_LIMIT : PLAYLIST_UI_LIMIT;
        context->playlist_window_start = c64_playlist_clamp_window_start(playlist_count, PLAYLIST_UI_LIMIT,
                                                                         context->playlist_window_start + window_delta);
        context->playlist_window_follow_selection = false;

        int restored_index = previous_index;
        if (restored_index < 0 || restored_index >= playlist_count) {
            restored_index = 0;
        }
        if (context->playlist_ignore_changes < INT_MAX) {
            context->playlist_ignore_changes++;
        }
        obs_data_set_int(settings, "playlist", restored_index);

        obs_property_t *playlist_prop = obs_properties_get(props, "playlist");
        if (playlist_prop) {
            update_playlist_property(playlist_prop, context, settings);
        }

        C64_LOG_DEBUG(PROPS_LOG_PREFIX "Playlist window moved: start=%d count=%d", context->playlist_window_start,
                      playlist_count);
        return true;
    }

    bool is_running = context->automation && c64_automation_is_running(context->automation);
    int current_index = context->automation ? c64_automation_get_current_index(context->automation) : -1;
    int file_system = settings ? (int)obs_data_get_int(settings, "file_system") : 0;
    const char *source_mode = (file_system == 0) ? "local" : "c64u";
    const char *selected_path =
        context->automation ? c64_automation_get_playlist_item(context->automation, selected_index) : NULL;
    C64_LOG_DEBUG(PROPS_LOG_PREFIX "Playlist selection changed: old=%d new=%d running=%d source=%s path=%s",
                  previous_index, selected_index, is_running ? 1 : 0, source_mode,
                  selected_path ? selected_path : "(none)");

    if (is_running && selected_index == current_index) {
        context->playlist_pending_active = false;
        context->playlist_pending_index = -1;
        return false;
    }

    if (is_running && selected_index >= 0 && selected_index != current_index) {
        context->playlist_pending_active = true;
        context->playlist_pending_index = selected_index;
    } else {
        context->playlist_pending_active = false;
        context->playlist_pending_index = -1;
    }

    bool jumped = false;
    if (context->automation && selected_index >= 0) {
        jumped = c64_automation_jump_to_index(context->automation, selected_index);
        if (jumped && is_running) {
            C64_LOG_INFO(PROPS_LOG_PREFIX "Playback switching to index %d source=%s path=%s", selected_index,
                         source_mode, selected_path ? selected_path : "(unknown)");
        }
    }

    context->playlist_window_follow_selection = true;

    context->playlist_last_selected_index = selected_index;
    context->playlist_last_selected_valid = (selected_index >= 0);

    // Update playlist dropdown to reflect new current index
    obs_property_t *playlist_prop = obs_properties_get(props, "playlist");
    if (playlist_prop) {
        update_playlist_property(playlist_prop, context, settings);
    }

    return true;
}

static void update_playlist_property(obs_property_t *prop, struct c64_source *context, obs_data_t *settings)
{
    if (!prop || !context) {
        return;
    }

    if (!context->automation) {
        obs_property_list_clear(prop);
        obs_property_list_add_int(prop, "(No files)", -1);
        obs_property_set_enabled(prop, false);
        if (settings) {
            obs_data_set_int(settings, "playlist", -1);
        }
        context->playlist_window_start = 0;
        context->playlist_window_follow_selection = true;
        context->playlist_last_selected_index = -1;
        context->playlist_last_selected_valid = false;
        return;
    }

    bool was_in_progress = context->playlist_ui_update_in_progress;
    context->playlist_ui_update_in_progress = true;

    // Clear existing list
    obs_property_list_clear(prop);

    bool is_running = c64_automation_is_running(context->automation);
    bool is_preloading = c64_automation_is_preloading(context->automation);
    int playlist_count = c64_automation_get_playlist_count(context->automation);
    int current_index = c64_automation_get_current_index(context->automation);
    int selected_index = settings ? (int)obs_data_get_int(settings, "playlist") : -1;
    bool pending_active = context->playlist_pending_active;
    int pending_index = context->playlist_pending_index;

    if (!is_running) {
        pending_active = false;
    } else if (pending_active) {
        if (pending_index < 0 || pending_index >= playlist_count) {
            pending_active = false;
        } else if (current_index == pending_index) {
            pending_active = false;
        }
    }

    if (!pending_active) {
        context->playlist_pending_active = false;
        context->playlist_pending_index = -1;
    }

    bool has_pending_selection = pending_active;

    if (playlist_count == 0) {
        obs_property_list_add_int(prop, is_preloading ? "(Loading playlist...)" : "(No files)", -1);
        obs_property_set_enabled(prop, false);
        if (settings) {
            obs_data_set_int(settings, "playlist", -1);
        }
        context->playlist_last_selected_index = -1;
        context->playlist_last_selected_valid = false;
        context->playlist_ui_update_in_progress = was_in_progress;
        return;
    }

    obs_property_set_enabled(prop, true);

    int focus_index = has_pending_selection ? pending_index : (is_running ? current_index : selected_index);
    if (focus_index < 0 || focus_index >= playlist_count) {
        focus_index = (current_index >= 0 && current_index < playlist_count) ? current_index : 0;
    }

    int window_start = 0;
    int window_count = playlist_count;
    bool use_paged_window = !is_running && playlist_count > PLAYLIST_UI_LIMIT;
    if (use_paged_window && !context->playlist_window_follow_selection) {
        window_start =
            c64_playlist_clamp_window_start(playlist_count, PLAYLIST_UI_LIMIT, context->playlist_window_start);
        window_count = PLAYLIST_UI_LIMIT;
        if (window_start + window_count > playlist_count) {
            window_count = playlist_count - window_start;
        }
    } else {
        c64_playlist_compute_window(playlist_count, focus_index, PLAYLIST_UI_LIMIT, &window_start, &window_count);
    }
    context->playlist_window_start = window_start;

    if (use_paged_window && window_start > 0) {
        char label[96];
        snprintf(label, sizeof(label), "... show earlier files (%d-%d of %d)", window_start + 1,
                 window_start + window_count, playlist_count);
        obs_property_list_add_int(prop, label, PLAYLIST_WINDOW_PREV_VALUE);
    }

    for (int i = window_start; i < window_start + window_count; i++) {
        const char *path = c64_automation_get_playlist_item(context->automation, i);
        if (path && playlist_path_is_playable(path)) {
            char display_text[256];
            int written = 0;
            if (is_running && i == current_index) {
                written = snprintf(display_text, sizeof(display_text), "► %d: ", i + 1);
            } else {
                written = snprintf(display_text, sizeof(display_text), "  %d: ", i + 1);
            }
            if (written < 0) {
                display_text[0] = '\0';
            } else {
                size_t remaining = sizeof(display_text) - (size_t)written - 1;
                strncpy(display_text + written, path, remaining);
                display_text[written + remaining] = '\0';
            }

            double song_seconds = 0.0;
            if (c64_automation_get_songlength_seconds(context->automation, path, &song_seconds)) {
                int total_seconds = (int)(song_seconds + 0.5);
                if (total_seconds < 0) {
                    total_seconds = 0;
                }
                char duration_text[32];
                if (total_seconds >= 60) {
                    int minutes = total_seconds / 60;
                    int seconds = total_seconds % 60;
                    snprintf(duration_text, sizeof(duration_text), " (%dm %ds)", minutes, seconds);
                } else {
                    snprintf(duration_text, sizeof(duration_text), " (%ds)", total_seconds);
                }

                size_t len = strlen(display_text);
                size_t available = sizeof(display_text) - len - 1;
                if (available > 0) {
                    strncat(display_text, duration_text, available);
                }
            }

            obs_property_list_add_int(prop, display_text, i);
        }
    }

    if (use_paged_window && (window_start + window_count) < playlist_count) {
        char label[96];
        snprintf(label, sizeof(label), "... show later files (%d-%d of %d)", window_start + 1,
                 window_start + window_count, playlist_count);
        obs_property_list_add_int(prop, label, PLAYLIST_WINDOW_NEXT_VALUE);
    }

    // Set the dropdown to show the currently playing file as selected
    if (settings) {
        int desired_index = has_pending_selection ? pending_index : selected_index;
        if (is_running && !has_pending_selection) {
            desired_index = current_index;
        } else if (selected_index < 0 || selected_index >= playlist_count) {
            desired_index = focus_index;
        }

        int current_setting = (int)obs_data_get_int(settings, "playlist");
        if (current_setting != desired_index) {
            if (context->playlist_ignore_changes < INT_MAX) {
                context->playlist_ignore_changes++;
            }
            obs_data_set_int(settings, "playlist", desired_index);
        }
        context->playlist_last_selected_index = (int)obs_data_get_int(settings, "playlist");
        context->playlist_last_selected_valid = (context->playlist_last_selected_index >= 0);
    }

    context->playlist_ui_update_in_progress = was_in_progress;
}

obs_properties_t *c64_create_properties(void *data)
{
    struct c64_source *context = (struct c64_source *)data;
    obs_properties_t *props = obs_properties_create();
    obs_properties_set_param(props, context, NULL);
    if (context) {
        context->playlist_properties_initializing = true;
    }

    // General Group
    obs_property_t *info_group = obs_properties_add_group(props, "info_group", obs_module_text("PluginInformation"),
                                                          OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *info_props = obs_property_group_content(info_group);

    // Version information (read-only)
    obs_property_t *version_prop =
        obs_properties_add_text(info_props, "version_info", obs_module_text("Version"), OBS_TEXT_INFO);
    obs_property_set_long_description(version_prop, c64_get_build_info());
    obs_property_text_set_info_type(version_prop, OBS_TEXT_INFO_NORMAL);

    // Import/Export Settings
    // Set initialization flag to prevent auto-import/export during properties UI setup
    {
        obs_data_t *init_settings = obs_source_get_settings(context->source);
        obs_data_set_bool(init_settings, C64_CONFIG_INITIALIZING_KEY, true);
        obs_data_release(init_settings);
    }

    // Import settings (INI) - action triggers immediately when file is selected
    obs_property_t *import_path_prop = obs_properties_add_path(info_props, C64_CONFIG_IMPORT_PATH_KEY,
                                                               obs_module_text("ImportSettings"), OBS_PATH_FILE,
                                                               "INI Files (*.ini);;All Files (*.*)", NULL);
    obs_property_set_long_description(import_path_prop, obs_module_text("ImportSettings.Description"));

    // Set default directory path BEFORE registering callback to avoid triggering it
    {
        char settings_dir[512];
        if (c64_get_user_dir(C64_USER_DIR_SETTINGS, settings_dir, sizeof(settings_dir))) {
            obs_data_t *path_settings = obs_source_get_settings(context->source);
            obs_data_set_default_string(path_settings, C64_CONFIG_IMPORT_PATH_KEY, settings_dir);
            obs_data_release(path_settings);
        }
    }

    obs_property_set_modified_callback(import_path_prop, config_import_path_changed);

    // Export settings (INI) - action triggers immediately when destination is selected
    obs_property_t *export_path_prop = obs_properties_add_path(info_props, C64_CONFIG_EXPORT_PATH_KEY,
                                                               obs_module_text("ExportSettings"), OBS_PATH_FILE_SAVE,
                                                               "INI Files (*.ini);;All Files (*.*)", NULL);
    obs_property_set_long_description(export_path_prop, obs_module_text("ExportSettings.Description"));

    // Set default directory path BEFORE registering callback to avoid triggering it
    // Use DIRECTORY (with trailing slash), not filename, to prevent spurious export
    // If we set a filename, the callback might trigger and create the file automatically
    {
        char settings_dir[512];
        if (c64_get_user_dir(C64_USER_DIR_SETTINGS, settings_dir, sizeof(settings_dir))) {
            obs_data_t *path_settings = obs_source_get_settings(context->source);
            obs_data_set_default_string(path_settings, C64_CONFIG_EXPORT_PATH_KEY, settings_dir);
            obs_data_release(path_settings);
        }
    }

    obs_property_set_modified_callback(export_path_prop, config_export_path_changed);

    // Clear initialization flag now that properties UI setup is complete
    {
        obs_data_t *init_settings = obs_source_get_settings(context->source);
        obs_data_erase(init_settings, C64_CONFIG_INITIALIZING_KEY);
        obs_data_release(init_settings);
    }

    UNUSED_PARAMETER(context);

    // Network Group
    obs_property_t *network_group = obs_properties_add_group(
        props, "network_group", obs_module_text("NetworkConfiguration"), OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *network_props = obs_property_group_content(network_group);

    obs_property_t *device_prop =
        obs_properties_add_list(network_props, "c64_device", "Device", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    c64_device_registry_populate_list(device_prop);

    // DNS Server IP
    obs_property_t *dns_prop =
        obs_properties_add_text(network_props, "dns_server_ip", obs_module_text("DNSServerIP"), OBS_TEXT_DEFAULT);
    obs_property_set_long_description(dns_prop, obs_module_text("DNSServerIP.Description"));

    // C64 Ultimate Host (IP Address or Hostname)
    obs_property_t *host_prop =
        obs_properties_add_text(network_props, "c64_host", obs_module_text("C64UHost"), OBS_TEXT_DEFAULT);
    obs_property_set_long_description(host_prop, obs_module_text("C64UHost.Description"));

    // C64 Ultimate Network Password (optional)
    obs_property_t *password_prop =
        obs_properties_add_text(network_props, "c64_password", obs_module_text("C64UPassword"), OBS_TEXT_PASSWORD);
    obs_property_set_long_description(password_prop, obs_module_text("C64UPassword.Description"));

    // OBS IP Address
    obs_property_t *obs_ip_prop =
        obs_properties_add_text(network_props, "obs_ip_address", obs_module_text("OBSMachineIP"), OBS_TEXT_DEFAULT);
    obs_property_set_long_description(obs_ip_prop, obs_module_text("OBSMachineIP.Description"));

    // Auto-detect IP toggle
    obs_property_t *auto_ip_prop =
        obs_properties_add_bool(network_props, "auto_detect_ip", obs_module_text("AutoDetectOBSIP"));
    obs_property_set_long_description(auto_ip_prop, obs_module_text("AutoDetectOBSIP.Description"));

    // UDP Ports
    obs_property_t *video_port_prop =
        obs_properties_add_int(network_props, "video_port", obs_module_text("VideoPort"), 1024, 65535, 1);
    obs_property_set_long_description(video_port_prop, obs_module_text("VideoPort.Description"));

    obs_property_t *audio_port_prop =
        obs_properties_add_int(network_props, "audio_port", obs_module_text("AudioPort"), 1024, 65535, 1);
    obs_property_set_long_description(audio_port_prop, obs_module_text("AudioPort.Description"));

    // Control Port (TCP)
    obs_property_t *control_port_prop =
        obs_properties_add_int(network_props, "control_port", obs_module_text("ControlPort"), 64, 65535, 1);
    obs_property_set_long_description(control_port_prop, obs_module_text("ControlPort.Description"));

    // Buffer Delay
    obs_property_t *delay_prop =
        obs_properties_add_int_slider(network_props, "buffer_delay_ms", obs_module_text("BufferDelay"), 0, 500, 1);
    obs_property_set_long_description(delay_prop, obs_module_text("BufferDelay.Description"));

    // Recording Group
    obs_property_t *recording_group = obs_properties_add_group(props, "recording_group", obs_module_text("Recording"),
                                                               OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *recording_props = obs_property_group_content(recording_group);

    // Output Folder
    obs_property_t *save_folder_prop = obs_properties_add_path(
        recording_props, "save_folder", obs_module_text("OutputFolder"), OBS_PATH_DIRECTORY, NULL, NULL);
    obs_property_set_long_description(save_folder_prop, obs_module_text("OutputFolder.Description"));

    // Record Raw Video (AVI) and Audio (WAV)
    obs_property_t *record_video_prop =
        obs_properties_add_bool(recording_props, "record_video", obs_module_text("RecordRawVideo"));
    obs_property_set_long_description(record_video_prop, obs_module_text("RecordRawVideo.Description"));

    // Record Raw Frames (BMP)
    obs_property_t *record_frames_prop =
        obs_properties_add_bool(recording_props, "record_frames", obs_module_text("RecordRawFrames"));
    obs_property_set_long_description(record_frames_prop, obs_module_text("RecordRawFrames.Description"));

    // Record Network and Streaming Events (CSV)
    obs_property_t *record_csv_prop =
        obs_properties_add_bool(recording_props, "record_csv", obs_module_text("RecordNetworkEvents"));
    obs_property_set_long_description(record_csv_prop, obs_module_text("RecordNetworkEvents.Description"));

    // Record A/V Sync (CSV)
    obs_property_t *record_av_sync_prop =
        obs_properties_add_bool(recording_props, "record_av_sync", obs_module_text("RecordAVSync"));
    obs_property_set_long_description(record_av_sync_prop, obs_module_text("RecordAVSync.Description"));

    // Show Debug Messages in OBS Logs
    obs_property_t *debug_prop =
        obs_properties_add_bool(recording_props, "debug_logging", obs_module_text("ShowDebugMessages"));
    obs_property_set_long_description(debug_prop, obs_module_text("ShowDebugMessages.Description"));

    // Effects Group
    obs_property_t *effects_group = obs_properties_add_group(props, "effects_group", obs_module_text("Effects"),
                                                             OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *effects_props = obs_property_group_content(effects_group);

    // Presets dropdown at the top
    obs_property_t *preset_prop = obs_properties_add_list(effects_props, "crt_preset", obs_module_text("Presets"),
                                                          OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_long_description(preset_prop, obs_module_text("Presets.Description"));

    // Populate presets from the loaded presets file
    c64_effect_populate_list(preset_prop);

    // Add modified callback to apply preset when selected
    obs_property_set_modified_callback(preset_prop, crt_preset_changed);

    obs_property_t *preserve_size_prop =
        obs_properties_add_bool(effects_props, "preserve_size", obs_module_text("PreservePreviewSize"));
    obs_property_set_long_description(preserve_size_prop, obs_module_text("PreservePreviewSize.Description"));

    // Scanlines
    obs_property_t *scanline_distance_prop = obs_properties_add_list(effects_props, "scan_line_distance",
                                                                     obs_module_text("ScanLineDistance"),
                                                                     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_FLOAT);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.None"), 0.0);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.Tight"), 0.25);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.Normal"), 0.50);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.Wide"), 1.0);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.ExtraWide"), 2.0);
    obs_property_set_long_description(scanline_distance_prop, obs_module_text("ScanLineDistance.Description"));

    obs_property_t *scanline_strength_prop = obs_properties_add_float_slider(
        effects_props, "scan_line_strength", obs_module_text("ScanLineStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(scanline_strength_prop, obs_module_text("ScanLineStrength.Description"));

    // Pixel Geometry
    obs_property_t *pixel_width_prop =
        obs_properties_add_float_slider(effects_props, "pixel_width", obs_module_text("PixelWidth"), 0.5, 4.0, 0.1);
    obs_property_set_long_description(pixel_width_prop, obs_module_text("PixelWidth.Description"));

    obs_property_t *pixel_height_prop =
        obs_properties_add_float_slider(effects_props, "pixel_height", obs_module_text("PixelHeight"), 0.5, 4.0, 0.1);
    obs_property_set_long_description(pixel_height_prop, obs_module_text("PixelHeight.Description"));

    obs_property_t *blur_strength_prop = obs_properties_add_float_slider(
        effects_props, "blur_strength", obs_module_text("BlurStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(blur_strength_prop, obs_module_text("BlurStrength.Description"));

    // CRT Bloom
    obs_property_t *bloom_strength_prop = obs_properties_add_float_slider(
        effects_props, "bloom_strength", obs_module_text("BloomStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(bloom_strength_prop, obs_module_text("BloomStrength.Description"));

    // CRT Afterglow
    obs_property_t *afterglow_duration_prop = obs_properties_add_int_slider(
        effects_props, "afterglow_duration_ms", obs_module_text("AfterglowDuration"), 0, 250, 10);
    obs_property_set_long_description(afterglow_duration_prop, obs_module_text("AfterglowDuration.Description"));

    obs_property_t *afterglow_curve_prop = obs_properties_add_list(
        effects_props, "afterglow_curve", obs_module_text("AfterglowCurve"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.InstantFade"), 0);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.RapidFade"), 1);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.GradualFade"), 2);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.LongTail"), 3);
    obs_property_set_long_description(afterglow_curve_prop, obs_module_text("AfterglowCurve.Description"));

    // Screen Tint
    obs_property_t *tint_mode_prop = obs_properties_add_list(effects_props, "tint_mode", obs_module_text("TintMode"),
                                                             OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.None"), 0);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.Amber"), 1);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.Green"), 2);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.Monochrome"), 3);
    obs_property_set_long_description(tint_mode_prop, obs_module_text("TintMode.Description"));

    obs_property_t *tint_strength_prop = obs_properties_add_float_slider(
        effects_props, "tint_strength", obs_module_text("TintStrength"), 0.0, 1.0, 0.05);
    // ═══════════════════════════════════════════════════════════════════════════
    // Palette Group (placed AFTER Effects - effects are more commonly used)
    // ═══════════════════════════════════════════════════════════════════════════
    obs_property_t *palette_group = obs_properties_add_group(props, "palette_group", obs_module_text("Palette"),
                                                             OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *palette_props = obs_property_group_content(palette_group);

    // Note: We do NOT auto-populate export/import paths here because setting values
    // triggers the modified callbacks, which would auto-export palettes on properties open.
    // Instead, let the user specify paths when they want to import/export.

    // Palette dropdown
    obs_property_t *palette_prop = obs_properties_add_list(palette_props, C64_PALETTE_KEY,
                                                           obs_module_text("PaletteSelection"), OBS_COMBO_TYPE_LIST,
                                                           OBS_COMBO_FORMAT_STRING);

    // Set description based on currently active palette
    const char *active_id = c64_palette_get_active_id();
    const char *palette_desc = active_id ? c64_palette_get_description(active_id) : NULL;
    if (palette_desc) {
        obs_property_set_long_description(palette_prop, palette_desc);
    } else {
        obs_property_set_long_description(palette_prop, obs_module_text("PaletteSelection.Description"));
    }

    // Validate filesystem before populating (removes stale entries, falls back to Default if active palette missing)
    // Get fresh settings reference since previous one was released
    obs_data_t *palette_settings = obs_source_get_settings(context->source);
    c64_palette_validate_filesystem(palette_settings);

    // If validation cleared stale palette data and set to Default, we need to explicitly
    // overwrite ANY stale color values that might be in the settings to prevent
    // palette recreation when those stale values trigger color_changed callbacks
    const char *current_palette = obs_data_get_string(palette_settings, "palette");
    if (current_palette && strcmp(current_palette, "Default") == 0) {
        // Get Default palette colors from working colors (which is Default after validation)
        uint32_t *default_colors = c64_palette_get_working_colors();

        if (default_colors) {
            // Explicitly set ACTUAL color values (not just defaults) to overwrite any stale values
            for (int i = 0; i < 16; i++) {
                char key[32];
                snprintf(key, sizeof(key), "palette_color_%d", i);

                // Convert BGRA to OBS color format
                uint32_t obs_color = c64_bgra_to_obs_color(default_colors[i]);

                // Use set_int to overwrite stale actual values
                obs_data_set_int(palette_settings, key, (long long)obs_color);
            }
        }
    }

    // Apply settings to source immediately to persist changes and prevent stale data
    // This ensures any palette ID or color changes from validation are saved before color pickers display
    obs_source_update(context->source, palette_settings);
    obs_source_save(context->source);

    // Set initialization flag to prevent auto-save during properties UI setup
    // This prevents spurious "Default (Custom)" palette creation when properties dialog opens
    obs_data_set_bool(palette_settings, C64_PALETTE_INITIALIZING_KEY, true);

    // Manually trigger palette_changed with validated settings BEFORE setting the callback
    // This ensures the correct palette is loaded before OBS can fire callbacks with stale data
    // Without this, OBS may call palette_changed with the old palette name from settings,
    // triggering auto-save and recreating deleted palettes
    palette_changed(context, palette_props, palette_prop, palette_settings);

    obs_data_release(palette_settings);

    c64_palette_populate_list(palette_prop);
    obs_property_set_modified_callback2(palette_prop, palette_changed, context); // Pass source context for persistence

    // Import path field (opens file dialog)
    obs_property_t *import_path = obs_properties_add_path(palette_props, "palette_import_path",
                                                          obs_module_text("PaletteImport"), OBS_PATH_FILE,
                                                          "VPL Palette Files (*.vpl);;All Files (*.*)", NULL);
    obs_property_set_long_description(import_path, obs_module_text("PaletteImport.Description"));

    // Set default directory path BEFORE registering callback to avoid triggering it
    {
        char palette_dir[512];
        c64_default_palette_import_path(palette_dir, sizeof(palette_dir));
        // Ensure trailing slash to clearly indicate directory (prevents spurious export)
        size_t len = strlen(palette_dir);
        if (len > 0 && len < sizeof(palette_dir) - 2 && palette_dir[len - 1] != '/' && palette_dir[len - 1] != '\\') {
            palette_dir[len] = '/';
            palette_dir[len + 1] = '\0';
        }
        obs_data_t *path_settings = obs_source_get_settings(context->source);
        obs_data_set_default_string(path_settings, "palette_import_path", palette_dir);
        obs_data_release(path_settings);
    }

    obs_property_set_modified_callback(import_path, palette_import_path_changed);

    // Export path field (opens save dialog)
    obs_property_t *export_path = obs_properties_add_path(palette_props, "palette_export_path",
                                                          obs_module_text("PaletteExport"), OBS_PATH_FILE_SAVE,
                                                          "VPL Palette Files (*.vpl);;All Files (*.*)", NULL);
    obs_property_set_long_description(export_path, obs_module_text("PaletteExport.Description"));

    // Set default directory path BEFORE registering callback to avoid triggering it
    {
        char palette_dir[512];
        c64_default_palette_export_path(palette_dir, sizeof(palette_dir));
        // Ensure trailing slash to clearly indicate directory (prevents spurious export)
        size_t len = strlen(palette_dir);
        if (len > 0 && len < sizeof(palette_dir) - 2 && palette_dir[len - 1] != '/' && palette_dir[len - 1] != '\\') {
            palette_dir[len] = '/';
            palette_dir[len + 1] = '\0';
        }
        obs_data_t *path_settings = obs_source_get_settings(context->source);
        obs_data_set_default_string(path_settings, "palette_export_path", palette_dir);
        obs_data_release(path_settings);
    }

    obs_property_set_modified_callback(export_path, palette_export_path_changed);

    // Delete button
    obs_property_t *delete_btn = obs_properties_add_button2(
        palette_props, "palette_delete", obs_module_text("PaletteDelete"), palette_delete_clicked, data);
    // Disable delete button initially (enabled only when custom palette is selected)
    obs_property_set_enabled(delete_btn, false);

    // Collapsible Color Editor group (with arrow to expand/collapse, not checkbox)
    obs_property_t *color_editor_group = obs_properties_add_group(palette_props, "color_editor_group",
                                                                  obs_module_text("PaletteColorEditor"),
                                                                  OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *color_editor_props = obs_property_group_content(color_editor_group);

    // Visual color editor (4x4 grid as 4 rows)
    static const char *color_names[16] = {"Black",   "White",      "Red",       "Cyan",     "Purple", "Green",
                                          "Blue",    "Yellow",     "Orange",    "Brown",    "Pink",   "DarkGrey",
                                          "MedGrey", "LightGreen", "LightBlue", "LightGrey"};
    for (int i = 0; i < 16; i++) {
        char key[32];
        char label[64];
        snprintf(key, sizeof(key), "palette_color_%d", i);
        snprintf(label, sizeof(label), "%d: %.48s", i, color_names[i]);

        obs_property_t *color_prop = obs_properties_add_color(color_editor_props, key, label);
        obs_property_set_modified_callback2(color_prop, palette_color_changed, data);
    }

    // Initialize color editor with current active palette colors
    // This ensures colors are correct when properties dialog first opens
    // Get fresh settings reference since the previous one was released
    obs_data_t *color_settings = obs_source_get_settings(context->source);
    // The initialization flag should still be set from line 315 (same underlying settings data)
    // This protects the update_palette_color_properties call below from triggering spurious auto-saves
    update_palette_color_properties(color_settings);
    // Clear initialization flag now that properties UI setup is complete
    obs_data_erase(color_settings, C64_PALETTE_INITIALIZING_KEY);
    obs_data_release(color_settings);

    obs_property_set_long_description(tint_strength_prop, obs_module_text("TintStrength.Description"));

    // Remote Control group
    obs_property_t *rest_group = obs_properties_add_group(props, "rest_group", obs_module_text("RemoteControl"),
                                                          OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *rest_props = obs_property_group_content(rest_group);

    // Keymap selection - dynamically populated
    obs_property_t *keymap_prop = obs_properties_add_list(
        rest_props, "keyboard_keymap", obs_module_text("KeyboardKeymap"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_long_description(keymap_prop, obs_module_text("KeyboardKeymap.Description"));

    // Discover and populate available keymaps
    char **keymap_paths = NULL;
    size_t keymap_count = 0;
    if (c64_keyboard_discover_keymaps(&keymap_paths, &keymap_count)) {
        // Build display name array for sorting
        typedef struct {
            char display_name[128];
            char *path;
            bool is_default;
        } keymap_entry_t;

        keymap_entry_t *entries = calloc(keymap_count, sizeof(keymap_entry_t));
        if (entries) {
            // Create display names and mark default
            for (size_t i = 0; i < keymap_count; i++) {
                strncpy(entries[i].display_name, keymap_paths[i], sizeof(entries[i].display_name) - 1);
                entries[i].display_name[sizeof(entries[i].display_name) - 1] = '\0';
                entries[i].path = keymap_paths[i];
                entries[i].is_default = (strcmp(keymap_paths[i], "symbolic_us") == 0);

                // Convert underscores to spaces for display
                for (char *p = entries[i].display_name; *p; p++) {
                    if (*p == '_')
                        *p = ' ';
                }
            }

            // Sort alphabetically (case-insensitive), ignoring default entry
            for (size_t i = 0; i < keymap_count - 1; i++) {
                for (size_t j = i + 1; j < keymap_count; j++) {
                    if (!entries[i].is_default && !entries[j].is_default &&
                        strcasecmp(entries[i].display_name, entries[j].display_name) > 0) {
                        keymap_entry_t temp = entries[i];
                        entries[i] = entries[j];
                        entries[j] = temp;
                    }
                }
            }

            // Add "Default (symbolic us)" first if present
            for (size_t i = 0; i < keymap_count; i++) {
                if (entries[i].is_default) {
                    obs_property_list_add_string(keymap_prop, "Default (symbolic us)", entries[i].path);
                    break;
                }
            }

            // Add remaining keymaps in sorted order
            for (size_t i = 0; i < keymap_count; i++) {
                if (!entries[i].is_default) {
                    obs_property_list_add_string(keymap_prop, entries[i].display_name, entries[i].path);
                }
                free(entries[i].path);
            }

            free(entries);
        }
        free(keymap_paths);
    } else {
        // Fallback to hardcoded defaults if discovery fails
        obs_property_list_add_string(keymap_prop, "Default (symbolic us)", "symbolic_us");
        obs_property_list_add_string(keymap_prop, "Positional US", "positional_us");
    }

    // File system dropdown (renamed from "File source")
    obs_property_t *file_system_prop = obs_properties_add_list(rest_props, "file_system", obs_module_text("FileSystem"),
                                                               OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_set_long_description(file_system_prop, obs_module_text("FileSystem.Description"));
    obs_property_list_add_int(file_system_prop, obs_module_text("FileSystem.Local"), 0);
    obs_property_list_add_int(file_system_prop, obs_module_text("FileSystem.C64U"), 1);
    obs_property_set_modified_callback(file_system_prop, file_source_changed);

    // Playback source dropdown (new)
    obs_property_t *playback_source_prop = obs_properties_add_list(
        rest_props, "playback_source", obs_module_text("PlaybackSource"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_set_long_description(playback_source_prop, obs_module_text("PlaybackSource.Description"));
    obs_property_list_add_int(playback_source_prop, obs_module_text("PlaybackSource.SingleFile"), 0);
    obs_property_list_add_int(playback_source_prop, obs_module_text("PlaybackSource.Folder"), 1);
    obs_property_set_modified_callback(playback_source_prop, playback_source_changed);

    // Get current selections for visibility
    obs_data_t *current_settings = obs_source_get_settings(context->source);
    int file_system = (int)obs_data_get_int(current_settings, "file_system");
    int playback_source = (int)obs_data_get_int(current_settings, "playback_source");
    bool use_songlengths = obs_data_get_bool(current_settings, "automation_use_songlengths");

    // Local file path (file_system=0, playback_source=0)
    obs_property_t *local_file_prop =
        obs_properties_add_path(rest_props, "local_file_path", obs_module_text("LocalFilePath"), OBS_PATH_FILE,
                                "Programs and music (*.sid *.mod *.prg *.crt *.d64 *.g64 *.d71 *.g71 *.d81);;All "
                                "files (*.*)",
                                NULL);
    obs_property_set_modified_callback(local_file_prop, automation_settings_changed);
    obs_property_set_long_description(local_file_prop, obs_module_text("LocalFilePath.Description"));
    obs_property_set_visible(local_file_prop, file_system == 0 && playback_source == 0);

    // Local folder path (file_system=0, playback_source=1)
    obs_property_t *local_folder_prop = obs_properties_add_path(
        rest_props, "local_folder_path", obs_module_text("LocalFolderPath"), OBS_PATH_DIRECTORY, NULL, NULL);
    obs_property_set_modified_callback(local_folder_prop, automation_settings_changed);
    obs_property_set_long_description(local_folder_prop, obs_module_text("LocalFolderPath.Description"));
    obs_property_set_visible(local_folder_prop, file_system == 0 && playback_source == 1);

    // C64U file path (file_system=1, playback_source=0)
    obs_property_t *c64u_file_prop =
        obs_properties_add_text(rest_props, "c64u_file_path", obs_module_text("C64UFilePath"), OBS_TEXT_DEFAULT);
    obs_property_set_modified_callback(c64u_file_prop, automation_settings_changed);
    obs_property_set_long_description(c64u_file_prop, obs_module_text("C64UFilePath.Description"));
    obs_property_set_visible(c64u_file_prop, file_system == 1 && playback_source == 0);

    // C64U folder path (file_system=1, playback_source=1)
    obs_property_t *c64u_folder_prop =
        obs_properties_add_text(rest_props, "c64u_folder_path", obs_module_text("C64UFolderPath"), OBS_TEXT_DEFAULT);
    obs_property_set_modified_callback(c64u_folder_prop, automation_settings_changed);
    obs_property_set_long_description(c64u_folder_prop, obs_module_text("C64UFolderPath.Description"));
    obs_property_set_visible(c64u_folder_prop, file_system == 1 && playback_source == 1);

    obs_data_release(current_settings);

    // Folder playback options (only visible when playback_source=1)
    obs_property_t *include_subfolders_prop =
        obs_properties_add_bool(rest_props, "include_subfolders", obs_module_text("IncludeSubfolders"));
    obs_property_set_modified_callback(include_subfolders_prop, automation_settings_changed);
    obs_property_set_long_description(include_subfolders_prop, obs_module_text("IncludeSubfolders.Description"));
    obs_property_set_visible(include_subfolders_prop, playback_source == 1);

    obs_property_t *shuffle_playback_prop =
        obs_properties_add_bool(rest_props, "shuffle_playback", obs_module_text("ShufflePlayback"));
    obs_property_set_modified_callback(shuffle_playback_prop, automation_settings_changed);
    obs_property_set_long_description(shuffle_playback_prop, obs_module_text("ShufflePlayback.Description"));
    obs_property_set_visible(shuffle_playback_prop, playback_source == 1);

    // Automation duration (seconds)
    obs_property_t *auto_dur_prop = obs_properties_add_int_slider(rest_props, "automation_duration",
                                                                  obs_module_text("AutomationDuration"), 1, 3600, 1);
    obs_property_set_long_description(auto_dur_prop, obs_module_text("AutomationDuration.Description"));
    obs_property_set_modified_callback(auto_dur_prop, automation_runtime_settings_changed);

    // Use song lengths (SID)
    obs_property_t *auto_songlengths_prop =
        obs_properties_add_bool(rest_props, "automation_use_songlengths", obs_module_text("UseSonglengths"));
    obs_property_set_long_description(auto_songlengths_prop, obs_module_text("UseSonglengths.Description"));
    obs_property_set_modified_callback(auto_songlengths_prop, automation_runtime_settings_changed);

    obs_property_t *songlengths_path_prop = obs_properties_add_path(rest_props, "automation_songlengths_path",
                                                                    obs_module_text("SonglengthsPath"), OBS_PATH_FILE,
                                                                    "Songlengths (*.md5 *.txt);;All Files (*.*)", NULL);
    obs_property_set_long_description(songlengths_path_prop, obs_module_text("SonglengthsPath.Description"));
    obs_property_set_modified_callback(songlengths_path_prop, automation_runtime_settings_changed);
    obs_property_set_visible(songlengths_path_prop, file_system == 0 && use_songlengths);

    // Automation reset between items
    obs_property_t *auto_reset_prop =
        obs_properties_add_bool(rest_props, "automation_reset", obs_module_text("AutomationReset"));
    obs_property_set_long_description(auto_reset_prop, obs_module_text("AutomationReset.Description"));
    obs_property_set_modified_callback(auto_reset_prop, automation_runtime_settings_changed);

    // Playlist dropdown (replaces status text)
    obs_property_t *playlist_prop = obs_properties_add_list(rest_props, "playlist", obs_module_text("Playing"),
                                                            OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_set_long_description(playlist_prop, obs_module_text("Playing.Description"));
    obs_property_set_modified_callback(playlist_prop, playlist_changed);
    // Get current settings to populate playlist from any existing cached automation state.
    obs_data_t *current_settings_for_playlist = obs_source_get_settings(context->source);
    c64_automation_config_t preload_config = {0};
    if (build_automation_config_from_settings(current_settings_for_playlist, &preload_config, NULL)) {
        if (!context->automation && context->rest_client) {
            context->automation = c64_automation_create(context->rest_client, context->keyboard, context->source);
        }
        if (context->automation && !c64_automation_is_running(context->automation)) {
            c64_automation_configure(context->automation, &preload_config);
            (void)c64_automation_preload_playlist_async(
                context->automation, &preload_config, (int)obs_data_get_int(current_settings_for_playlist, "playlist"));
        }
    }
    update_playlist_property(playlist_prop, context, current_settings_for_playlist);
    obs_data_release(current_settings_for_playlist);

    // Refresh playlist button (directly below playlist dropdown)
    obs_property_t *refresh_playlist_prop = obs_properties_add_button(rest_props, "automation_refresh_playlist",
                                                                      obs_module_text("RefreshPlaylist"),
                                                                      automation_refresh_playlist_clicked);
    obs_property_set_long_description(refresh_playlist_prop, obs_module_text("RefreshPlaylist.Description"));

    // Get automation status to determine button state
    bool automation_running = false;
    bool automation_preloading = false;
    if (context->automation) {
        automation_running = c64_automation_is_running((c64_automation_t *)context->automation);
        automation_preloading = c64_automation_is_preloading((c64_automation_t *)context->automation);
    }

    obs_property_set_enabled(refresh_playlist_prop, !automation_running && !automation_preloading);

    // Play/Stop Content button - label changes based on state
    const char *button_label = automation_running ? obs_module_text("AutomationStopContent")
                                                  : obs_module_text("AutomationPlayContent");
    obs_property_t *auto_start_stop_prop =
        obs_properties_add_button(rest_props, "automation_start_stop", button_label, automation_start_stop_clicked);
    obs_property_set_long_description(auto_start_stop_prop, automation_running
                                                                ? obs_module_text("AutomationStopContent.Description")
                                                                : obs_module_text("AutomationPlayContent.Description"));
    // Disable Play while an async preload is scanning the filesystem so the user cannot
    // trigger a blocking join of the preload thread from the UI thread.
    if (!automation_running && automation_preloading) {
        obs_property_set_enabled(auto_start_stop_prop, false);
    }

    // Next button (only enabled when playing)
    obs_property_t *auto_next_prop = obs_properties_add_button(
        rest_props, "automation_next", obs_module_text("AutomationNext"), automation_next_clicked);
    obs_property_set_long_description(auto_next_prop, obs_module_text("AutomationNext.Description"));
    obs_property_set_enabled(auto_next_prop, automation_running);

    // Reset controls (split into two buttons)
    obs_property_t *reset_plugin_prop =
        obs_properties_add_button(rest_props, "reset_plugin", obs_module_text("ResetPlugin"), reset_plugin_clicked);
    obs_property_set_long_description(reset_plugin_prop, obs_module_text("ResetPlugin.Description"));

    obs_property_t *reset_c64u_prop =
        obs_properties_add_button(rest_props, "reset_c64u", obs_module_text("ResetC64U"), reset_c64u_clicked);
    obs_property_set_long_description(reset_c64u_prop, obs_module_text("ResetC64U.Description"));

    // Script Group
    obs_property_t *script_group = obs_properties_add_group(props, "script_group", obs_module_text("Script"),
                                                            OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *script_props = obs_property_group_content(script_group);

    // Script Automation
    obs_property_t *script_file_prop = obs_properties_add_path(script_props, "script_file",
                                                               obs_module_text("ScriptFile"), OBS_PATH_FILE,
                                                               "C64 Script (*.c64script);;All Files (*.*)", NULL);
    obs_property_set_long_description(script_file_prop, obs_module_text("ScriptFile.Description"));

    {
        obs_data_t *script_settings = obs_source_get_settings(context->source);
        const char *existing_script = obs_data_get_string(script_settings, "script_file");
        if (!existing_script || existing_script[0] == '\0') {
            char scripts_dir[512];
            if (c64_get_user_dir(C64_USER_DIR_SCRIPTS, scripts_dir, sizeof(scripts_dir))) {
                size_t len = strlen(scripts_dir);
                if (len > 0 && len < sizeof(scripts_dir) - 2 && scripts_dir[len - 1] != '/' &&
                    scripts_dir[len - 1] != '\\') {
                    scripts_dir[len] = '/';
                    scripts_dir[len + 1] = '\0';
                }
                obs_data_set_default_string(script_settings, "script_file", scripts_dir);
                obs_data_set_string(script_settings, "script_file", scripts_dir);
            }
        }
        obs_data_release(script_settings);
    }

    obs_property_t *script_auto_start_prop =
        obs_properties_add_bool(script_props, "script_auto_start", obs_module_text("ScriptAutoStart"));
    obs_property_set_long_description(script_auto_start_prop, obs_module_text("ScriptAutoStart.Description"));

    obs_property_t *script_status_prop =
        obs_properties_add_text(script_props, "script_status", obs_module_text("ScriptStatus"), OBS_TEXT_INFO);
    update_script_status_property(script_status_prop, context);

    // Get current script status for button labels and visibility
    c64_script_status_t current_status = C64_SCRIPT_STATUS_IDLE;
    if (context->script_executor) {
        current_status = c64_script_executor_get_status(context->script_executor);
    }
    bool is_running_or_paused =
        (current_status == C64_SCRIPT_STATUS_RUNNING || current_status == C64_SCRIPT_STATUS_PAUSED);
    bool is_in_debug_mode = (current_status == C64_SCRIPT_STATUS_PAUSED);

    // Determine if we should update line tracking UI based on throttling
    bool should_update_lines = false;
    uint64_t now = os_gettime_ns();
    uint64_t time_since_last_update = (context->last_ui_update_time > 0) ? (now - context->last_ui_update_time)
                                                                         : UINT64_MAX;

    if (context->force_ui_update) {
        // State transition occurred - force immediate update
        should_update_lines = true;
        context->force_ui_update = false;
        context->last_ui_update_time = now;
    } else if (is_in_debug_mode) {
        // Debug mode (paused/stepping) - always update immediately
        should_update_lines = true;
        context->last_ui_update_time = now;
    } else if (is_running_or_paused) {
        // Normal execution - throttle to once per second
        const uint64_t throttle_interval_ns = 1000000000ULL; // 1 second in nanoseconds
        if (time_since_last_update >= throttle_interval_ns) {
            should_update_lines = true;
            context->last_ui_update_time = now;
        }
    } else {
        // Script not running - always show current state
        should_update_lines = true;
    }

    // Start/Stop button - label changes based on state
    const char *script_button_label = is_running_or_paused ? obs_module_text("ScriptStopScript")
                                                           : obs_module_text("ScriptStartScript");
    obs_property_t *script_start_stop_prop =
        obs_properties_add_button(script_props, "script_start_stop", script_button_label, script_start_stop_clicked);
    obs_property_set_long_description(script_start_stop_prop, is_running_or_paused
                                                                  ? obs_module_text("ScriptStartStop.Stop")
                                                                  : obs_module_text("ScriptStartStop.Start"));

    // Debug/Pause/Resume button - state machine labels
    const char *debug_label = NULL;
    const char *debug_desc = NULL;
    if (current_status == C64_SCRIPT_STATUS_PAUSED) {
        debug_label = obs_module_text("ScriptResumeScript");
        debug_desc = obs_module_text("ScriptResumeScript.Description");
    } else if (current_status == C64_SCRIPT_STATUS_RUNNING || current_status == C64_SCRIPT_STATUS_WAITING) {
        debug_label = obs_module_text("ScriptPauseScript");
        debug_desc = obs_module_text("ScriptPauseScript.Description");
    } else {
        debug_label = obs_module_text("ScriptDebugScript");
        debug_desc = obs_module_text("ScriptDebugScript.Description");
    }
    obs_property_t *script_pause_resume_prop =
        obs_properties_add_button(script_props, "script_pause_resume", debug_label, script_pause_resume_clicked);
    obs_property_set_long_description(script_pause_resume_prop, debug_desc);
    obs_property_set_enabled(script_pause_resume_prop, context->script_file_path[0] != '\0');

    // Step button - only enabled when paused
    obs_property_t *script_step_prop =
        obs_properties_add_button(script_props, "script_step", obs_module_text("ScriptStep"), script_step_clicked);
    obs_property_set_long_description(script_step_prop, obs_module_text("ScriptStep.Description"));
    obs_property_set_enabled(script_step_prop, current_status == C64_SCRIPT_STATUS_PAUSED);

    // Log variables button
    obs_property_t *script_log_vars_prop = obs_properties_add_button(
        script_props, "script_log_vars", obs_module_text("ScriptLogVariables"), script_log_variables_clicked);
    obs_property_set_long_description(script_log_vars_prop, obs_module_text("ScriptLogVariables.Description"));
    obs_property_set_enabled(script_log_vars_prop, is_running_or_paused);

    // Execution state display
    char exec_state[64] = "stopped";
    if (current_status == C64_SCRIPT_STATUS_RUNNING) {
        snprintf(exec_state, sizeof(exec_state), "running");
    } else if (current_status == C64_SCRIPT_STATUS_PAUSED) {
        snprintf(exec_state, sizeof(exec_state), "paused");
    } else if (current_status == C64_SCRIPT_STATUS_ERROR) {
        snprintf(exec_state, sizeof(exec_state), "error");
    } else if (current_status == C64_SCRIPT_STATUS_COMPLETED) {
        snprintf(exec_state, sizeof(exec_state), "completed");
    }
    obs_property_t *exec_state_prop = obs_properties_add_text(script_props, "script_exec_state",
                                                              obs_module_text("ScriptExecutionState"), OBS_TEXT_INFO);
    obs_property_text_set_info_type(exec_state_prop, OBS_TEXT_INFO_NORMAL);
    obs_property_set_description(exec_state_prop, obs_module_text("ScriptExecutionState"));
    obs_property_set_long_description(exec_state_prop, exec_state);

    // Last executed line - always visible, throttled in normal mode
    if (context->script_executor && should_update_lines) {
        char line_content[256] = {0};
        int line_num =
            c64_script_executor_get_last_executed_line(context->script_executor, line_content, sizeof(line_content));
        if (line_num > 0) {
            snprintf(context->cached_last_line, sizeof(context->cached_last_line), "%d: %.400s", line_num,
                     line_content);
        } else {
            snprintf(context->cached_last_line, sizeof(context->cached_last_line), "(not started)");
        }
    }
    // Use cached value (either just updated or from previous update)
    if (context->cached_last_line[0] == '\0') {
        snprintf(context->cached_last_line, sizeof(context->cached_last_line), "(not started)");
    }
    obs_property_t *last_line_prop =
        obs_properties_add_text(script_props, "script_last_line", obs_module_text("ScriptLastExecuted"), OBS_TEXT_INFO);
    obs_property_text_set_info_type(last_line_prop, OBS_TEXT_INFO_NORMAL);
    obs_property_set_long_description(last_line_prop, context->cached_last_line);

    // Next line to execute - always visible, throttled in normal mode
    if (context->script_executor && should_update_lines) {
        char line_content[256] = {0};
        int line_num = c64_script_executor_get_next_line(context->script_executor, line_content, sizeof(line_content));
        if (line_num > 0) {
            snprintf(context->cached_next_line, sizeof(context->cached_next_line), "%d: %.400s", line_num,
                     line_content);
        } else if (is_running_or_paused) {
            snprintf(context->cached_next_line, sizeof(context->cached_next_line), "(completed)");
        } else {
            snprintf(context->cached_next_line, sizeof(context->cached_next_line), "(not started)");
        }
    }
    // Use cached value (either just updated or from previous update)
    if (context->cached_next_line[0] == '\0') {
        snprintf(context->cached_next_line, sizeof(context->cached_next_line), "(not started)");
    }
    obs_property_t *next_line_prop = obs_properties_add_text(script_props, "script_next_line",
                                                             obs_module_text("ScriptNextToExecute"), OBS_TEXT_INFO);
    obs_property_text_set_info_type(next_line_prop, OBS_TEXT_INFO_NORMAL);
    obs_property_set_long_description(next_line_prop, context->cached_next_line);

    // Last runtime error (only show if there's an error)
    if (current_status == C64_SCRIPT_STATUS_ERROR && context->script_executor) {
        const char *err = c64_script_executor_get_error(context->script_executor);
        if (err && err[0] != '\0') {
            obs_property_t *error_prop = obs_properties_add_text(script_props, "script_error",
                                                                 obs_module_text("ScriptLastError"), OBS_TEXT_INFO);
            obs_property_text_set_info_type(error_prop, OBS_TEXT_INFO_ERROR);
            obs_property_set_long_description(error_prop, err);
        }
    }

    if (context) {
        context->playlist_refresh_suppressed_once = true;
        context->playlist_properties_initializing = false;
    }

    return props;
}

static void c64_default_palette_import_path(char *path, size_t path_size)
{
    if (!path || path_size < 64)
        return;

    // Get palettes directory
    char palettes_dir[512];
    if (c64_get_user_dir(C64_USER_DIR_PALETTES, palettes_dir, sizeof(palettes_dir))) {
        strncpy(path, palettes_dir, path_size - 1);
        path[path_size - 1] = '\0';
    } else {
        // Fallback to empty string (current directory)
        path[0] = '\0';
    }
}

static void c64_default_palette_export_path(char *path, size_t path_size)
{
    if (!path || path_size < 64)
        return;

    // Just return the user's palettes folder - user will specify filename when exporting
    if (c64_get_user_dir(C64_USER_DIR_PALETTES, path, path_size)) {
        return;
    }

    // Fallback: current directory
    path[0] = '\0';
}

static bool c64_ensure_parent_dir_exists(const char *file_path)
{
    if (!file_path || file_path[0] == '\0')
        return false;

    char dir[1024];
    strncpy(dir, file_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    char *slash = strrchr(dir, '/');
    char *bslash = strrchr(dir, '\\');
    char *sep = slash;
    if (bslash && (!sep || bslash > sep))
        sep = bslash;

    if (!sep)
        return true; // No directory component

    *sep = '\0';
    if (dir[0] == '\0')
        return true;

    return c64_create_directory_recursive(dir);
}

static bool c64_export_settings_to_ini(obs_data_t *settings, const char *path)
{
    if (!settings || !path || path[0] == '\0')
        return false;

    if (!c64_ensure_parent_dir_exists(path)) {
        C64_LOG_WARNING("Export: failed to create parent directory for %s", path);
        return false;
    }

    FILE *f = os_fopen(path, "w");
    if (!f) {
        C64_LOG_WARNING("Export: failed to open %s for writing", path);
        return false;
    }

    const char *c64_host = obs_data_get_string(settings, "c64_host");
    // NOTE: Password is intentionally excluded from export for security.
    // Passwords remain in OBS settings which are protected by OS-level access controls.
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");
    const char *obs_ip_address = obs_data_get_string(settings, "obs_ip_address");
    const bool auto_detect_ip = obs_data_get_bool(settings, "auto_detect_ip");
    const int video_port = (int)obs_data_get_int(settings, "video_port");
    const int audio_port = (int)obs_data_get_int(settings, "audio_port");
    const int control_port = (int)obs_data_get_int(settings, "control_port");
    const int buffer_delay_ms = (int)obs_data_get_int(settings, "buffer_delay_ms");

    const char *save_folder = obs_data_get_string(settings, "save_folder");
    const bool record_frames = obs_data_get_bool(settings, "record_frames");
    const bool record_video = obs_data_get_bool(settings, "record_video");
    const bool record_csv = obs_data_get_bool(settings, "record_csv");
    const bool record_av_sync = obs_data_get_bool(settings, "record_av_sync");

    const bool debug_logging = obs_data_get_bool(settings, "debug_logging");

    const char *crt_preset = obs_data_get_string(settings, "crt_preset");
    const double scan_line_distance = obs_data_get_double(settings, "scan_line_distance");
    const double scan_line_strength = obs_data_get_double(settings, "scan_line_strength");
    const double pixel_width = obs_data_get_double(settings, "pixel_width");
    const double pixel_height = obs_data_get_double(settings, "pixel_height");
    const double blur_strength = obs_data_get_double(settings, "blur_strength");
    const double bloom_strength = obs_data_get_double(settings, "bloom_strength");
    const int afterglow_duration_ms = (int)obs_data_get_int(settings, "afterglow_duration_ms");
    const int afterglow_curve = (int)obs_data_get_int(settings, "afterglow_curve");
    const int tint_mode = (int)obs_data_get_int(settings, "tint_mode");
    const double tint_strength = obs_data_get_double(settings, "tint_strength");
    const bool preserve_size = obs_data_get_bool(settings, "preserve_size");

    // Palette settings
    const char *palette_id = obs_data_get_string(settings, C64_PALETTE_KEY);

    // Get current timestamp for export
    time_t now = time(NULL);
    struct tm *utc_time = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", utc_time);

    // Determine platform string
#if defined(_WIN32) || defined(_WIN64)
#if defined(_WIN64)
    const char *platform = "windows-x64";
#else
    const char *platform = "windows-x86";
#endif
#elif defined(__APPLE__)
    const char *platform = "macos";
#elif defined(__linux__)
#if defined(__x86_64__)
    const char *platform = "linux-x86_64";
#elif defined(__aarch64__)
    const char *platform = "linux-arm64";
#else
    const char *platform = "linux";
#endif
#else
    const char *platform = "unknown";
#endif

    fprintf(f, "# C64 Stream Properties Export\n");
    fprintf(f, "#\n");
    fprintf(f, "# This file can be imported via the C64 Stream source Properties window.\n");
    fprintf(f, "#\n");
    fprintf(f, "# --- System Information (for bug reports) ---\n");
    fprintf(f, "# Plugin:   %s\n", c64_get_build_info());
    fprintf(f, "# OBS:      %s\n", obs_get_version_string());
    fprintf(f, "# Platform: %s\n", platform);
    fprintf(f, "# Exported: %s\n", timestamp);
    fprintf(f, "#\n\n");

    fprintf(f, "[network]\n");
    fprintf(f, "c64_host=%s\n", c64_host ? c64_host : "");
    // Password is intentionally excluded from export for security
    fprintf(f, "dns_server_ip=%s\n", dns_server_ip ? dns_server_ip : "");
    fprintf(f, "obs_ip_address=%s\n", obs_ip_address ? obs_ip_address : "");
    fprintf(f, "auto_detect_ip=%s\n", auto_detect_ip ? "true" : "false");
    fprintf(f, "video_port=%d\n", video_port);
    fprintf(f, "audio_port=%d\n", audio_port);
    fprintf(f, "control_port=%d\n", control_port);
    fprintf(f, "buffer_delay_ms=%d\n", buffer_delay_ms);
    fprintf(f, "\n");

    fprintf(f, "[recording]\n");
    fprintf(f, "save_folder=%s\n", save_folder ? save_folder : "");
    fprintf(f, "record_frames=%s\n", record_frames ? "true" : "false");
    fprintf(f, "record_video=%s\n", record_video ? "true" : "false");
    fprintf(f, "record_csv=%s\n", record_csv ? "true" : "false");
    fprintf(f, "record_av_sync=%s\n", record_av_sync ? "true" : "false");
    fprintf(f, "\n");

    fprintf(f, "[debug]\n");
    fprintf(f, "debug_logging=%s\n", debug_logging ? "true" : "false");
    fprintf(f, "\n");

    fprintf(f, "[effects]\n");
    fprintf(f, "crt_preset=%s\n", crt_preset ? crt_preset : "");
    fprintf(f, "scan_line_distance=%.6f\n", scan_line_distance);
    fprintf(f, "scan_line_strength=%.6f\n", scan_line_strength);
    fprintf(f, "pixel_width=%.6f\n", pixel_width);
    fprintf(f, "pixel_height=%.6f\n", pixel_height);
    fprintf(f, "blur_strength=%.6f\n", blur_strength);
    fprintf(f, "bloom_strength=%.6f\n", bloom_strength);
    fprintf(f, "afterglow_duration_ms=%d\n", afterglow_duration_ms);
    fprintf(f, "afterglow_curve=%d\n", afterglow_curve);
    fprintf(f, "tint_mode=%d\n", tint_mode);
    fprintf(f, "tint_strength=%.6f\n", tint_strength);
    fprintf(f, "preserve_size=%s\n", preserve_size ? "true" : "false");
    fprintf(f, "\n");

    fprintf(f, "[palette]\n");
    fprintf(f, "palette=%s\n", palette_id ? palette_id : "Default");
    // Export all 16 color values in hex format (AARRGGBB)
    for (int i = 0; i < 16; i++) {
        char key[32];
        snprintf(key, sizeof(key), "palette_color_%d", i);
        long long color_value = obs_data_get_int(settings, key);
        // Convert from OBS format (0xAABBGGRR) to standard hex (0xAARRGGBB) for portability
        uint8_t a = (color_value >> 24) & 0xFF;
        uint8_t b = (color_value >> 16) & 0xFF;
        uint8_t g = (color_value >> 8) & 0xFF;
        uint8_t r = color_value & 0xFF;
        fprintf(f, "palette_color_%d=0x%02X%02X%02X%02X\n", i, a, r, g, b);
    }
    fprintf(f, "\n");

    fclose(f);
    return true;
}

static bool c64_parse_bool(const char *value, bool default_value)
{
    if (!value || value[0] == '\0')
        return default_value;
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0)
        return true;
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0)
        return false;
    return default_value;
}

static bool c64_apply_ini_to_settings(obs_data_t *settings, const char *path)
{
    if (!settings || !path || path[0] == '\0')
        return false;

    FILE *file = os_fopen(path, "r");
    if (!file) {
        C64_LOG_WARNING("Import: failed to open %s", path);
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        trim_config_string(line);

        if (line[0] == '\0' || line[0] == ';' || line[0] == '#')
            continue;

        if (line[0] == '[')
            continue; // section header

        char *equals = strchr(line, '=');
        if (!equals)
            continue;

        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        trim_config_string(key);
        trim_config_string(value);

        if (strcmp(key, "dns_server_ip") == 0) {
            obs_data_set_string(settings, "dns_server_ip", value);
        } else if (strcmp(key, "c64_host") == 0) {
            obs_data_set_string(settings, "c64_host", value);
        } else if (strcmp(key, "c64_password") == 0) {
            // Password is intentionally excluded from import for security.
            // Users must re-enter passwords after importing configuration.
            C64_LOG_INFO("Config import: skipping c64_password (excluded for security)");
        } else if (strcmp(key, "obs_ip_address") == 0) {
            // Allow empty string (means "leave as-is"/auto-detect default).
            if (value && value[0] != '\0')
                obs_data_set_string(settings, "obs_ip_address", value);
        } else if (strcmp(key, "auto_detect_ip") == 0) {
            obs_data_set_bool(settings, "auto_detect_ip", c64_parse_bool(value, true));
        } else if (strcmp(key, "video_port") == 0) {
            int port = atoi(value);
            if (port >= 1024 && port <= 65535)
                obs_data_set_int(settings, "video_port", port);
        } else if (strcmp(key, "audio_port") == 0) {
            int port = atoi(value);
            if (port >= 1024 && port <= 65535)
                obs_data_set_int(settings, "audio_port", port);
        } else if (strcmp(key, "control_port") == 0) {
            int port = atoi(value);
            if (port >= 64 && port <= 65535)
                obs_data_set_int(settings, "control_port", port);
        } else if (strcmp(key, "buffer_delay_ms") == 0) {
            int delay = atoi(value);
            if (delay >= 0 && delay <= 500)
                obs_data_set_int(settings, "buffer_delay_ms", delay);
        } else if (strcmp(key, "save_folder") == 0) {
            if (value && value[0] != '\0')
                obs_data_set_string(settings, "save_folder", value);
        } else if (strcmp(key, "record_frames") == 0) {
            obs_data_set_bool(settings, "record_frames", c64_parse_bool(value, false));
        } else if (strcmp(key, "record_video") == 0) {
            obs_data_set_bool(settings, "record_video", c64_parse_bool(value, false));
        } else if (strcmp(key, "record_csv") == 0) {
            obs_data_set_bool(settings, "record_csv", c64_parse_bool(value, false));
        } else if (strcmp(key, "record_av_sync") == 0) {
            obs_data_set_bool(settings, "record_av_sync", c64_parse_bool(value, false));
        } else if (strcmp(key, "debug_logging") == 0) {
            obs_data_set_bool(settings, "debug_logging", c64_parse_bool(value, false));
        } else if (strcmp(key, "crt_preset") == 0) {
            if (value && value[0] != '\0') {
                obs_data_set_string(settings, "crt_preset", value);
                // Prevent preset re-apply from overwriting imported tweaks when Properties UI rebuilds.
                obs_data_set_string(settings, C64_PRESET_LAST_APPLIED_KEY, value);
            }
        } else if (strcmp(key, "scan_line_distance") == 0) {
            // Validate range: 0.0 - 2.0 (Critical #3: import validation)
            double v = os_strtod(value);
            if (v >= 0.0 && v <= 2.0)
                obs_data_set_double(settings, "scan_line_distance", v);
        } else if (strcmp(key, "scan_line_strength") == 0) {
            // Validate range: 0.0 - 1.0
            double v = os_strtod(value);
            if (v >= 0.0 && v <= 1.0)
                obs_data_set_double(settings, "scan_line_strength", v);
        } else if (strcmp(key, "pixel_width") == 0) {
            // Validate range: 0.5 - 3.0
            double v = os_strtod(value);
            if (v >= 0.5 && v <= 3.0)
                obs_data_set_double(settings, "pixel_width", v);
        } else if (strcmp(key, "pixel_height") == 0) {
            // Validate range: 0.5 - 3.0
            double v = os_strtod(value);
            if (v >= 0.5 && v <= 3.0)
                obs_data_set_double(settings, "pixel_height", v);
        } else if (strcmp(key, "blur_strength") == 0) {
            // Validate range: 0.0 - 1.0
            double v = os_strtod(value);
            if (v >= 0.0 && v <= 1.0)
                obs_data_set_double(settings, "blur_strength", v);
        } else if (strcmp(key, "bloom_strength") == 0) {
            // Validate range: 0.0 - 1.0
            double v = os_strtod(value);
            if (v >= 0.0 && v <= 1.0)
                obs_data_set_double(settings, "bloom_strength", v);
        } else if (strcmp(key, "afterglow_duration_ms") == 0) {
            int ms = atoi(value);
            if (ms >= 0 && ms <= 3000)
                obs_data_set_int(settings, "afterglow_duration_ms", ms);
        } else if (strcmp(key, "afterglow_curve") == 0) {
            int curve = atoi(value);
            if (curve >= 0 && curve <= 3)
                obs_data_set_int(settings, "afterglow_curve", curve);
        } else if (strcmp(key, "tint_mode") == 0) {
            int mode = atoi(value);
            if (mode >= 0 && mode <= 3)
                obs_data_set_int(settings, "tint_mode", mode);
        } else if (strcmp(key, "tint_strength") == 0) {
            // Validate range: 0.0 - 1.0
            double v = os_strtod(value);
            if (v >= 0.0 && v <= 1.0)
                obs_data_set_double(settings, "tint_strength", v);
        } else if (strcmp(key, "preserve_size") == 0) {
            obs_data_set_bool(settings, "preserve_size", c64_parse_bool(value, true));
        } else if (strcmp(key, "palette") == 0) {
            // Import palette selection
            if (value && value[0] != '\0') {
                obs_data_set_string(settings, C64_PALETTE_KEY, value);
            }
        } else if (strncmp(key, "palette_color_", 14) == 0) {
            // Import palette color values (format: 0xAARRGGBB)
            int color_index = atoi(key + 14);
            if (color_index >= 0 && color_index < 16) {
                // Parse hex color value (0xAARRGGBB)
                unsigned long hex_value = strtoul(value, NULL, 16);
                uint8_t a = (hex_value >> 24) & 0xFF;
                uint8_t r = (hex_value >> 16) & 0xFF;
                uint8_t g = (hex_value >> 8) & 0xFF;
                uint8_t b = hex_value & 0xFF;
                // Convert to OBS format (0xAABBGGRR)
                long long obs_color = ((long long)a << 24) | ((long long)b << 16) | ((long long)g << 8) | r;
                obs_data_set_int(settings, key, obs_color);
            }
        }
    }

    fclose(file);
    return true;
}

// Callback for preset selection
static bool crt_preset_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    if (!settings)
        return false;

    const char *preset_name = obs_data_get_string(settings, "crt_preset");
    if (!preset_name || preset_name[0] == '\0')
        return false;

    const char *last_applied = obs_data_get_string(settings, C64_PRESET_LAST_APPLIED_KEY);
    if (last_applied && last_applied[0] != '\0' && strcmp(last_applied, preset_name) == 0) {
        // Already applied; don't overwrite user tweaks when reopening the Properties panel.
        return false;
    }

    // Apply the preset
    if (c64_effect_apply(settings, preset_name)) {
        C64_LOG_INFO("" EFFECT_LOG_PREFIX " Applied CRT preset: %s", preset_name);
        obs_data_set_string(settings, C64_PRESET_LAST_APPLIED_KEY, preset_name);
        return true;
    }

    return false;
}

void c64_set_property_defaults(obs_data_t *settings)
{
    // Defaults initialization

    // Keep debug logging off by default; users can enable it explicitly.
    obs_data_set_default_bool(settings, "debug_logging", false);
    // Default: auto-detect OBS IP enabled (OBS "Defaults" button uses these defaults)
    obs_data_set_default_bool(settings, "auto_detect_ip", true);
    obs_data_set_default_string(settings, "dns_server_ip", "192.168.1.1");
    obs_data_set_default_string(settings, "c64_host", C64_DEFAULT_HOST);
    obs_data_set_default_string(settings, "c64_password", "");
    // Default OBS IP should be the dynamically detected local IP so OBS "Defaults" fills it in immediately.
    // If detection fails, fall back to empty and the runtime code will use localhost.
    {
        char detected_ip[64];
        if (c64_detect_local_ip(detected_ip, sizeof(detected_ip))) {
            obs_data_set_default_string(settings, "obs_ip_address", detected_ip);
        } else {
            obs_data_set_default_string(settings, "obs_ip_address", "");
        }
    }
    obs_data_set_default_int(settings, "video_port", C64_DEFAULT_VIDEO_PORT);
    obs_data_set_default_int(settings, "audio_port", C64_DEFAULT_AUDIO_PORT);
    obs_data_set_default_int(settings, "control_port", C64_CONTROL_PORT);
    obs_data_set_default_int(settings, "buffer_delay_ms", 10); // Default 10ms buffer delay

    obs_data_set_default_bool(settings, "script_auto_start", false);

    // Default script path (points to user scripts directory)
    {
        char scripts_path[512];
        if (c64_get_user_dir(C64_USER_DIR_SCRIPTS, scripts_path, sizeof(scripts_path))) {
            size_t len = strlen(scripts_path);
            if (len > 0 && len < sizeof(scripts_path) - 2 && scripts_path[len - 1] != '/' &&
                scripts_path[len - 1] != '\\') {
                scripts_path[len] = '/';
                scripts_path[len + 1] = '\0';
            }
            obs_data_set_default_string(settings, "script_file", scripts_path);
        }
    }

    // Frame saving defaults
    obs_data_set_default_bool(settings, "record_frames", false); // Disabled by default

    // Platform-specific default recording folder
    char platform_path[512];
    if (c64_get_user_dir(C64_USER_DIR_RECORDINGS, platform_path, sizeof(platform_path))) {
        obs_data_set_default_string(settings, "save_folder", platform_path);
    } else {
        // Fallback to platform-specific defaults if helper fails
#ifdef _WIN32
        strcpy(platform_path, "C:\\Users\\Public\\Documents\\obs-studio\\c64stream\\recordings");
#elif defined(__APPLE__)
        strcpy(platform_path, "/Users/user/Documents/obs-studio/c64stream/recordings");
#else // Linux and other Unix-like systems
        strcpy(platform_path, "/home/user/Documents/obs-studio/c64stream/recordings");
#endif
        obs_data_set_default_string(settings, "save_folder", platform_path);
    }

    // NOTE: Do NOT set default export/import paths
    // Setting defaults triggers modified callbacks which cause spurious exports
    // The file browser will handle appropriate directory navigation

    // Video recording defaults
    obs_data_set_default_bool(settings, "record_video", false); // Disabled by default

    // CSV recording defaults
    obs_data_set_default_bool(settings, "record_csv", false); // Disabled by default

    // A/V sync CSV recording defaults
    obs_data_set_default_bool(settings, "record_av_sync", false); // Disabled by default

    // CRT effects defaults
    obs_data_set_default_double(settings, "scan_line_distance", 0.0);
    obs_data_set_default_double(settings, "scan_line_strength", 0.0);
    obs_data_set_default_double(settings, "pixel_width", 1.0);
    obs_data_set_default_double(settings, "pixel_height", 1.0);
    obs_data_set_default_double(settings, "blur_strength", 0.0);
    obs_data_set_default_double(settings, "bloom_strength", 0.0);
    obs_data_set_default_int(settings, "afterglow_duration_ms", 0);
    obs_data_set_default_int(settings, "afterglow_curve", 2);
    obs_data_set_default_int(settings, "tint_mode", 0);
    obs_data_set_default_double(settings, "tint_strength", 0.0);
    obs_data_set_default_bool(settings, "preserve_size", false);

    // Palette defaults
    obs_data_set_default_string(settings, C64_PALETTE_KEY, "Default");
    // NOTE: Do NOT set default values for palette_import_path or palette_export_path.
    // Setting defaults triggers the modified callbacks (palette_import_path_changed, palette_export_path_changed),
    // which automatically creates unwanted VPL files (e.g., "palettes.vpl" from directory name "palettes").
    // Users should explicitly choose paths when they want to import/export.

    // Set initialization flag to prevent auto-save during property setup
    // This prevents spurious "Default (Custom)" palette creation on Windows
    obs_data_set_bool(settings, C64_PALETTE_INITIALIZING_KEY, true);

    // Initialize palette color properties from the current working palette
    update_palette_color_properties(settings);

    // Clear initialization flag - auto-save is now allowed
    obs_data_erase(settings, C64_PALETTE_INITIALIZING_KEY);

    // REST Control defaults
    obs_data_set_default_string(settings, "keyboard_keymap", "symbolic_us");

    obs_data_set_default_int(settings, "file_system", 0);     // Local filesystem
    obs_data_set_default_int(settings, "playback_source", 1); // Folder playback
    obs_data_set_default_bool(settings, "include_subfolders", true);
    obs_data_set_default_bool(settings, "shuffle_playback", false);
    obs_data_set_default_int(settings, "automation_duration", 120); // 2 minutes default
    obs_data_set_default_bool(settings, "automation_reset", true);
    obs_data_set_default_bool(settings, "automation_use_songlengths", true);

    obs_data_set_default_string(settings, "local_folder_path", "");

    // Legacy defaults for older settings keys (kept for compatibility)
    obs_data_set_default_int(settings, "file_source", 0);
    obs_data_set_default_string(settings, "automation_path", "");
    obs_data_set_default_bool(settings, "automation_shuffle", false);

    // Load configuration overrides from properties.ini if available
    c64_load_configuration(settings);
}

// Generic helper to ensure path ends with specified extension
static void c64_ensure_file_extension(char *path, size_t path_size, const char *extension)
{
    if (!path || !extension || path_size < 5 || path[0] == '\0')
        return;

    size_t ext_len = strlen(extension);
    if (ext_len == 0 || ext_len > 10) // Sanity check
        return;

    size_t len = strlen(path);

    // Check if path already ends with the extension (case-insensitive)
    if (len >= ext_len + 1) { // +1 for the dot
        const char *existing_ext = path + len - ext_len - 1;
        if (existing_ext[0] == '.' && strcasecmp(existing_ext + 1, extension) == 0)
            return; // Already has the correct extension
    }

    // Check if path ends with a dot (e.g., "file.")
    if (len >= 1 && path[len - 1] == '.') {
        // Append extension to existing dot
        if (len + ext_len < path_size) {
            strcat(path, extension);
        }
        return;
    }

    // Append .extension
    if (len + ext_len + 1 < path_size) {
        strcat(path, ".");
        strcat(path, extension);
    }
}

// Helper function to ensure path ends with .ini extension
static void c64_ensure_ini_extension(char *path, size_t path_size)
{
    c64_ensure_file_extension(path, path_size, "ini");
}

// Helper function to ensure path ends with .vpl extension
static void c64_ensure_vpl_extension(char *path, size_t path_size)
{
    c64_ensure_file_extension(path, path_size, "vpl");
}

// Heuristic fallback for export targets that do not exist yet.
static bool c64_path_looks_like_directory(const char *path)
{
    if (!path || !path[0])
        return true; // Empty path treated as directory

    const char *path_ext = strrchr(path, '.');
    const char *last_sep = strrchr(path, '/');
#ifdef _WIN32
    const char *last_backslash = strrchr(path, '\\');
    if (last_backslash && (!last_sep || last_backslash > last_sep))
        last_sep = last_backslash;
#endif

    // If the extension comes before the last separator, it's not a file extension
    if (path_ext && last_sep && path_ext < last_sep) {
        path_ext = NULL;
    }

    // If no extension or no filename component, this is likely a directory
    if (!path_ext && last_sep && last_sep[1] == '\0') {
        return true;
    }

    return false;
}

static bool config_export_path_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    if (!settings)
        return false;

    // Never export during initialization
    // This prevents spurious exports when properties dialog opens
    if (obs_data_get_bool(settings, C64_CONFIG_INITIALIZING_KEY)) {
        return false;
    }

    const char *path = obs_data_get_string(settings, C64_CONFIG_EXPORT_PATH_KEY);
    if (!path || path[0] == '\0') {
        return false;
    }

    // Check if we already processed this exact path
    const char *last_path = obs_data_get_string(settings, C64_CONFIG_EXPORT_LAST_KEY);
    if (last_path && strcmp(path, last_path) == 0) {
        return false; // Already processed
    }

    // Don't export if the path is a directory (not an .ini file)
    c64_path_kind_t path_kind = C64_PATH_KIND_OTHER;
    if (!c64_get_path_kind(path, &path_kind)) {
        C64_LOG_WARNING("Export: unable to access path: %s", path);
        return false;
    }

    if (path_kind == C64_PATH_KIND_DIRECTORY) {
        return false;
    }
    if (path_kind == C64_PATH_KIND_MISSING && c64_path_looks_like_directory(path)) {
        return false;
    }

    // Copy path and ensure .ini extension
    char export_path[512];
    strncpy(export_path, path, sizeof(export_path) - 1);
    export_path[sizeof(export_path) - 1] = '\0';
    c64_ensure_ini_extension(export_path, sizeof(export_path));

    const bool ok = c64_export_settings_to_ini(settings, export_path);
    if (ok) {
        C64_LOG_INFO("Exported C64 Stream settings to %s", export_path);

        // Update the path field to show the actual path with .ini extension
        if (strcmp(path, export_path) != 0) {
            obs_data_set_string(settings, C64_CONFIG_EXPORT_PATH_KEY, export_path);
        }

        // Remember this path so we don't re-export if callback fires again
        obs_data_set_string(settings, C64_CONFIG_EXPORT_LAST_KEY, export_path);
    } else {
        C64_LOG_WARNING("Failed to export C64 Stream settings to %s", export_path);
    }

    return true; // Refresh UI
}

static bool config_import_path_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    if (!settings)
        return false;

    // Never import during initialization
    // This prevents spurious imports when properties dialog opens
    if (obs_data_get_bool(settings, C64_CONFIG_INITIALIZING_KEY)) {
        return false;
    }

    const char *path = obs_data_get_string(settings, C64_CONFIG_IMPORT_PATH_KEY);
    if (!path || path[0] == '\0') {
        return false;
    }

    // Check if we already processed this exact path
    const char *last_path = obs_data_get_string(settings, C64_CONFIG_IMPORT_LAST_KEY);
    if (last_path && strcmp(path, last_path) == 0) {
        return false; // Already processed
    }

    c64_path_kind_t path_kind = C64_PATH_KIND_OTHER;
    if (!c64_get_path_kind(path, &path_kind)) {
        return false;
    }

    if (path_kind == C64_PATH_KIND_DIRECTORY) {
        return false;
    }

    if (path_kind == C64_PATH_KIND_MISSING) {
        C64_LOG_WARNING("Import: file does not exist: %s", path);
        return false;
    }

    if (path_kind != C64_PATH_KIND_FILE) {
        C64_LOG_WARNING("Import: unable to access file path: %s", path);
        return false;
    }

    const bool ok = c64_apply_ini_to_settings(settings, path);
    if (ok) {
        // Refresh color editor to reflect imported palette colors
        update_palette_color_properties(settings);

        C64_LOG_INFO("Imported C64 Stream settings from %s", path);

        // Remember this path so we don't re-import if callback fires again
        obs_data_set_string(settings, C64_CONFIG_IMPORT_LAST_KEY, path);
    } else {
        C64_LOG_WARNING("Failed to import C64 Stream settings from %s", path);
    }

    return true; // Refresh UI
}

// ============================================================================
// Palette UI callbacks
// ============================================================================

static void update_palette_color_properties(obs_data_t *settings)
{
    uint32_t *colors = c64_palette_get_working_colors();
    if (!colors) {
        return;
    }

    for (int i = 0; i < 16; i++) {
        char key[32];
        snprintf(key, sizeof(key), "palette_color_%d", i);

        // Convert BGRA to OBS color format (ABGR stored as int)
        uint32_t bgra = colors[i];
        // Convert BGRA to OBS color format (0xAABBGGRR)
        uint32_t obs_color = c64_bgra_to_obs_color(bgra);

        // Check if actual value exists and matches working color
        // If not, set it to prevent stale values from triggering callbacks
        // This prevents deleted palette colors from persisting and triggering recreation
        if (!obs_data_has_user_value(settings, key) || obs_data_get_int(settings, key) != (long long)obs_color) {
            // Set actual value to match working color
            obs_data_set_int(settings, key, (long long)obs_color);
        }

        // Also set default value for proper UI initialization
        obs_data_set_default_int(settings, key, (long long)obs_color);
    }
}

static bool palette_changed(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);
    struct c64_source *context = (struct c64_source *)priv;

    if (!settings) {
        return false;
    }

    const char *palette_id = obs_data_get_string(settings, C64_PALETTE_KEY);
    if (!palette_id || !palette_id[0]) {
        return false;
    }

    // Update tooltip with palette description
    const char *desc = c64_palette_get_description(palette_id);
    if (desc && property) {
        obs_property_set_long_description(property, desc);
    } else if (property) {
        obs_property_set_long_description(property, obs_module_text("PaletteSelection.Description"));
    }

    // Enable/disable delete button based on whether selected palette is custom
    obs_property_t *delete_btn = obs_properties_get(props, "palette_delete");
    if (delete_btn) {
        bool is_custom = !c64_palette_is_preset(palette_id);
        obs_property_set_enabled(delete_btn, is_custom);
    }

    // Check if palette needs to be loaded
    const char *current = c64_palette_get_active_id();
    bool needs_load = !current || strcmp(current, palette_id) != 0;

    if (needs_load) {
        if (!c64_palette_select(palette_id)) {
            return false;
        }

        // Save the palette selection to settings AND persist to disk
        // This ensures the selection persists across OBS restarts
        obs_data_set_string(settings, C64_PALETTE_KEY, palette_id);

        // Persist settings using passed-in source context
        if (context && context->source) {
            obs_source_update(context->source, settings);
            obs_source_save(context->source);
        }
    }

    // Always update color picker values to reflect the palette
    // (needed when switching between "Preset" and "Custom" versions with same base name)
    update_palette_color_properties(settings);

    return true; // Refresh UI
}

static bool palette_import_path_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(property);

    if (!settings) {
        return false;
    }

    // Never import during initialization
    // This prevents spurious palette imports when properties dialog opens
    if (obs_data_get_bool(settings, C64_PALETTE_INITIALIZING_KEY)) {
        return false;
    }

    const char *path = obs_data_get_string(settings, "palette_import_path");
    if (!path || !path[0]) {
        return false;
    }

    // Check if we already processed this exact path
    const char *last_path = obs_data_get_string(settings, C64_PALETTE_IMPORT_LAST_KEY);
    if (last_path && strcmp(path, last_path) == 0) {
        return false; // Already processed
    }

    c64_path_kind_t path_kind = C64_PATH_KIND_OTHER;
    if (!c64_get_path_kind(path, &path_kind)) {
        return false;
    }

    if (path_kind != C64_PATH_KIND_FILE) {
        return false;
    }

    // Load the palette file
    bool ok = c64_palette_load_from_file(path);

    if (ok) {
        C64_LOG_INFO("Palette imported from: %s", path);

        // Update palette dropdown selection
        obs_data_set_string(settings, C64_PALETTE_KEY, c64_palette_get_active_id());

        // Repopulate the palette dropdown to include the imported palette
        obs_property_t *palette_prop = obs_properties_get(props, C64_PALETTE_KEY);
        if (palette_prop) {
            c64_palette_populate_list(palette_prop);
        }

        // Update color pickers
        update_palette_color_properties(settings);

        // Remember this path so we don't re-import if callback fires again
        obs_data_set_string(settings, C64_PALETTE_IMPORT_LAST_KEY, path);
    } else {
        C64_LOG_WARNING("Failed to import palette from: %s", path);
    }

    // Keep the path visible so user can see what was imported
    // (Path persists until next import operation)

    return true; // Refresh UI
}

static bool palette_export_path_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    if (!settings) {
        return false;
    }

    // Never export during initialization
    // This prevents spurious palette file creation when properties dialog opens
    if (obs_data_get_bool(settings, C64_PALETTE_INITIALIZING_KEY)) {
        return false;
    }

    const char *path = obs_data_get_string(settings, "palette_export_path");
    if (!path || !path[0]) {
        return false;
    }

    // Check if we already processed this exact path
    const char *last_path = obs_data_get_string(settings, C64_PALETTE_EXPORT_LAST_KEY);
    if (last_path && strcmp(path, last_path) == 0) {
        return false; // Already processed
    }

    // Don't export if the path is a directory (not a .vpl file)
    c64_path_kind_t path_kind = C64_PATH_KIND_OTHER;
    if (!c64_get_path_kind(path, &path_kind)) {
        C64_LOG_WARNING("Palette export: unable to access path: %s", path);
        return false;
    }

    if (path_kind == C64_PATH_KIND_DIRECTORY) {
        return false;
    }
    if (path_kind == C64_PATH_KIND_MISSING && c64_path_looks_like_directory(path)) {
        return false;
    }

    // Copy path and ensure .vpl extension
    char full_path[512];
    strncpy(full_path, path, sizeof(full_path) - 1);
    full_path[sizeof(full_path) - 1] = '\0';
    c64_ensure_vpl_extension(full_path, sizeof(full_path));

    // Extract name from filename
    char name[64];
    const char *filename = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!filename || backslash > filename)) {
        filename = backslash;
    }
#endif
    if (filename) {
        filename++;
    } else {
        filename = path;
    }
    strncpy(name, filename, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    // Remove .vpl extension if present
    char *ext = strrchr(name, '.');
    if (ext && strcasecmp(ext, ".vpl") == 0) {
        *ext = '\0';
    }

    // Save current working palette to the specified path
    bool ok = c64_palette_save_as(name, full_path);

    if (ok) {
        C64_LOG_INFO("Palette exported to: %s", full_path);

        // Update the path field to show the actual path with .vpl extension
        if (strcmp(path, full_path) != 0) {
            obs_data_set_string(settings, "palette_export_path", full_path);
        }

        // Repopulate the palette dropdown in case export created a new palette
        obs_property_t *palette_prop = obs_properties_get(props, C64_PALETTE_KEY);
        if (palette_prop) {
            c64_palette_populate_list(palette_prop);
        }

        // Remember the corrected path so we don't re-export if callback fires again
        obs_data_set_string(settings, C64_PALETTE_EXPORT_LAST_KEY, full_path);
    } else {
        C64_LOG_WARNING("Failed to export palette to: %s", full_path);
    }

    // Keep the path visible so user can see what was exported
    // (Path persists until next export operation)

    return true; // Refresh UI
}

static bool palette_delete_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context || !context->source) {
        return false;
    }

    obs_data_t *settings = obs_source_get_settings(context->source);
    if (!settings) {
        return false;
    }

    const char *palette_id = obs_data_get_string(settings, C64_PALETTE_KEY);
    if (!palette_id || !palette_id[0]) {
        obs_data_release(settings);
        return false;
    }

    // Cannot delete presets
    if (c64_palette_is_preset(palette_id)) {
        C64_LOG_WARNING("Cannot delete preset palette: %s", palette_id);
        obs_data_release(settings);
        return false;
    }

    // Delete the palette
    bool ok = c64_palette_delete(palette_id);

    if (ok) {
        // Switch to Default palette
        obs_data_set_string(settings, C64_PALETTE_KEY, "Default");

        // Clear any stale export path
        obs_data_erase(settings, "palette_export_path");

        // IMPORTANT: Erase all color keys first, then set to Default
        // This ensures no stale values remain in OBS settings cache
        for (int i = 0; i < 16; i++) {
            char key[32];
            snprintf(key, sizeof(key), "palette_color_%d", i);
            obs_data_erase(settings, key);
        }

        // Now set Default palette colors
        // Working colors are now Default because c64_palette_delete() selected it
        uint32_t *default_colors = c64_palette_get_working_colors();
        if (default_colors) {
            for (int i = 0; i < 16; i++) {
                char key[32];
                snprintf(key, sizeof(key), "palette_color_%d", i);

                // Convert BGRA to OBS color format (ABGR stored as int)
                uint32_t obs_color = c64_bgra_to_obs_color(default_colors[i]);

                // Set actual value after erasing to ensure clean slate
                obs_data_set_int(settings, key, (long long)obs_color);
            }
        }

        obs_source_update(context->source, settings);

        // Force save settings to OBS config immediately
        // This prevents OBS from overwriting our changes when the properties dialog closes
        obs_source_save(context->source);

        // Repopulate the palette dropdown
        obs_property_t *palette_prop = obs_properties_get(props, C64_PALETTE_KEY);
        if (palette_prop) {
            c64_palette_populate_list(palette_prop);
        }

        // Update color pickers to show Default colors
        update_palette_color_properties(settings);
    }

    obs_data_release(settings);
    return true; // Refresh UI
}

static bool palette_color_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(data);

    if (!settings || !property) {
        return false;
    }

    // Get the property name to determine which color index
    const char *name = obs_property_name(property);
    if (!name || strncmp(name, "palette_color_", 14) != 0) {
        return false;
    }

    int index = atoi(name + 14);
    if (index < 0 || index >= 16) {
        return false;
    }

    // Get the color value from settings
    long long obs_color = obs_data_get_int(settings, name);
    // OBS color format: 0xAABBGGRR (ABGR)
    uint8_t b = (obs_color >> 16) & 0xFF;
    uint8_t g = (obs_color >> 8) & 0xFF;
    uint8_t r = obs_color & 0xFF;

    // Maintain ABGR format (0xFFBBGGRR) used by the palette system
    uint32_t color = 0xFF000000 | (b << 16) | (g << 8) | r;

    // Never auto-save during initialization
    // This prevents spurious "Default (Custom)" palette creation on Windows
    // where OBS may trigger color callbacks before working colors are properly initialized
    if (obs_data_get_bool(settings, C64_PALETTE_INITIALIZING_KEY)) {
        // Still in initialization phase - update working color but don't save
        c64_palette_set_working_color(index, color);
        return false;
    }

    // IMPORTANT: Only update and save if the color actually CHANGED from current working color
    // This prevents spurious auto-saves when properties dialog is opened and OBS triggers
    // callbacks during initialization with stale color values from settings
    uint32_t *working_colors = c64_palette_get_working_colors();
    if (working_colors && working_colors[index] == color) {
        // Color is the same - no change needed, don't trigger auto-save
        return false;
    }

    // Update working color
    c64_palette_set_working_color(index, color);

    // Immediately save the palette:
    // - For presets: Creates $preset-custom.vpl with name "$Preset (Custom)"
    // - For custom palettes: Overwrites in place
    // This updates settings with the new palette ID if a custom copy was created
    bool palette_changed = c64_palette_auto_save(settings);

    // Refresh UI if palette was converted from preset to custom
    // This updates the dropdown to show the new custom palette
    if (palette_changed && props) {
        obs_property_t *palette_prop = obs_properties_get(props, C64_PALETTE_KEY);
        if (palette_prop) {
            // Repopulate the palette list to include the new custom palette
            c64_palette_populate_list(palette_prop);
        }
    }

    return palette_changed; // Refresh UI if palette was converted to custom
}

// Helper function to trim whitespace from both ends of a string
static void trim_config_string(char *str)
{
    if (!str || !*str)
        return;

    // Trim leading whitespace
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n'))
        start++;

    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        end--;

    // Copy trimmed string back
    size_t len = (end - start) + 1;
    memmove(str, start, len);
    str[len] = '\0';
}

bool c64_load_configuration(obs_data_t *settings)
{
    if (!settings) {
        C64_LOG_WARNING("Cannot load configuration - invalid settings object");
        return false;
    }

    // Get the plugin data directory path
    char *plugin_data_path = obs_module_file("properties.ini");
    if (!plugin_data_path) {
        C64_LOG_WARNING("Failed to get plugin data path for properties.ini");
        return false;
    }

    FILE *file = fopen(plugin_data_path, "r");
    bfree(plugin_data_path);

    if (!file) {
        C64_LOG_INFO("No properties.ini found, using built-in defaults");
        return false;
    }

    C64_LOG_INFO("Loading configuration from properties.ini");

    char line[512];
    char current_section[64] = "";
    int loaded_settings = 0;
    bool ci_enforced = false; // When true (CI), apply values as defaults AND directly

    while (fgets(line, sizeof(line), file)) {
        trim_config_string(line);

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#') {
            continue;
        }

        // Check for section header [SectionName]
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, line + 1, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
                C64_LOG_DEBUG("Processing configuration section: %s", current_section);
            }
            continue;
        }

        // Parse key=value pairs
        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }

        *equals = '\0';
        char *key = line;
        char *value = equals + 1;

        trim_config_string(key);
        trim_config_string(value);

        C64_LOG_INFO("Properties: Processing key='%s' value='%s'", key, value);

        // Detect CI enforcement flag inside [ci] section
        if (strcmp(key, "is_ci") == 0) {
            bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
            if (enabled) {
                ci_enforced = true;
                C64_LOG_INFO("CI mode detected in properties.ini - enforcing values as defaults and direct");
            }
            continue;
        }

        // Apply configuration based on key name
        if (strcmp(key, "c64_host") == 0) {
            c64_set_string(settings, "c64_host", value, ci_enforced);
            C64_LOG_INFO("Config: c64_host = %s", value);
            loaded_settings++;
        } else if (strcmp(key, "dns_server_ip") == 0) {
            c64_set_string(settings, "dns_server_ip", value, ci_enforced);
            C64_LOG_DEBUG("Config: dns_server_ip = %s", value);
            loaded_settings++;
        } else if (strcmp(key, "video_port") == 0) {
            int port = atoi(value);
            if (port >= 1024 && port <= 65535) {
                c64_set_int(settings, "video_port", port, ci_enforced);
                C64_LOG_DEBUG("Config: video_port = %d", port);
                loaded_settings++;
            }
        } else if (strcmp(key, "audio_port") == 0) {
            int port = atoi(value);
            if (port >= 1024 && port <= 65535) {
                c64_set_int(settings, "audio_port", port, ci_enforced);
                C64_LOG_DEBUG("Config: audio_port = %d", port);
                loaded_settings++;
            }
        } else if (strcmp(key, "control_port") == 0) {
            int port = atoi(value);
            if (port >= 64 && port <= 65535) {
                c64_set_int(settings, "control_port", port, ci_enforced);
                C64_LOG_DEBUG("Config: control_port = %d", port);
                loaded_settings++;
            }
        } else if (strcmp(key, "auto_detect_ip") == 0) {
            bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
            c64_set_bool(settings, "auto_detect_ip", enabled, ci_enforced);
            C64_LOG_DEBUG("Config: auto_detect_ip = %s", enabled ? "true" : "false");
            loaded_settings++;
        } else if (strcmp(key, "obs_ip_address") == 0) {
            // Only apply non-empty value. Empty means "use auto-detect default".
            if (value && value[0] != '\0') {
                c64_set_string(settings, "obs_ip_address", value, ci_enforced);
                C64_LOG_INFO("Config: obs_ip_address = %s", value);
                loaded_settings++;
            }
        } else if (strcmp(key, "c64_password") == 0) {
            // Password is intentionally excluded from import for security.
            // Users must re-enter passwords after importing configuration.
            C64_LOG_INFO("Config import: skipping c64_password (excluded for security)");
        } else if (strcmp(key, "buffer_delay_ms") == 0) {
            int delay = atoi(value);
            if (delay >= 0 && delay <= 500) {
                c64_set_int(settings, "buffer_delay_ms", delay, ci_enforced);
                C64_LOG_DEBUG("Config: buffer_delay_ms = %d", delay);
                loaded_settings++;
            }
        } else if (strcmp(key, "debug_logging") == 0) {
            bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
            c64_set_bool(settings, "debug_logging", enabled, ci_enforced);
            C64_LOG_DEBUG("Config: debug_logging = %s", enabled ? "true" : "false");
            loaded_settings++;
        } else if (strcmp(key, "record_frames") == 0) {
            bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
            c64_set_bool(settings, "record_frames", enabled, ci_enforced);
            C64_LOG_DEBUG("Config: record_frames = %s", enabled ? "true" : "false");
            loaded_settings++;
        } else if (strcmp(key, "record_video") == 0) {
            bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
            if (ci_enforced) {
                // Enforce both default and direct on CI
                obs_data_set_default_bool(settings, "record_video", enabled);
            }
            obs_data_set_bool(settings, "record_video", enabled);
            C64_LOG_DEBUG("Config: record_video = %s%s", enabled ? "true" : "false",
                          ci_enforced ? " (default+direct)" : " (direct)");
            loaded_settings++;
        } else if (strcmp(key, "record_csv") == 0) {
            bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
            if (ci_enforced) {
                obs_data_set_default_bool(settings, "record_csv", enabled);
            }
            obs_data_set_bool(settings, "record_csv", enabled);
            C64_LOG_INFO("Config: record_csv = %s%s (value='%s')", enabled ? "true" : "false",
                         ci_enforced ? " (default+direct)" : " (direct)", value);
            loaded_settings++;
        } else if (strcmp(key, "record_av_sync") == 0) {
            bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
            if (ci_enforced) {
                obs_data_set_default_bool(settings, "record_av_sync", enabled);
            }
            obs_data_set_bool(settings, "record_av_sync", enabled);
            C64_LOG_DEBUG("Config: record_av_sync = %s%s", enabled ? "true" : "false",
                          ci_enforced ? " (default+direct)" : " (direct)");
            loaded_settings++;
        } else if (strcmp(key, "save_folder") == 0 && strlen(value) > 0) {
            c64_set_string(settings, "save_folder", value, ci_enforced);
            C64_LOG_DEBUG("Config: save_folder = %s", value);
            loaded_settings++;
        }
        // Note: CRT effects can be configured here too if needed in the future
    }

    fclose(file);
    C64_LOG_INFO("Configuration loaded successfully: %d settings applied", loaded_settings);
    return true;
}
