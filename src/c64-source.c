/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include <obs-module.h>
#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif
#include <graphics/graphics.h>
#include <util/platform.h>
#include <util/threading.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/stat.h>
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#include <ctype.h>
#include "c64-network.h"
#include "c64-network-buffer.h"
#include "c64-logging.h"
#include "c64-source.h"
#include "c64-types.h"
#include "c64-protocol.h"
#include "c64-stream-control.h"
#include "c64-video.h"
#include "c64-color.h"
#include "c64-audio.h"
#include "c64-interact-key.h"
#include "c64-logo.h"
#include "c64-record.h"
#include "c64-version.h"
#include "c64-properties.h"
#include "c64-file.h"
#include "c64-palette.h"
#include "device/c64-device.h"
#include "c64-keyboard.h"
#include "c64-rest-client.h"
#include "c64-script-executor.h"
#include "c64-automation.h"
#include "plugin-support.h"
#include "c64-effect.h"
#include "c64-effect-geometry.h"
#include "c64-av-sync.h"
#include "c64-network-fifo.h"

// Forward declarations
static void close_and_reset_sockets(struct c64_source *context);
static void c64_schedule_retry(struct c64_source *context, const char *reason);
static void c64_refresh_resolved_ip(struct c64_source *context);
static void c64_set_expected_peer_ip(struct c64_source *context, const char *ip_string);
static void c64_attempt_script_autostart(struct c64_source *context, obs_data_t *settings);
static void c64_queue_properties_refresh(struct c64_source *context);
static void c64_rebuild_rest_client(struct c64_source *context);

static void c64_rebuild_rest_client(struct c64_source *context)
{
    if (!context) {
        return;
    }
    if (context->keyboard) {
        c64_keyboard_destroy(context->keyboard);
        context->keyboard = NULL;
    }
    if (context->rest_client) {
        c64_rest_client_destroy(context->rest_client);
        context->rest_client = NULL;
    }
    context->rest_base_url[0] = '\0';
    if (!context->ip_address[0] || !strcmp(context->ip_address, "0.0.0.0")) {
        return;
    }
    snprintf(context->rest_base_url, sizeof(context->rest_base_url), "http://%s", context->ip_address);
    context->rest_client = c64_rest_client_create(context->rest_base_url, context->c64_password);
    if (context->rest_client) {
        context->keyboard = c64_keyboard_create(context->rest_client);
        if (context->keyboard) {
            c64_keyboard_set_keymap(context->keyboard, context->keymap);
        }
        c64_record_on_rest_client_ready(context);
    }
}

static const char *C64_PRESET_LAST_APPLIED_KEY = "crt_preset_last_applied";
static const char *const C64_SOURCE_SAVED_SETTING_KEYS[] = {
    "debug_logging",      "auto_detect_ip",
    "dns_server_ip",      "c64_host",
    "c64_password",       "stream_control_transport",
    "obs_ip_address",     "video_port",
    "audio_port",         "control_port",
    "buffer_delay_ms",    "script_auto_start",
    "script_file",        "record_frames",
    "save_folder",        "record_video",
    "record_csv",         "record_av_sync",
    "crt_preset",         "scan_line_distance",
    "scan_line_strength", "pixel_width",
    "pixel_height",       "blur_strength",
    "bloom_strength",     "afterglow_duration_ms",
    "afterglow_curve",    "tint_mode",
    "tint_strength",      "palette",
    "keyboard_keymap",    "file_system",
    "playback_source",    "include_subfolders",
    "shuffle_playback",   "automation_duration",
    "automation_reset",   "automation_use_songlengths",
    "local_folder_path",  "file_source",
    "automation_path",    "automation_shuffle",
};

static bool c64_source_settings_have_effect_overrides(obs_data_t *settings)
{
    if (!settings) {
        return false;
    }

    return obs_data_has_user_value(settings, "scan_line_distance") ||
           obs_data_has_user_value(settings, "scan_line_strength") ||
           obs_data_has_user_value(settings, "pixel_width") || obs_data_has_user_value(settings, "pixel_height") ||
           obs_data_has_user_value(settings, "blur_strength") || obs_data_has_user_value(settings, "bloom_strength") ||
           obs_data_has_user_value(settings, "tint_mode") || obs_data_has_user_value(settings, "tint_strength") ||
           obs_data_has_user_value(settings, "afterglow_duration_ms") ||
           obs_data_has_user_value(settings, "afterglow_curve");
}

static bool c64_source_apply_crt_preset_if_needed(obs_data_t *settings, const char *preset_name, bool is_update)
{
    if (!settings || !preset_name || preset_name[0] == '\0') {
        return false;
    }

    const char *last_applied = obs_data_get_string(settings, C64_PRESET_LAST_APPLIED_KEY);
    const bool preset_changed = !last_applied || last_applied[0] == '\0' || strcmp(last_applied, preset_name) != 0;
    if (!preset_changed) {
        return false;
    }

    if (!last_applied || last_applied[0] == '\0') {
        if (c64_effect_matches_preset(settings, preset_name)) {
            obs_data_set_string(settings, C64_PRESET_LAST_APPLIED_KEY, preset_name);
            return false;
        }
    }

    if (!is_update && (!last_applied || last_applied[0] == '\0') &&
        c64_source_settings_have_effect_overrides(settings)) {
        C64_LOG_INFO("" EFFECT_LOG_PREFIX " Skipping preset auto-apply; custom effect overrides detected");
        return false;
    }

    if (!c64_effect_apply(settings, preset_name)) {
        if (is_update) {
            C64_LOG_WARNING("" EFFECT_LOG_PREFIX " CRT preset in update not found: %s", preset_name);
        } else {
            C64_LOG_WARNING("" EFFECT_LOG_PREFIX " CRT preset not found: %s", preset_name);
        }
        return false;
    }

    obs_data_set_string(settings, C64_PRESET_LAST_APPLIED_KEY, preset_name);
    if (is_update) {
        C64_LOG_INFO("Applied CRT preset on update: %s", preset_name);
    } else {
        C64_LOG_INFO("Applied CRT preset from settings: %s", preset_name);
    }
    return true;
}

static float c64_obs_data_get_double_or_current(obs_data_t *settings, const char *key, float current_value)
{
    if (!settings || !key) {
        return current_value;
    }

    return obs_data_has_user_value(settings, key) ? (float)obs_data_get_double(settings, key) : current_value;
}

static int c64_obs_data_get_int_or_current(obs_data_t *settings, const char *key, int current_value)
{
    if (!settings || !key) {
        return current_value;
    }

    return obs_data_has_user_value(settings, key) ? (int)obs_data_get_int(settings, key) : current_value;
}

static bool c64_try_get_prefer_pal_from_obs_fps(bool *prefer_pal)
{
    struct obs_video_info ovi;
    if (!prefer_pal) {
        return false;
    }
    if (!obs_get_video_info(&ovi) || ovi.fps_den == 0) {
        return false;
    }
    double fps = (double)ovi.fps_num / (double)ovi.fps_den;
    *prefer_pal = (fps < 55.0);
    return true;
}

static void c64_source_get_effect_geometry(const struct c64_source *context, struct c64_effect_geometry *geometry)
{
    c64_effect_geometry_init(geometry, context ? context->width : 0, context ? context->height : 0,
                             context ? context->pixel_width : 1.0f, context ? context->pixel_height : 1.0f,
                             context ? context->scan_line_distance : 0.0f, context ? context->preserve_size : false);
}

static void c64_apply_format_hint(struct c64_source *context, bool prefer_pal)
{
    if (!context) {
        return;
    }
    context->width = C64_PAL_WIDTH;
    context->height = prefer_pal ? C64_PAL_HEIGHT : C64_NTSC_HEIGHT;
    context->expected_fps = prefer_pal ? 50.125 : 59.826;
    context->frame_interval_ns = prefer_pal ? C64_PAL_FRAME_INTERVAL_NS : C64_NTSC_FRAME_INTERVAL_NS;

    // Set format-specific audio sample rate (derived from color subcarrier)
    context->audio_sample_rate = prefer_pal ? C64_PAL_AUDIO_SAMPLE_RATE : C64_NTSC_AUDIO_SAMPLE_RATE;
    context->audio_info.samples_per_sec = (uint32_t)context->audio_sample_rate;

    c64_logo_set_format_preference(context, prefer_pal);
}

static void c64_update_format_hint_if_needed(struct c64_source *context)
{
    if (!context || context->format_detected || context->format_hint_set) {
        return;
    }
    bool prefer_pal = false;
    if (!c64_try_get_prefer_pal_from_obs_fps(&prefer_pal)) {
        return;
    }
    c64_apply_format_hint(context, prefer_pal);
    context->format_hint_set = true;
}

// Async retry task - runs in OBS thread pool (NOT render thread)
void c64_async_retry_task(void *data)
{
    struct c64_source *context = (struct c64_source *)data;

    if (!context) {
        C64_LOG_WARNING("Async retry task called with NULL context");
        return;
    }

    C64_LOG_INFO("" NETWORK_LOG_PREFIX " Async retry attempt %u - %s", context->retry_count,
                 context->streaming ? "sending start commands" : "starting streaming");

    // Resolve hostname -> IP in the background (never do DNS on the OBS UI thread).
    c64_refresh_resolved_ip(context);

    bool tcp_success = false;

    if (!context->streaming) {
        // Initial streaming start - full setup with fresh UDP sockets
        c64_start_streaming(context);
        tcp_success = true; // c64_start_streaming handles TCP commands internally
    } else {
        // Already streaming - test connectivity and send start commands
        // Use quick connectivity test instead of recreating sockets (avoids race conditions)
        if (c64_test_connectivity(context->ip_address, context->control_port)) {
            // We are explicitly requesting the peer to (re)start streaming now.
            context->last_start_command_time_ns = os_gettime_ns();
            char video_dest[C64_STREAM_DEST_MAX];
            char audio_dest[C64_STREAM_DEST_MAX];
            if (c64_build_stream_dest(video_dest, sizeof(video_dest), context->obs_ip_address, context->video_port) &&
                c64_build_stream_dest(audio_dest, sizeof(audio_dest), context->obs_ip_address, context->audio_port) &&
                c64_stream_control(context, true, 0, video_dest) && c64_stream_control(context, true, 1, audio_dest)) {
                tcp_success = true;
                context->consecutive_failures = 0; // Reset failure counter on success
            } else {
                tcp_success = false;
                context->consecutive_failures++;
            }
        } else {
            tcp_success = false;
            context->consecutive_failures++;
        }
    }

    context->retry_count++;

    if (!tcp_success) {
        C64_LOG_DEBUG("TCP connection failed (%u consecutive failures)", context->consecutive_failures);
    }

    // Always clear retry state to allow future retries
    // The video thread will enforce timing between retry attempts
    os_atomic_set_long(&context->retry_in_progress, 0);
}

static void *c64_retry_thread_main(void *arg)
{
    struct c64_source *context = (struct c64_source *)arg;
    c64_async_retry_task(context);
    os_atomic_set_long(&context->retry_thread_active, 0);
    return NULL;
}

static void c64_schedule_retry(struct c64_source *context, const char *reason)
{
    if (!context)
        return;

    if (os_atomic_load_long(&context->retry_in_progress) || os_atomic_load_long(&context->retry_thread_active)) {
        C64_LOG_DEBUG("Retry already in progress, skipping (%s)", reason ? reason : "no reason");
        return;
    }

    // Reserve the retry slot atomically to prevent concurrent thread creation.
    if (!os_atomic_compare_swap_long(&context->retry_thread_active, 0, 1)) {
        return;
    }
    if (!os_atomic_compare_swap_long(&context->retry_in_progress, 0, 1)) {
        os_atomic_set_long(&context->retry_thread_active, 0);
        return;
    }

    int err = pthread_create(&context->retry_thread, NULL, c64_retry_thread_main, context);
    if (err != 0) {
        os_atomic_set_long(&context->retry_in_progress, 0);
        os_atomic_set_long(&context->retry_thread_active, 0);
        C64_LOG_WARNING("Failed to start retry thread (%s)", reason ? reason : "no reason");
    } else {
        C64_LOG_DEBUG("Scheduled background retry (%s)", reason ? reason : "no reason");
    }
}

void c64_schedule_retry_task(struct c64_source *context, const char *reason)
{
    c64_schedule_retry(context, reason);
}

void *c64_source_get_rest_client(struct c64_source *context)
{
    if (!context) {
        return NULL;
    }
    return context->rest_client;
}

void *c64_source_get_keyboard(struct c64_source *context)
{
    if (!context) {
        return NULL;
    }
    return context->keyboard;
}

typedef struct {
    char path[512];
} c64_last_screenshot_query_t;

typedef struct {
    obs_source_t *source;
    bool preview;
} c64_take_screenshot_request_t;

#ifdef ENABLE_FRONTEND_API
static void c64_source_ui_get_last_screenshot(void *data)
{
    c64_last_screenshot_query_t *query = (c64_last_screenshot_query_t *)data;
    if (!query) {
        return;
    }

    query->path[0] = '\0';
    char *last = obs_frontend_get_last_screenshot();
    if (last) {
        snprintf(query->path, sizeof(query->path), "%s", last);
        bfree(last);
    }
}

static void c64_source_ui_take_frontend_screenshot(void *data)
{
    c64_take_screenshot_request_t *request = (c64_take_screenshot_request_t *)data;
    if (!request) {
        return;
    }

    if (request->preview) {
        obs_frontend_take_screenshot();
    } else if (request->source) {
        obs_frontend_take_source_screenshot(request->source);
    }
}
#endif

static bool c64_source_copy_file(const char *src_path, const char *dst_path)
{
    if (!src_path || !dst_path) {
        return false;
    }

    FILE *src = fopen(src_path, "rb");
    if (!src) {
        return false;
    }

    FILE *dst = fopen(dst_path, "wb");
    if (!dst) {
        fclose(src);
        return false;
    }

    char buffer[64 * 1024];
    bool ok = true;
    while (!feof(src)) {
        size_t read_count = fread(buffer, 1, sizeof(buffer), src);
        if (read_count > 0 && fwrite(buffer, 1, read_count, dst) != read_count) {
            ok = false;
            break;
        }
        if (ferror(src)) {
            ok = false;
            break;
        }
    }

    fclose(dst);
    fclose(src);
    return ok;
}

static bool c64_source_wait_for_stable_file(const char *path, uint64_t deadline_ns, char *error_msg, size_t error_size)
{
    if (!path || path[0] == '\0') {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Invalid screenshot path");
        }
        return false;
    }

    int stable_polls = 0;
    int64_t last_size = -1;
    time_t last_mtime = (time_t)0;

    while (os_gettime_ns() < deadline_ns) {
        struct stat st;
        if (os_stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
            const int64_t current_size = (int64_t)st.st_size;
            const time_t current_mtime = st.st_mtime;

            if (current_size == last_size && current_mtime == last_mtime) {
                stable_polls++;
            } else {
                stable_polls = 1;
                last_size = current_size;
                last_mtime = current_mtime;
            }

            if (stable_polls >= 3) {
                return true;
            }
        } else {
            stable_polls = 0;
            last_size = -1;
            last_mtime = (time_t)0;
        }

        os_sleep_ms(20);
    }

    if (error_msg && error_size > 0) {
        snprintf(error_msg, error_size, "Timed out waiting for OBS screenshot file to stabilize");
    }
    return false;
}

static bool c64_source_ensure_parent_directory(const char *path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

    char directory[1024];
    snprintf(directory, sizeof(directory), "%s", path);

    char *last_slash = strrchr(directory, '/');
#ifdef _WIN32
    char *last_backslash = strrchr(directory, '\\');
    if (!last_slash || (last_backslash && last_backslash > last_slash)) {
        last_slash = last_backslash;
    }
#endif
    if (!last_slash) {
        return true;
    }

    *last_slash = '\0';
    if (directory[0] == '\0') {
        return true;
    }

    return c64_create_directory_recursive(directory);
}

bool c64_source_script_wait_rendered_frames(struct c64_source *context, uint32_t frame_count, char *error_msg,
                                            size_t error_size)
{
    if (!context) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Source context not available");
        }
        return false;
    }

    if (frame_count == 0) {
        return true;
    }

    const long start_count = os_atomic_load_long(&context->script_render_count);
    const long target_count = start_count + (long)frame_count;
    const uint64_t deadline_ns = os_gettime_ns() + 10000000000ull;

    while (os_gettime_ns() < deadline_ns) {
        if (os_atomic_load_long(&context->script_render_count) >= target_count) {
            return true;
        }
        os_sleep_ms(5);
    }

    if (error_msg && error_size > 0) {
        snprintf(error_msg, error_size, "Timed out waiting for %u rendered frame(s)", frame_count);
    }
    return false;
}

bool c64_source_script_take_frontend_screenshot(struct c64_source *context, bool preview, const char *output_path,
                                                char *error_msg, size_t error_size)
{
#ifndef ENABLE_FRONTEND_API
    UNUSED_PARAMETER(context);
    UNUSED_PARAMETER(preview);
    UNUSED_PARAMETER(output_path);
    if (error_msg && error_size > 0) {
        snprintf(error_msg, error_size, "OBS frontend API not enabled");
    }
    return false;
#else
    if (!context || !output_path || output_path[0] == '\0') {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Invalid screenshot request");
        }
        return false;
    }

    c64_last_screenshot_query_t before = {0};
    obs_queue_task(OBS_TASK_UI, c64_source_ui_get_last_screenshot, &before, true);

    c64_take_screenshot_request_t request = {
        .source = context->source,
        .preview = preview,
    };
    obs_queue_task(OBS_TASK_UI, c64_source_ui_take_frontend_screenshot, &request, true);

    const uint64_t deadline_ns = os_gettime_ns() + 10000000000ull;
    while (os_gettime_ns() < deadline_ns) {
        c64_last_screenshot_query_t after = {0};
        obs_queue_task(OBS_TASK_UI, c64_source_ui_get_last_screenshot, &after, true);

        const bool changed = after.path[0] != '\0' && (before.path[0] == '\0' || strcmp(before.path, after.path) != 0);
        if (changed) {
            c64_path_kind_t kind = C64_PATH_KIND_MISSING;
            if (c64_get_path_kind(after.path, &kind) && kind == C64_PATH_KIND_FILE) {
                if (!c64_source_wait_for_stable_file(after.path, deadline_ns, error_msg, error_size)) {
                    return false;
                }

                if (!c64_source_ensure_parent_directory(output_path)) {
                    if (error_msg && error_size > 0) {
                        snprintf(error_msg, error_size, "Failed to create screenshot directory: %s", output_path);
                    }
                    return false;
                }

                if (c64_source_copy_file(after.path, output_path)) {
                    return true;
                }
            }
        }

        os_sleep_ms(20);
    }

    if (error_msg && error_size > 0) {
        snprintf(error_msg, error_size, "Timed out waiting for OBS screenshot output");
    }
    return false;
#endif
}

static void c64_refresh_resolved_ip(struct c64_source *context)
{
    if (!context)
        return;

    // Take a thread-safe snapshot of hostname and dns_server_ip to avoid data races
    // (these fields may be written by c64_update on the OBS UI thread concurrently).
    char hostname_copy[64];
    char dns_copy[64];

    pthread_mutex_lock(&context->config_mutex);
    strncpy(hostname_copy, context->hostname, sizeof(hostname_copy) - 1);
    hostname_copy[sizeof(hostname_copy) - 1] = '\0';
    strncpy(dns_copy, context->dns_server_ip, sizeof(dns_copy) - 1);
    dns_copy[sizeof(dns_copy) - 1] = '\0';
    pthread_mutex_unlock(&context->config_mutex);

    // If hostname is empty, nothing to do.
    if (hostname_copy[0] == '\0')
        return;

    // Always try to resolve; c64_resolve_hostname_with_dns is fast for numeric IPs.
    char resolved[64];
    resolved[0] = '\0';

    const char *dns = (dns_copy[0] != '\0') ? dns_copy : NULL;
    if (c64_resolve_hostname_with_dns(hostname_copy, dns, resolved, sizeof(resolved))) {
        pthread_mutex_lock(&context->config_mutex);
        if (strcmp(context->ip_address, resolved) != 0) {
            strncpy(context->ip_address, resolved, sizeof(context->ip_address) - 1);
            context->ip_address[sizeof(context->ip_address) - 1] = '\0';
            C64_LOG_INFO("" NETWORK_LOG_PREFIX " Resolved C64 host '%s' -> %s", hostname_copy, context->ip_address);
        }
        c64_set_expected_peer_ip(context, context->ip_address);
        pthread_mutex_unlock(&context->config_mutex);
    } else {
        // Keep ip_address as-is (may be hostname) and let connectivity checks fail fast.
        C64_LOG_DEBUG("" NETWORK_LOG_PREFIX " Hostname resolution failed for '%s' (dns=%s)", hostname_copy,
                      dns ? dns : "system");
    }
}

// Helper function to safely close and reset sockets
static void close_and_reset_sockets(struct c64_source *context)
{
    if (context->video_socket != INVALID_SOCKET_VALUE) {
        C64_LOG_DEBUG("Closing video socket (port %u)", context->video_port);
        close(context->video_socket);
        context->video_socket = INVALID_SOCKET_VALUE;
        C64_LOG_DEBUG("Video socket closed and reset to INVALID_SOCKET_VALUE");
    }
    if (context->audio_socket != INVALID_SOCKET_VALUE) {
        C64_LOG_DEBUG("Closing audio socket (port %u)", context->audio_port);
        close(context->audio_socket);
        context->audio_socket = INVALID_SOCKET_VALUE;
        C64_LOG_DEBUG("Audio socket closed and reset to INVALID_SOCKET_VALUE");
    }
}

void *c64_create(obs_data_t *settings, obs_source_t *source)
{
    C64_LOG_INFO("Creating C64 Stream source");

    // C64 Stream source creation

    // Initialize networking on first use
    static bool networking_initialized = false;
    if (!networking_initialized) {
        if (!c64_init_networking()) {
            C64_LOG_ERROR("Failed to initialize networking");
            return NULL;
        }
        networking_initialized = true;
    }

    // Initialize color conversion optimization on first use
    static bool color_lut_initialized = false;
    if (!color_lut_initialized) {
        c64_init_color_conversion_lut();
        color_lut_initialized = true;
    }

    struct c64_source *context = bzalloc(sizeof(struct c64_source));
    if (!context) {
        C64_LOG_ERROR("Failed to allocate memory for source context");
        return NULL;
    }

    context->preserve_size = c64_effect_settings_resolve_preserve_size(settings, C64_SOURCE_SAVED_SETTING_KEYS,
                                                                       sizeof(C64_SOURCE_SAVED_SETTING_KEYS) /
                                                                           sizeof(C64_SOURCE_SAVED_SETTING_KEYS[0]));

    // Load configuration file before initializing settings-dependent values
    c64_load_configuration(settings);
    c64_device_registry_migrate_legacy(settings);
    c64_device_registry_apply_selected(settings);

    const char *initial_preset = obs_data_get_string(settings, "crt_preset");
    c64_source_apply_crt_preset_if_needed(settings, initial_preset, false);

    context->source = source;

    c64_av_sync_init(context);

    // Initialize configuration from settings
    const char *host = obs_data_get_string(settings, "c64_host");
    const char *hostname = host ? host : C64_DEFAULT_HOST;

    const char *c64_password = obs_data_get_string(settings, "c64_password");
    if (c64_password && c64_password[0] != '\0') {
        strncpy(context->c64_password, c64_password, sizeof(context->c64_password) - 1);
        context->c64_password[sizeof(context->c64_password) - 1] = '\0';
    } else {
        context->c64_password[0] = '\0';
    }

    // Store the original hostname/IP as entered by user
    strncpy(context->hostname, hostname, sizeof(context->hostname) - 1);
    context->hostname[sizeof(context->hostname) - 1] = '\0';

    // Store DNS server IP (resolution happens asynchronously in background thread).
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");
    if (dns_server_ip && dns_server_ip[0] != '\0') {
        strncpy(context->dns_server_ip, dns_server_ip, sizeof(context->dns_server_ip) - 1);
        context->dns_server_ip[sizeof(context->dns_server_ip) - 1] = '\0';
    } else {
        context->dns_server_ip[0] = '\0';
    }

    // IMPORTANT: do not do DNS resolution in c64_create (OBS UI thread).
    // Initialize ip_address to the user-provided hostname; it will be resolved in the background.
    strncpy(context->ip_address, hostname, sizeof(context->ip_address) - 1);
    context->ip_address[sizeof(context->ip_address) - 1] = '\0';

    context->auto_detect_ip = obs_data_get_bool(settings, "auto_detect_ip");
    context->video_port = (uint32_t)obs_data_get_int(settings, "video_port");
    context->audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");
    context->control_port = (uint32_t)obs_data_get_int(settings, "control_port");
    context->streaming = false;

    // Initialize OBS IP address from settings or auto-detect if enabled
    memset(context->obs_ip_address, 0, sizeof(context->obs_ip_address));
    const char *saved_obs_ip = obs_data_get_string(settings, "obs_ip_address");

    if (saved_obs_ip && strlen(saved_obs_ip) > 0) {
        // Use previously saved/configured OBS IP address
        strncpy(context->obs_ip_address, saved_obs_ip, sizeof(context->obs_ip_address) - 1);
        context->initial_ip_detected = true;
        C64_LOG_INFO("" NETWORK_LOG_PREFIX " Using configured OBS IP address: %s", context->obs_ip_address);
    } else if (context->auto_detect_ip) {
        // Auto-detect local IP address only if auto-detection is enabled
        if (c64_detect_local_ip_for_host(context->hostname, context->dns_server_ip, context->obs_ip_address,
                                         sizeof(context->obs_ip_address)) ||
            c64_detect_local_ip(context->obs_ip_address, sizeof(context->obs_ip_address))) {
            C64_LOG_INFO("" NETWORK_LOG_PREFIX " Auto-detected OBS IP address: %s", context->obs_ip_address);
            context->initial_ip_detected = true;
            // Save the detected IP to settings for future use
            obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
        } else {
            C64_LOG_WARNING("" NETWORK_LOG_PREFIX " Failed to auto-detect OBS IP address, will use localhost fallback");
            context->initial_ip_detected = false;
        }
    } else {
        C64_LOG_INFO("" NETWORK_LOG_PREFIX " Auto-detection disabled, will use localhost fallback");
        context->initial_ip_detected = false;
    }

    // Ensure we have a valid OBS IP address - use localhost as last resort
    if (strlen(context->obs_ip_address) == 0) {
        C64_LOG_INFO("" NETWORK_LOG_PREFIX " No OBS IP configured, using localhost as fallback");
        strncpy(context->obs_ip_address, "127.0.0.1", sizeof(context->obs_ip_address) - 1);
        obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
    }

    // Set default ports if not configured
    if (context->video_port == 0)
        context->video_port = C64_DEFAULT_VIDEO_PORT;
    if (context->audio_port == 0)
        context->audio_port = C64_DEFAULT_AUDIO_PORT;

    bool prefer_pal = false;
    bool have_obs_hint = c64_try_get_prefer_pal_from_obs_fps(&prefer_pal);

    // Initialize video format (match OBS FPS when possible, then refine on stream detection)
    context->width = C64_PAL_WIDTH;
    context->height = prefer_pal ? C64_PAL_HEIGHT : C64_NTSC_HEIGHT;
    context->format_hint_set = have_obs_hint;

    // Allocate single frame buffer for direct async video output (RGBA, 4 bytes per pixel)
    size_t frame_size = C64_PAL_WIDTH * C64_PAL_HEIGHT * sizeof(uint32_t);
    context->frame_buffer = bmalloc(frame_size);
    if (!context->frame_buffer) {
        C64_LOG_ERROR("Failed to allocate frame buffer");
        return NULL;
    }
    memset(context->frame_buffer, 0, frame_size);

    // Allocate pre-allocated recording buffers to eliminate malloc/free in hot paths
    context->recording_buffer_size = frame_size;                             // Same as RGBA frame size
    context->bmp_row_buffer = bmalloc(C64_PAL_WIDTH * 4 + 4);                // Row + padding for BMP
    context->bgr_frame_buffer = bmalloc(C64_PAL_WIDTH * C64_PAL_HEIGHT * 3); // BGR24 frame (max size)
    if (!context->bmp_row_buffer || !context->bgr_frame_buffer) {
        C64_LOG_ERROR("Failed to allocate recording buffers");
        if (context->frame_buffer)
            bfree(context->frame_buffer);
        if (context->bmp_row_buffer)
            bfree(context->bmp_row_buffer);
        if (context->bgr_frame_buffer)
            bfree(context->bgr_frame_buffer);
        bfree(context);
        return NULL;
    }
    context->last_frame_time = 0; // Initialize frame timeout detection

    // Initialize video format detection
    context->detected_frame_height = 0;
    context->format_detected = false;
    context->expected_fps = prefer_pal ? 50.125 : 59.826; // Default to OBS FPS; updated on detection

    // Initialize mutexes (frame_mutex no longer needed for async video output)
    if (pthread_mutex_init(&context->assembly_mutex, NULL) != 0) {
        C64_LOG_ERROR("Failed to initialize assembly mutex");
        bfree(context->frame_buffer);
        bfree(context->bmp_row_buffer);
        bfree(context->bgr_frame_buffer);
        bfree(context);
        return NULL;
    }

    // Initialize config mutex for thread-safe access to hostname/dns_server_ip/ip_address
    if (pthread_mutex_init(&context->config_mutex, NULL) != 0) {
        C64_LOG_ERROR("Failed to initialize config mutex");
        pthread_mutex_destroy(&context->assembly_mutex);
        bfree(context->frame_buffer);
        bfree(context->bmp_row_buffer);
        bfree(context->bgr_frame_buffer);
        bfree(context);
        return NULL;
    }

    // Initialize shared synthetic start time mutex (A/V timestamps share a single origin)
    if (pthread_mutex_init(&context->stream_start_mutex, NULL) != 0) {
        C64_LOG_ERROR("Failed to initialize stream start mutex");
        pthread_mutex_destroy(&context->config_mutex);
        pthread_mutex_destroy(&context->assembly_mutex);
        bfree(context->frame_buffer);
        bfree(context->bmp_row_buffer);
        bfree(context->bgr_frame_buffer);
        bfree(context);
        return NULL;
    }

    // Initialize buffer delay from settings - optimized for low latency
    context->buffer_delay_ms = (uint32_t)obs_data_get_int(settings, "buffer_delay_ms");
    if (context->buffer_delay_ms == 0) {
        context->buffer_delay_ms = 10;
    }

    // Initialize network buffer for packet jitter correction
    context->network_buffer = c64_network_buffer_create();
    if (!context->network_buffer) {
        C64_LOG_ERROR("Failed to create network buffer");
        pthread_mutex_destroy(&context->stream_start_mutex);
        pthread_mutex_destroy(&context->config_mutex);
        pthread_mutex_destroy(&context->assembly_mutex);
        if (context->frame_buffer)
            bfree(context->frame_buffer);
        if (context->bmp_row_buffer)
            bfree(context->bmp_row_buffer);
        if (context->bgr_frame_buffer)
            bfree(context->bgr_frame_buffer);
        bfree(context);
        return NULL;
    }

    // Set initial buffer delay for both video and audio
    c64_network_buffer_set_delay(context->network_buffer, context->buffer_delay_ms, context->buffer_delay_ms);

    C64_LOG_INFO("Network buffer initialized: %u ms delay", context->buffer_delay_ms);

    // Initialize sockets to invalid
    context->video_socket = INVALID_SOCKET_VALUE;
    context->audio_socket = INVALID_SOCKET_VALUE;
    context->control_socket = INVALID_SOCKET_VALUE;
    os_atomic_set_bool(&context->thread_active, false);
    os_atomic_set_bool(&context->video_thread_active, false);
    os_atomic_set_bool(&context->video_processor_thread_active, false);
    os_atomic_set_bool(&context->audio_thread_active, false);
    context->auto_start_attempted = false;

    // Initialize audio info for low-latency audio processing hints to OBS
    // Audio sample rate will be set by format detection
    // Default to PAL until format is detected
    context->audio_sample_rate = C64_PAL_AUDIO_SAMPLE_RATE;
    context->audio_info.samples_per_sec = (uint32_t)context->audio_sample_rate;
    context->audio_info.format = AUDIO_FORMAT_16BIT;
    context->audio_info.speakers = SPEAKERS_STEREO;

    // Log low-latency configuration
    C64_LOG_INFO("Low-latency mode: buffer_delay=%ums, audio_rate=%.1fHz (will adjust to format)",
                 context->buffer_delay_ms, context->audio_sample_rate);

    // Initialize statistics counters
    os_atomic_set_long(&context->video_packets_received, 0);
    os_atomic_set_long(&context->video_bytes_received, 0);
    os_atomic_set_long(&context->video_sequence_errors, 0);
    os_atomic_set_long(&context->video_frames_processed, 0);
    os_atomic_set_long(&context->audio_packets_received, 0);
    os_atomic_set_long(&context->audio_bytes_received, 0);

    // Initialize debug counters
    os_atomic_set_long(&context->debug_recvfrom_calls, 0);
    os_atomic_set_long(&context->debug_recvfrom_eagain, 0);
    os_atomic_set_long(&context->debug_recvfrom_bytes_total, 0);
    os_atomic_set_long(&context->debug_packets_dropped_size, 0);

    // Preallocate Stage-1 network FIFOs (Stage-1: socket recv, Stage-2: buffering/order)
    // Video is higher PPS; keep a larger backlog to absorb short processing stalls.
    {
        const uint32_t video_fifo_capacity = 8192;
        const uint32_t audio_fifo_capacity = 2048;

        context->video_fifo_entries = bmalloc(sizeof(struct c64_network_fifo_packet) * video_fifo_capacity);
        context->audio_fifo_entries = bmalloc(sizeof(struct c64_network_fifo_packet) * audio_fifo_capacity);

        if (!context->video_fifo_entries || !context->audio_fifo_entries) {
            C64_LOG_ERROR("Failed to allocate UDP network FIFOs");
            if (context->video_fifo_entries) {
                bfree(context->video_fifo_entries);
                context->video_fifo_entries = NULL;
            }
            if (context->audio_fifo_entries) {
                bfree(context->audio_fifo_entries);
                context->audio_fifo_entries = NULL;
            }
            c64_network_buffer_destroy(context->network_buffer);
            context->network_buffer = NULL;
            pthread_mutex_destroy(&context->stream_start_mutex);
            pthread_mutex_destroy(&context->assembly_mutex);
            pthread_mutex_destroy(&context->config_mutex);
            bfree(context->frame_buffer);
            bfree(context->bmp_row_buffer);
            bfree(context->bgr_frame_buffer);
            bfree(context);
            return NULL;
        }

        context->video_fifo.entries = context->video_fifo_entries;
        context->video_fifo.capacity = video_fifo_capacity;
        context->audio_fifo.entries = context->audio_fifo_entries;
        context->audio_fifo.capacity = audio_fifo_capacity;
        c64_network_fifo_reset(&context->video_fifo);
        c64_network_fifo_reset(&context->audio_fifo);
    }

    context->last_stats_log_time = os_gettime_ns();
    context->last_audio_stats_log_time = context->last_stats_log_time;
    context->last_stats_tick_ns = context->last_stats_log_time;

    // Initialize render callback timeout system
    uint64_t now = os_gettime_ns();
    context->last_udp_packet_time = now; // DEPRECATED - kept for compatibility
    context->last_video_packet_time = now;
    context->last_audio_packet_time = now;
    os_atomic_set_long(&context->retry_in_progress, 0);
    context->retry_count = 0;
    context->consecutive_failures = 0;
    os_atomic_set_long(&context->retry_thread_active, 0);

    // Initialize synthetic A/V timeline state
    context->stream_start_ns = 0;
    os_atomic_set_bool(&context->stream_start_set, false);
    context->last_video_ts_ns = 0;
    context->last_audio_ts_ns = 0;
    context->first_video_ts_ns = 0;
    context->first_audio_ts_ns = 0;
    context->first_video_ts_logged = false;
    context->first_audio_ts_logged = false;
    context->initial_av_delta_logged = false;
    context->last_video_ts_frame_num = 0;
    context->video_ts_frame_num_set = false;
    context->video_frame_index = 0;
    context->frame_interval_ns = prefer_pal ? C64_PAL_FRAME_INTERVAL_NS
                                            : C64_NTSC_FRAME_INTERVAL_NS; // Default to OBS FPS
    os_atomic_set_long(&context->script_render_count, 0);

    // Initialize debug logging from settings (must be done before any debug logs)
    c64_debug_logging = obs_data_get_bool(settings, "debug_logging");
    C64_LOG_INFO("Debug logging initialized: %s", c64_debug_logging ? "enabled" : "disabled");

    // Initialize logo system with pre-rendered frame
    if (!c64_logo_init(context)) {
        C64_LOG_WARNING("Logo system initialization failed - continuing without logo");
    }

    c64_apply_format_hint(context, prefer_pal);

    // Display logo immediately so user sees it instantly (before connection attempt)
    if (c64_logo_is_available(context)) {
        c64_logo_render_to_frame(context, os_gettime_ns());
        C64_LOG_DEBUG("Logo rendered immediately on source creation");
    }

    // Initialize recording for this source
    c64_record_init(context);

    // Apply initial recording settings from configuration
    c64_record_update_settings(context, settings);

    // Initialize CRT effect state from settings
    c64_afterglow_init(&context->afterglow);
    context->scan_line_distance =
        c64_obs_data_get_double_or_current(settings, "scan_line_distance", context->scan_line_distance);
    context->scan_line_strength =
        c64_obs_data_get_double_or_current(settings, "scan_line_strength", context->scan_line_strength);
    context->pixel_width = c64_obs_data_get_double_or_current(settings, "pixel_width", context->pixel_width);
    context->pixel_height = c64_obs_data_get_double_or_current(settings, "pixel_height", context->pixel_height);
    context->blur_strength = c64_obs_data_get_double_or_current(settings, "blur_strength", context->blur_strength);
    context->bloom_strength = c64_obs_data_get_double_or_current(settings, "bloom_strength", context->bloom_strength);
    context->bloom_enable = context->bloom_strength > 0.0f;
    context->afterglow.duration_ms =
        c64_obs_data_get_int_or_current(settings, "afterglow_duration_ms", context->afterglow.duration_ms);
    context->afterglow.curve = c64_obs_data_get_int_or_current(settings, "afterglow_curve", context->afterglow.curve);
    context->afterglow_enable = (context->afterglow.duration_ms > 0);
    context->tint_mode = c64_obs_data_get_int_or_current(settings, "tint_mode", context->tint_mode);
    context->tint_strength = c64_obs_data_get_double_or_current(settings, "tint_strength", context->tint_strength);
    context->tint_enable = (context->tint_mode > 0 && context->tint_strength > 0.0f);

    context->frame_dirty = false;

    // Initialize palette from settings (must be done after palette system init)
    // Always select Default if settings are empty (first startup guarantee)
    const char *palette_id = obs_data_get_string(settings, "palette");
    if (!palette_id || !palette_id[0]) {
        // No palette specified - select Default (first startup)
        c64_palette_select("Default");
    } else {
        // Palette specified - try to select it, fall back to Default if not found
        if (!c64_palette_select(palette_id)) {
            // Palette not found (was deleted) - fall back to Default
            c64_palette_select("Default");
        }
    }

    // Note: avoid noisy logging here; E2E expects deterministic behavior without requiring log parsing.
    context->render_texture = NULL;
    context->render_texture_width = 0;
    context->render_texture_height = 0;
    context->intermediate_texture = NULL;
    context->crt_effect = NULL;
    context->afterglow_accum_prev = NULL;
    context->afterglow_accum_next = NULL;
    context->last_frame_time_ns = 0;
    context->afterglow_dt_ms = 33.33f;
    context->afterglow_last_tick_ns = 0;
    context->afterglow.accum = NULL;
    context->afterglow.accum_bytes = 0;
    context->afterglow.accum_valid = false;
    context->afterglow.decay_cache_valid = false;

    // Pre-allocate afterglow CPU accumulator to avoid allocation in video hot path
    // Use PAL size (larger) to cover both PAL and NTSC; will be resized if needed.
    {
        const size_t initial_accum_bytes = C64_PAL_WIDTH * C64_PAL_HEIGHT * sizeof(uint32_t);
        // Use aligned allocation for optimal SIMD performance (declared in c64-video.h)
        context->afterglow.accum = (uint32_t *)c64_alloc_aligned(initial_accum_bytes, 64);
        if (context->afterglow.accum) {
            memset(context->afterglow.accum, 0, initial_accum_bytes);
            context->afterglow.accum_bytes = initial_accum_bytes;
        }
    }

    // Start initial connection asynchronously to avoid blocking OBS UI thread.
    C64_LOG_INFO("C64 Stream source created successfully - scheduling background initial connection");
    c64_schedule_retry(context, "initial connection");

    // Initialize REST control and keyboard capture
    context->rest_client = NULL;
    context->keyboard = NULL;
    context->keymap = NULL;
    context->keyboard_capture_active = false;
    memset(context->rest_base_url, 0, sizeof(context->rest_base_url));
    memset(context->keyboard_keymap_name, 0, sizeof(context->keyboard_keymap_name));

    // Build REST base URL from c64_host
    const char *c64_host = obs_data_get_string(settings, "c64_host");
    if (c64_host && c64_host[0] != '\0' && strcmp(c64_host, "0.0.0.0") != 0) {
        // Build REST API URL from host (add http:// if not present)
        if (strncmp(c64_host, "http://", 7) == 0 || strncmp(c64_host, "https://", 8) == 0) {
            snprintf(context->rest_base_url, sizeof(context->rest_base_url), "%s", c64_host);
        } else {
            snprintf(context->rest_base_url, sizeof(context->rest_base_url), "http://%s", c64_host);
        }

        context->rest_client = c64_rest_client_create(context->rest_base_url, context->c64_password);
    }

    if (context->rest_client) {
        c64_record_on_rest_client_ready(context);
    }

    // Load keyboard settings and create keyboard module
    const char *keymap_name = obs_data_get_string(settings, "keyboard_keymap");
    if (keymap_name && keymap_name[0] != '\0') {
        strncpy(context->keyboard_keymap_name, keymap_name, sizeof(context->keyboard_keymap_name) - 1);
        // Load keymap file using absolute path from module
        char keymap_filename[128];
        snprintf(keymap_filename, sizeof(keymap_filename), "keymaps/%s.c64keymap.ini", keymap_name);
        char *keymap_path = obs_module_file(keymap_filename);
        if (keymap_path) {
            context->keymap = c64_keymap_load(keymap_path);
            if (!context->keymap) {
                C64_LOG_WARNING("Failed to load keymap: %s", keymap_path);
            } else {
                C64_LOG_INFO("🕹 KEYBOARD: Loaded keymap: %s", keymap_name);
            }
            bfree(keymap_path);
        } else {
            C64_LOG_WARNING("Failed to resolve keymap path for: %s", keymap_name);
        }
    }

    // Create keyboard module if REST client is available
    if (context->rest_client) {
        context->keyboard = c64_keyboard_create(context->rest_client);
    }

    // Initialize script automation fields
    context->script_executor = NULL;
    memset(context->script_file_path, 0, sizeof(context->script_file_path));
    context->last_script_status = C64_SCRIPT_STATUS_IDLE;
    context->last_ui_update_time = 0;
    context->force_ui_update = false;
    context->script_autostarted = false;
    memset(context->cached_last_line, 0, sizeof(context->cached_last_line));
    memset(context->cached_next_line, 0, sizeof(context->cached_next_line));

    // Initialize content automation fields
    context->automation = NULL;
    memset(context->automation_status, 0, sizeof(context->automation_status));

    // Load script file path if set
    const char *script_path = obs_data_get_string(settings, "script_file");
    if (script_path && script_path[0] != '\0') {
        strncpy(context->script_file_path, script_path, sizeof(context->script_file_path) - 1);
    }

    c64_attempt_script_autostart(context, settings);

    return context;
}

void c64_destroy(void *data)
{
    struct c64_source *context = data;
    if (!context)
        return;

    C64_LOG_INFO("Destroying C64 Stream source");

    c64_av_sync_cleanup(context);

    // Stop any background retry thread.
    if (os_atomic_load_long(&context->retry_thread_active)) {
        int join_result = pthread_join(context->retry_thread, NULL);
        if (join_result != 0) {
            C64_LOG_WARNING("Failed to join retry thread during destroy (err=%d)", join_result);
        }
        os_atomic_set_long(&context->retry_thread_active, 0);
        os_atomic_set_long(&context->retry_in_progress, 0);
    }

    // Stop streaming if active. This sends release_all and explicit remote
    // stream stops before closing local sockets.
    if (context->streaming) {
        C64_LOG_DEBUG("Stopping active streaming during destruction");
        c64_stop_streaming(context);
    }

    c64_record_cleanup(context);

    // Cleanup script automation
    if (context->script_executor) {
        c64_script_executor_destroy(context->script_executor);
        context->script_executor = NULL;
    }

    // Cleanup content automation
    if (context->automation) {
        c64_automation_destroy((c64_automation_t *)context->automation);
        context->automation = NULL;
    }

    // Cleanup logo system
    c64_logo_cleanup(context);

    // Cleanup CRT effect resources (must be done in graphics context)
    obs_enter_graphics();
    if (context->render_texture) {
        gs_texture_destroy(context->render_texture);
        context->render_texture = NULL;
        context->render_texture_width = 0;
        context->render_texture_height = 0;
    }
    if (context->intermediate_texture) {
        gs_texture_destroy(context->intermediate_texture);
        context->intermediate_texture = NULL;
    }
    if (context->afterglow_accum_prev) {
        gs_texture_destroy(context->afterglow_accum_prev);
        context->afterglow_accum_prev = NULL;
    }
    if (context->afterglow_accum_next) {
        gs_texture_destroy(context->afterglow_accum_next);
        context->afterglow_accum_next = NULL;
    }
    if (context->crt_effect) {
        gs_effect_destroy(context->crt_effect);
        context->crt_effect = NULL;
    }
    if (context->point_sampler) {
        gs_samplerstate_destroy(context->point_sampler);
        context->point_sampler = NULL;
    }
    obs_leave_graphics();

    // Cleanup resources
    pthread_mutex_destroy(&context->stream_start_mutex);
    pthread_mutex_destroy(&context->assembly_mutex);
    pthread_mutex_destroy(&context->config_mutex);
    if (context->frame_buffer) {
        bfree(context->frame_buffer);
    }
    if (context->bmp_row_buffer) {
        bfree(context->bmp_row_buffer);
    }
    if (context->bgr_frame_buffer) {
        bfree(context->bgr_frame_buffer);
    }
    if (context->network_buffer) {
        c64_network_buffer_destroy(context->network_buffer);
        context->network_buffer = NULL;
    }

    if (context->video_fifo_entries) {
        bfree(context->video_fifo_entries);
        context->video_fifo_entries = NULL;
    }
    if (context->audio_fifo_entries) {
        bfree(context->audio_fifo_entries);
        context->audio_fifo_entries = NULL;
    }

    c64_afterglow_free(&context->afterglow);

    // Cleanup REST control and keyboard capture
    if (context->keyboard) {
        c64_keyboard_destroy(context->keyboard);
        context->keyboard = NULL;
    }
    if (context->keymap) {
        c64_keymap_destroy(context->keymap);
        context->keymap = NULL;
    }
    if (context->rest_client) {
        c64_rest_client_destroy(context->rest_client);
        context->rest_client = NULL;
    }

    bfree(context);
    C64_LOG_INFO("C64 Stream source destroyed");
}

static void c64_attempt_script_autostart(struct c64_source *context, obs_data_t *settings)
{
    if (!context || !settings) {
        return;
    }

    bool script_auto_start = obs_data_get_bool(settings, "script_auto_start");
    if (!script_auto_start) {
        context->script_autostarted = false;
        return;
    }

    if (context->script_autostarted || context->script_file_path[0] == '\0') {
        return;
    }

    c64_script_status_t status = C64_SCRIPT_STATUS_IDLE;
    if (context->script_executor) {
        status = c64_script_executor_get_status(context->script_executor);
    }

    if (status == C64_SCRIPT_STATUS_RUNNING || status == C64_SCRIPT_STATUS_PAUSED) {
        context->script_autostarted = true;
        return;
    }

    if (!context->script_executor) {
        context->script_executor = c64_script_executor_create(context->source, context);
    }

    if (context->script_executor) {
        C64_LOG_INFO("Auto-starting script: %s", context->script_file_path);
        context->script_start_time = os_gettime_ns();
        context->script_end_time = 0;
        context->last_script_status = C64_SCRIPT_STATUS_IDLE;
        if (c64_script_executor_start(context->script_executor, context->script_file_path)) {
            context->last_script_status = C64_SCRIPT_STATUS_RUNNING;
            context->force_ui_update = true;
        } else {
            const char *err = c64_script_executor_get_error(context->script_executor);
            C64_LOG_ERROR("Auto-start script failed: %s", err ? err : "unknown error");
            context->script_end_time = os_gettime_ns();
            context->script_ended_successfully = false;
            context->force_ui_update = true;
        }
    }

    context->script_autostarted = true;
}

typedef struct {
    obs_source_t *source;
} c64_source_properties_refresh_t;

static void c64_apply_properties_refresh(void *data)
{
    c64_source_properties_refresh_t *refresh = (c64_source_properties_refresh_t *)data;
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

    c64_source_properties_refresh_t *refresh = calloc(1, sizeof(c64_source_properties_refresh_t));
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

void c64_update(void *data, obs_data_t *settings)
{
    struct c64_source *context = data;
    c64_device_registry_migrate_legacy(settings);
    c64_device_registry_apply_selected(settings);
    if (!context)
        return;

    context->preserve_size = c64_effect_settings_resolve_preserve_size(settings, C64_SOURCE_SAVED_SETTING_KEYS,
                                                                       sizeof(C64_SOURCE_SAVED_SETTING_KEYS) /
                                                                           sizeof(C64_SOURCE_SAVED_SETTING_KEYS[0]));

    char old_script_path[512];
    strncpy(old_script_path, context->script_file_path, sizeof(old_script_path) - 1);
    old_script_path[sizeof(old_script_path) - 1] = '\0';

    // Script file path (used by Properties UI controls)
    const char *script_path = obs_data_get_string(settings, "script_file");
    if (script_path && script_path[0] != '\0') {
        strncpy(context->script_file_path, script_path, sizeof(context->script_file_path) - 1);
        context->script_file_path[sizeof(context->script_file_path) - 1] = '\0';
    } else {
        context->script_file_path[0] = '\0';
    }

    if (strcmp(old_script_path, context->script_file_path) != 0) {
        context->script_autostarted = false;
    }

    c64_attempt_script_autostart(context, settings);

    const char *preset_name = obs_data_get_string(settings, "crt_preset");
    c64_source_apply_crt_preset_if_needed(settings, preset_name, true);

    // Update debug logging setting
    bool previous_debug_logging = c64_debug_logging;
    c64_debug_logging = obs_data_get_bool(settings, "debug_logging");
    if (previous_debug_logging != c64_debug_logging) {
        C64_LOG_INFO("Debug logging %s", c64_debug_logging ? "enabled" : "disabled");
    }

    // Update IP detection setting - only auto-detect when checkbox state changes from off to on
    bool new_auto_detect = obs_data_get_bool(settings, "auto_detect_ip");
    if (new_auto_detect && !context->auto_detect_ip) {
        // Checkbox was just enabled - perform auto-detection
        if (c64_detect_local_ip_for_host(context->hostname, context->dns_server_ip, context->obs_ip_address,
                                         sizeof(context->obs_ip_address)) ||
            c64_detect_local_ip(context->obs_ip_address, sizeof(context->obs_ip_address))) {
            C64_LOG_INFO("" NETWORK_LOG_PREFIX " Auto-detected OBS IP address: %s", context->obs_ip_address);
            // Save the updated IP to settings
            obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
        } else {
            C64_LOG_WARNING("" NETWORK_LOG_PREFIX " Failed to auto-detect OBS IP address");
        }
    }
    context->auto_detect_ip = new_auto_detect;

    // Update configuration
    const char *new_host = obs_data_get_string(settings, "c64_host");
    const char *new_password = obs_data_get_string(settings, "c64_password");
    const char *new_obs_ip = obs_data_get_string(settings, "obs_ip_address");
    uint32_t new_video_port = (uint32_t)obs_data_get_int(settings, "video_port");
    uint32_t new_audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");
    uint32_t new_control_port = (uint32_t)obs_data_get_int(settings, "control_port");
    context->stream_control_transport = (int)obs_data_get_int(settings, "stream_control_transport");

    // Set defaults
    if (!new_host)
        new_host = C64_DEFAULT_HOST;
    if (new_video_port == 0)
        new_video_port = C64_DEFAULT_VIDEO_PORT;
    if (new_audio_port == 0)
        new_audio_port = C64_DEFAULT_AUDIO_PORT;
    if (new_control_port == 0)
        new_control_port = C64_CONTROL_PORT;

    // Snapshot existing network-related settings so we can avoid unnecessary retries/restarts.
    char old_hostname[64];
    char old_dns_server_ip[64];
    pthread_mutex_lock(&context->config_mutex);
    strncpy(old_hostname, context->hostname, sizeof(old_hostname) - 1);
    old_hostname[sizeof(old_hostname) - 1] = '\0';
    strncpy(old_dns_server_ip, context->dns_server_ip, sizeof(old_dns_server_ip) - 1);
    old_dns_server_ip[sizeof(old_dns_server_ip) - 1] = '\0';
    pthread_mutex_unlock(&context->config_mutex);

    char old_obs_ip[64];
    char old_password[sizeof(context->c64_password)];
    strncpy(old_obs_ip, context->obs_ip_address, sizeof(old_obs_ip) - 1);
    old_obs_ip[sizeof(old_obs_ip) - 1] = '\0';
    snprintf(old_password, sizeof(old_password), "%s", context->c64_password);

    const bool host_changed = (strcmp(old_hostname, new_host) != 0);
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");
    const char *new_dns_server_ip = (dns_server_ip && dns_server_ip[0] != '\0') ? dns_server_ip : "";
    const bool dns_changed = (strcmp(old_dns_server_ip, new_dns_server_ip) != 0);
    const char *new_obs_ip_str = new_obs_ip ? new_obs_ip : "";
    const bool obs_ip_changed = (strcmp(old_obs_ip, new_obs_ip_str) != 0);

    // Check if ports have changed (requires socket recreation)
    bool ports_changed = (new_video_port != context->video_port) || (new_audio_port != context->audio_port) ||
                         (new_control_port != context->control_port);

    if ((ports_changed || host_changed) && context->streaming) {
        if (ports_changed) {
            C64_LOG_INFO(
                "Port configuration changed (video: %u->%u, audio: %u->%u, control: %u->%u), recreating sockets",
                context->video_port, new_video_port, context->audio_port, new_audio_port, context->control_port,
                new_control_port);
        }
        if (host_changed) {
            C64_LOG_INFO("C64 host changed (%s->%s), restarting streaming", old_hostname, new_host);
        }

        // Stop streaming and close existing sockets
        c64_stop_streaming(context);

        // Give the C64 Ultimate device time to process stop commands
        os_sleep_ms(100);
    }

    // Update configuration - hostname and IP resolution (thread-safe)
    pthread_mutex_lock(&context->config_mutex);
    strncpy(context->hostname, new_host, sizeof(context->hostname) - 1);
    context->hostname[sizeof(context->hostname) - 1] = '\0';

    if (new_password && new_password[0] != '\0') {
        strncpy(context->c64_password, new_password, sizeof(context->c64_password) - 1);
        context->c64_password[sizeof(context->c64_password) - 1] = '\0';
    } else {
        context->c64_password[0] = '\0';
    }

    // Update DNS server IP (resolution happens in background).
    if (new_dns_server_ip[0] != '\0') {
        strncpy(context->dns_server_ip, new_dns_server_ip, sizeof(context->dns_server_ip) - 1);
        context->dns_server_ip[sizeof(context->dns_server_ip) - 1] = '\0';
    } else {
        context->dns_server_ip[0] = '\0';
    }

    // IMPORTANT: do not do DNS resolution in c64_update (OBS UI thread).
    // Store hostname as-is; resolution will happen in the background before connecting.
    if (host_changed) {
        strncpy(context->ip_address, new_host, sizeof(context->ip_address) - 1);
        context->ip_address[sizeof(context->ip_address) - 1] = '\0';
    }
    c64_set_expected_peer_ip(context, context->ip_address);
    pthread_mutex_unlock(&context->config_mutex);

    if (new_obs_ip) {
        strncpy(context->obs_ip_address, new_obs_ip, sizeof(context->obs_ip_address) - 1);
        context->obs_ip_address[sizeof(context->obs_ip_address) - 1] = '\0';
    }
    context->video_port = new_video_port;
    context->audio_port = new_audio_port;
    context->control_port = new_control_port;

    if (host_changed || strcmp(old_password, context->c64_password) != 0) {
        context->stream_rest_demoted_until_ns = 0;
        c64_rebuild_rest_client(context);
    }

    // Update buffer delay setting with debouncing to prevent timestamp reset storms
    uint32_t new_buffer_delay_ms = (uint32_t)obs_data_get_int(settings, "buffer_delay_ms");
    if (new_buffer_delay_ms != context->buffer_delay_ms) {
        uint32_t old_buffer_delay_ms = context->buffer_delay_ms;
        C64_LOG_INFO("Buffer delay changed from %u to %u ms", old_buffer_delay_ms, new_buffer_delay_ms);

        context->buffer_delay_ms = new_buffer_delay_ms;

        // Update network buffer delay (this adjusts UDP packet buffering only)
        if (context->network_buffer) {
            c64_network_buffer_set_delay(context->network_buffer, new_buffer_delay_ms, new_buffer_delay_ms);

            // Force render texture refresh to prevent display freeze after buffer changes
            // Buffer delay changes can cause frame buffer desynchronization
            if (context->render_texture) {
                obs_enter_graphics();
                gs_texture_destroy(context->render_texture);
                context->render_texture = NULL;
                obs_leave_graphics();
                C64_LOG_DEBUG("🔄 Render texture invalidated due to buffer delay change");
            }
        }
    }

    // Update recording settings
    c64_record_update_settings(context, settings);

    // Update palette selection if changed
    const char *palette_id = obs_data_get_string(settings, "palette");
    if (palette_id && palette_id[0]) {
        const char *current_palette = c64_palette_get_active_id();
        if (!current_palette || strcmp(current_palette, palette_id) != 0) {
            c64_palette_select(palette_id);
        }
    }

    // Check if dimension-affecting effects were previously disabled
    bool prev_dimension_effects = (context->scan_line_distance > 0.0f) || (context->pixel_width != 1.0f) ||
                                  (context->pixel_height != 1.0f);

    // Update CRT effect settings
    context->scan_line_distance =
        c64_obs_data_get_double_or_current(settings, "scan_line_distance", context->scan_line_distance);
    context->scan_line_strength =
        c64_obs_data_get_double_or_current(settings, "scan_line_strength", context->scan_line_strength);
    context->pixel_width = c64_obs_data_get_double_or_current(settings, "pixel_width", context->pixel_width);
    context->pixel_height = c64_obs_data_get_double_or_current(settings, "pixel_height", context->pixel_height);
    context->blur_strength = c64_obs_data_get_double_or_current(settings, "blur_strength", context->blur_strength);
    context->bloom_strength = c64_obs_data_get_double_or_current(settings, "bloom_strength", context->bloom_strength);
    context->bloom_enable = context->bloom_strength > 0.0f;
    context->afterglow.duration_ms =
        c64_obs_data_get_int_or_current(settings, "afterglow_duration_ms", context->afterglow.duration_ms);
    context->afterglow.curve = c64_obs_data_get_int_or_current(settings, "afterglow_curve", context->afterglow.curve);
    context->afterglow_enable = (context->afterglow.duration_ms > 0);
    context->tint_mode = c64_obs_data_get_int_or_current(settings, "tint_mode", context->tint_mode);
    context->tint_strength = c64_obs_data_get_double_or_current(settings, "tint_strength", context->tint_strength);
    context->tint_enable = (context->tint_mode > 0 && context->tint_strength > 0.0f);

    // Check if dimension-affecting effects are now enabled
    bool new_dimension_effects = (context->scan_line_distance > 0.0f) || (context->pixel_width != 1.0f) ||
                                 (context->pixel_height != 1.0f);

    // Reset timing base if dimension-affecting effects were just enabled during streaming
    (void)prev_dimension_effects;
    (void)new_dimension_effects;

    // Reload keymap if the selection changed
    const char *new_keymap_name = obs_data_get_string(settings, "keyboard_keymap");
    if (new_keymap_name && new_keymap_name[0] != '\0' && strcmp(context->keyboard_keymap_name, new_keymap_name) != 0) {
        // Record the attempted name now to prevent repeated retries on persistent failure.
        snprintf(context->keyboard_keymap_name, sizeof(context->keyboard_keymap_name), "%s", new_keymap_name);
        char keymap_filename[128];
        snprintf(keymap_filename, sizeof(keymap_filename), "keymaps/%s.c64keymap.ini", new_keymap_name);
        char *keymap_path = obs_module_file(keymap_filename);
        if (keymap_path) {
            c64_keymap_t *new_keymap = c64_keymap_load(keymap_path);
            bfree(keymap_path);
            if (new_keymap) {
                c64_keymap_t *old_keymap = context->keymap;
                context->keymap = new_keymap;
                C64_LOG_INFO("🕹 KEYBOARD: Reloaded keymap: %s", new_keymap_name);
                if (old_keymap) {
                    c64_keymap_destroy(old_keymap);
                }
            } else {
                C64_LOG_WARNING("Failed to load keymap on update: %s", new_keymap_name);
            }
        } else {
            C64_LOG_WARNING("Failed to resolve keymap path for: %s", new_keymap_name);
        }
    }

    // Only schedule a background retry when network-related settings changed or streaming is stopped.
    const bool should_schedule_retry = (!context->streaming) || ports_changed || host_changed || obs_ip_changed ||
                                       dns_changed;
    if (should_schedule_retry) {
        const char *reason = !context->streaming ? "update (not streaming)"
                             : host_changed      ? "update (host changed)"
                             : ports_changed     ? "update (ports changed)"
                             : obs_ip_changed    ? "update (OBS IP changed)"
                             : dns_changed       ? "update (DNS changed)"
                                                 : "update";
        C64_LOG_INFO("" NETWORK_LOG_PREFIX " Applying configuration and scheduling streaming start (%s)", reason);
        c64_schedule_retry(context, reason);
    } else {
        C64_LOG_DEBUG("" NETWORK_LOG_PREFIX " Configuration update does not require streaming restart");
    }
}

static void c64_set_expected_peer_ip(struct c64_source *context, const char *ip_string)
{
    if (!context || !ip_string) {
        return;
    }

    struct in_addr addr;
    if (inet_pton(AF_INET, ip_string, &addr) == 1) {
        context->expected_peer_ip = addr.s_addr;
        context->expected_peer_ip_set = true;
        C64_LOG_DEBUG("" NETWORK_LOG_PREFIX " Expected peer IP set to %s", ip_string);
    } else {
        context->expected_peer_ip_set = false;
        C64_LOG_DEBUG("" NETWORK_LOG_PREFIX " Expected peer IP cleared (non-IPv4 input): %s", ip_string);
    }
}

void c64_start_streaming(struct c64_source *context)
{
    if (!context) {
        C64_LOG_WARNING("Cannot start streaming - invalid context");
        return;
    }

    C64_LOG_INFO("Starting C64 Stream streaming to C64 %s (OBS IP: %s, video:%u, audio:%u)...", context->ip_address,
                 context->obs_ip_address, context->video_port, context->audio_port);

    // Proactively disconnect all streams before starting to ensure clean state
    // This prevents stale streaming state on the C64U from previous sessions
    if (strcmp(context->ip_address, "0.0.0.0") != 0) {
        C64_LOG_DEBUG("Sending proactive disconnect for all streams before starting");
        c64_stream_control(context, false, 0, NULL); // Stop video
        c64_stream_control(context, false, 1, NULL); // Stop audio
        // Brief delay to ensure stop commands are processed before start commands
        os_sleep_ms(50);
    }

    // Ensure expected peer IP matches current ip_address before binding sockets
    c64_set_expected_peer_ip(context, context->ip_address);

    // Stop existing threads BEFORE closing sockets (prevents race conditions on Windows)
    if (context->streaming) {
        context->streaming = false;
        os_atomic_set_bool(&context->thread_active, false);

        // Wait for existing threads to finish BEFORE closing their sockets
        if (os_atomic_load_bool(&context->video_thread_active) && pthread_join(context->video_thread, NULL) != 0) {
            C64_LOG_WARNING("Failed to join existing video thread during reconnection");
        }
        if (os_atomic_load_bool(&context->audio_thread_active) && pthread_join(context->audio_thread, NULL) != 0) {
            C64_LOG_WARNING("Failed to join existing audio thread during reconnection");
        }
        os_atomic_set_bool(&context->video_thread_active, false);
        os_atomic_set_bool(&context->audio_thread_active, false);
    }

    // Now safe to close existing sockets after threads have stopped
    close_and_reset_sockets(context);

    // Create fresh UDP sockets (required for reconnection after C64 restart)
    context->video_socket = c64_create_udp_socket(context->video_port);
    context->audio_socket = c64_create_udp_socket(context->audio_port);

    if (context->video_socket == INVALID_SOCKET_VALUE || context->audio_socket == INVALID_SOCKET_VALUE) {
        C64_LOG_ERROR("Failed to create UDP sockets for streaming");
        close_and_reset_sockets(context);
        return;
    }

#ifdef _WIN32
    // Windows: Additional delay to ensure sockets are fully bound and ready
    // before sending start commands to C64 (prevents race condition)
    os_sleep_ms(100);
#endif

    // Reset synthetic timing state for clean reconnection
    if (pthread_mutex_lock(&context->stream_start_mutex) == 0) {
        context->stream_start_ns = 0;
        os_atomic_set_bool(&context->stream_start_set, false);

        context->audio_packet_count = 0;
        context->last_audio_ts_seq = 0;
        context->audio_ts_seq_set = false;
        context->audio_packet_index = 0;
        context->audio_interval_ns = 0;
        context->last_audio_timestamp_validation = 0;

        context->last_video_ts_frame_num = 0;
        context->video_ts_frame_num_set = false;
        context->video_frame_index = 0;

        context->last_video_ts_ns = 0;
        context->last_audio_ts_ns = 0;

        context->first_video_ts_ns = 0;
        context->first_audio_ts_ns = 0;
        context->first_video_ts_logged = false;
        context->first_audio_ts_logged = false;
        context->initial_av_delta_logged = false;

        pthread_mutex_unlock(&context->stream_start_mutex);
    }
    C64_LOG_DEBUG("Synthetic A/V timing state reset for reconnection");

    // Send start commands to C64 Ultimate
    context->last_start_command_time_ns = os_gettime_ns();
    char video_dest[C64_STREAM_DEST_MAX];
    char audio_dest[C64_STREAM_DEST_MAX];
    if (!c64_build_stream_dest(video_dest, sizeof(video_dest), context->obs_ip_address, context->video_port) ||
        !c64_build_stream_dest(audio_dest, sizeof(audio_dest), context->obs_ip_address, context->audio_port)) {
        C64_LOG_ERROR("" NETWORK_LOG_PREFIX " Failed to build stream destination for start command");
        close_and_reset_sockets(context);
        return;
    }
    if (!c64_stream_control(context, true, 0, video_dest) || !c64_stream_control(context, true, 1, audio_dest)) {
        C64_LOG_ERROR("" NETWORK_LOG_PREFIX " Failed to start C64 stream control");
        if (context->keyboard) {
            c64_keyboard_release_all(context->keyboard);
        }
        c64_stream_control(context, false, 0, NULL);
        c64_stream_control(context, false, 1, NULL);
        close_and_reset_sockets(context);
        return;
    }

    // Start fresh worker threads
    os_atomic_set_bool(&context->thread_active, true);
    context->streaming = true;

    // Reset Stage-1 UDP network FIFOs for a clean start/reconnect.
    c64_network_fifo_reset(&context->video_fifo);
    c64_network_fifo_reset(&context->audio_fifo);

    if (pthread_create(&context->video_thread, NULL, c64_video_thread_func, context) != 0) {
        C64_LOG_ERROR("Failed to create video receiver thread");
        context->streaming = false;
        os_atomic_set_bool(&context->thread_active, false);
        close_and_reset_sockets(context);
        return;
    }
    os_atomic_set_bool(&context->video_thread_active, true);

    // Start video processor thread (processes packets from network buffer)
    if (pthread_create(&context->video_processor_thread, NULL, c64_video_processor_thread_func, context) != 0) {
        C64_LOG_ERROR("Failed to create video processor thread");
        context->streaming = false;
        os_atomic_set_bool(&context->thread_active, false);
        if (os_atomic_load_bool(&context->video_thread_active)) {
            pthread_join(context->video_thread, NULL);
            os_atomic_set_bool(&context->video_thread_active, false);
        }
        close_and_reset_sockets(context);
        return;
    }
    os_atomic_set_bool(&context->video_processor_thread_active, true);

    if (pthread_create(&context->audio_thread, NULL, audio_thread_func, context) != 0) {
        C64_LOG_ERROR("Failed to create audio receiver thread");
        context->streaming = false;
        os_atomic_set_bool(&context->thread_active, false);
        if (os_atomic_load_bool(&context->video_thread_active)) {
            pthread_join(context->video_thread, NULL);
            os_atomic_set_bool(&context->video_thread_active, false);
        }
        if (os_atomic_load_bool(&context->video_processor_thread_active)) {
            pthread_join(context->video_processor_thread, NULL);
            os_atomic_set_bool(&context->video_processor_thread_active, false);
        }
        close_and_reset_sockets(context);
        return;
    }
    os_atomic_set_bool(&context->audio_thread_active, true);

    C64_LOG_INFO("C64 Stream streaming started successfully");
}

void c64_stop_streaming(struct c64_source *context)
{
    if (!context || !context->streaming) {
        C64_LOG_WARNING("Cannot stop streaming - invalid context or not streaming");
        return;
    }

    C64_LOG_INFO("Stopping C64 Stream streaming...");

    /* Remote teardown precedes socket teardown so the old device is never left
     * streaming after a switch. release_all is deliberately attempted even if
     * stream control fails. */
    if (context->keyboard) {
        c64_keyboard_release_all(context->keyboard);
    } else if (context->rest_client) {
        c64_rest_release_all(context->rest_client);
    }
    if (!c64_stream_control(context, false, 0, NULL) || !c64_stream_control(context, false, 1, NULL)) {
        C64_LOG_WARNING("Remote stream stop failed for %s", context->ip_address);
    }

    context->streaming = false;
    os_atomic_set_bool(&context->thread_active, false);

    close_and_reset_sockets(context);
    if (os_atomic_load_bool(&context->video_thread_active) && pthread_join(context->video_thread, NULL) != 0) {
        C64_LOG_WARNING("Failed to join video thread");
    }
    os_atomic_set_bool(&context->video_thread_active, false);

    if (os_atomic_load_bool(&context->video_processor_thread_active) &&
        pthread_join(context->video_processor_thread, NULL) != 0) {
        C64_LOG_WARNING("Failed to join video processor thread");
    }
    os_atomic_set_bool(&context->video_processor_thread_active, false);

    if (os_atomic_load_bool(&context->audio_thread_active) && pthread_join(context->audio_thread, NULL) != 0) {
        C64_LOG_WARNING("Failed to join audio thread");
    }
    os_atomic_set_bool(&context->audio_thread_active, false);

    // Clear any queued UDP packets so the next start begins from an empty ingest state.
    c64_network_fifo_reset(&context->video_fifo);
    c64_network_fifo_reset(&context->audio_fifo);

    // Clear frame buffer (async video will stop automatically)
    if (context->frame_buffer) {
        uint32_t frame_size = context->width * context->height * 4;
        memset(context->frame_buffer, 0, frame_size);
    }

    // Reset frame assembly state
    if (pthread_mutex_lock(&context->assembly_mutex) == 0) {
        memset(&context->current_frame, 0, sizeof(context->current_frame));
        context->last_completed_frame = 0;
        context->frame_drops = 0;
        context->packet_drops = 0;
        context->frames_expected = 0;
        context->frames_captured = 0;
        context->frames_delivered_to_obs = 0;
        context->frames_completed = 0;
        pthread_mutex_unlock(&context->assembly_mutex);
    }

    C64_LOG_INFO("C64 Stream streaming stopped");
}

// Video tick callback - updates texture from async frame buffer when CRT effects are enabled
void c64_video_tick(void *data, float seconds)
{
    struct c64_source *context = data;
    if (!context)
        return;

    // Monitor script executor status for completion/errors
    if (context->script_executor) {
        c64_script_status_t current_status = c64_script_executor_get_status(context->script_executor);
        c64_script_status_t last_status = (c64_script_status_t)context->last_script_status;

        // Detect status transitions
        if (current_status != last_status) {
            if (current_status == C64_SCRIPT_STATUS_COMPLETED) {
                // Script completed successfully
                context->script_end_time = os_gettime_ns();
                context->script_ended_successfully = true;
                C64_LOG_INFO("Script completed successfully");
                context->force_ui_update = true; // Force immediate UI update
                c64_queue_properties_refresh(context);
            } else if (current_status == C64_SCRIPT_STATUS_ERROR) {
                // Script ended with error
                context->script_end_time = os_gettime_ns();
                context->script_ended_successfully = false;
                const char *error = c64_script_executor_get_error(context->script_executor);
                C64_LOG_ERROR("Script failed: %s", error ? error : "unknown error");
                context->force_ui_update = true; // Force immediate UI update
                c64_queue_properties_refresh(context);
            } else if (current_status == C64_SCRIPT_STATUS_IDLE && last_status == C64_SCRIPT_STATUS_RUNNING) {
                // Script was stopped
                context->script_end_time = os_gettime_ns();
                context->script_ended_successfully = false;
                context->force_ui_update = true; // Force immediate UI update
                c64_queue_properties_refresh(context);
            } else if (current_status == C64_SCRIPT_STATUS_PAUSED || current_status == C64_SCRIPT_STATUS_RUNNING) {
                // Entering or leaving pause/debug mode - force immediate update
                context->force_ui_update = true;
                c64_queue_properties_refresh(context);
            }
            context->last_script_status = (int)current_status;
        }
    }

    const bool effects_enabled =
        (context->scan_line_distance > 0.0f) || (context->bloom_strength > 0.0f) ||
        (context->afterglow.duration_ms > 0) || (context->tint_mode > 0 && context->tint_strength > 0.0f) ||
        (context->pixel_width != 1.0f || context->pixel_height != 1.0f) || (context->blur_strength > 0.0f);

    // Stable per-tick dt for afterglow.
    // Do NOT derive dt from `video_render` timestamps: render calls can be irregular (minimize-to-tray/headless),
    // causing huge dt spikes and making afterglow decay instantly to black.
    const uint64_t now_ns = os_gettime_ns();
    if (context->afterglow_last_tick_ns != 0 && now_ns > context->afterglow_last_tick_ns) {
        context->afterglow_dt_ms = (float)(now_ns - context->afterglow_last_tick_ns) / 1000000.0f;
    } else {
        const float fallback = (seconds > 0.0001f) ? (seconds * 1000.0f) : 33.33f;
        context->afterglow_dt_ms = fallback;
    }
    context->afterglow_last_tick_ns = now_ns;

    // Always update texture from frame buffer for consistent rendering.
    // Important: do NOT call gs_texture_get_width/height outside graphics context; cache dimensions instead.
    if (!context->render_texture || context->render_texture_width != context->width ||
        context->render_texture_height != context->height) {
        obs_enter_graphics();
        if (context->render_texture) {
            gs_texture_destroy(context->render_texture);
        }
        // Must be dynamic: we update it every tick via gs_texture_set_image (which maps internally).
        context->render_texture = gs_texture_create(context->width, context->height, GS_RGBA, 1, NULL, GS_DYNAMIC);
        context->render_texture_width = context->width;
        context->render_texture_height = context->height;

        // Upload initial pixels immediately to avoid a black/undefined frame and to keep update path consistent.
        if (context->render_texture && context->frame_buffer) {
            gs_texture_set_image(context->render_texture, (const uint8_t *)context->frame_buffer, context->width * 4,
                                 false);
        }

        // Create afterglow accumulation textures only when effects are enabled
        if (effects_enabled) {
            struct c64_effect_geometry geometry;
            c64_source_get_effect_geometry(context, &geometry);

            if (context->afterglow_accum_prev) {
                gs_texture_destroy(context->afterglow_accum_prev);
            }
            if (context->afterglow_accum_next) {
                gs_texture_destroy(context->afterglow_accum_next);
            }
            context->afterglow_accum_prev =
                gs_texture_create(geometry.virtual_width, geometry.virtual_height, GS_RGBA, 1, NULL, GS_RENDER_TARGET);
            context->afterglow_accum_next =
                gs_texture_create(geometry.virtual_width, geometry.virtual_height, GS_RGBA, 1, NULL, GS_RENDER_TARGET);
        } else {
            if (context->afterglow_accum_prev) {
                gs_texture_destroy(context->afterglow_accum_prev);
                context->afterglow_accum_prev = NULL;
            }
            if (context->afterglow_accum_next) {
                gs_texture_destroy(context->afterglow_accum_next);
                context->afterglow_accum_next = NULL;
            }
        }

        obs_leave_graphics();
        if (!context->render_texture) {
            C64_LOG_ERROR("Failed to create render texture");
            context->render_texture_width = 0;
            context->render_texture_height = 0;
        }
        if (effects_enabled && (!context->afterglow_accum_prev || !context->afterglow_accum_next)) {
            C64_LOG_ERROR("" EFFECT_LOG_PREFIX " Failed to create afterglow accumulation textures");
        }

        // Invalidate CPU afterglow accumulator on texture recreation (Medium #8: prevent visual glitch)
        context->afterglow.accum_valid = false;
        context->frame_dirty = false;

    } else {
        // Upload latest frame to the render texture.
        // Note: afterglow is applied at frame delivery time (video thread) into `afterglow.accum`.
        // We must NOT write afterglow back into `frame_buffer` here; that creates feedback and flicker when
        // packets drop or when video thread is concurrently writing the raw buffer.
        if (context->frame_buffer && context->width > 0 && context->height > 0) {
            const uint32_t *src_pixels = context->frame_buffer;
            if (context->afterglow_enable && context->afterglow.accum && context->afterglow.accum_valid &&
                context->afterglow.duration_ms > 0) {
                src_pixels = context->afterglow.accum;
            }
            obs_enter_graphics();
            gs_texture_set_image(context->render_texture, (const uint8_t *)src_pixels, context->width * 4, false);
            obs_leave_graphics();
        }
    }
}

// Helper to check if we're outputting to stream/recording
static bool is_output_active(void)
{
#ifdef ENABLE_FRONTEND_API
    // Check if OBS is streaming or recording
    obs_output_t *streaming = obs_frontend_get_streaming_output();
    obs_output_t *recording = obs_frontend_get_recording_output();

    bool active = false;
    if (streaming) {
        active = active || obs_output_active(streaming);
        obs_output_release(streaming);
    }
    if (recording) {
        active = active || obs_output_active(recording);
        obs_output_release(recording);
    }

    return active;
#else
    // Frontend API disabled - always show indicators in preview
    return false;
#endif
}

// Video render callback for CRT effects (GPU rendering)
void c64_video_render(void *data, gs_effect_t *effect)
{
    UNUSED_PARAMETER(effect);
    struct c64_source *context = data;
    if (!context)
        return;

    // If no render texture available, fall back to default rendering
    if (!context->render_texture) {
        return;
    }

    // Input texture for rendering. Afterglow persistence is baked into `render_texture` via CPU accumulation in
    // `video_tick`, so we always render from `render_texture` here.
    gs_texture_t *input_tex = context->render_texture;

    // Use stable per-tick dt computed in `video_tick`.
    // (Render calls are not guaranteed to happen at a steady cadence.)
    float dt_ms = context->afterglow_dt_ms;
    if (dt_ms <= 0.001f) {
        dt_ms = 33.33f;
    }

    // Check if any CRT effects are enabled
    bool any_effects_enabled =
        (context->scan_line_distance > 0.0f) || (context->bloom_strength > 0.0f) ||
        (context->afterglow.duration_ms > 0) || (context->tint_mode > 0 && context->tint_strength > 0.0f) ||
        (context->pixel_width != 1.0f || context->pixel_height != 1.0f) || context->blur_strength > 0.0f;

    // If no effects are enabled, use simple default rendering
    if (!any_effects_enabled) {
        gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        if (default_effect) {
            gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), input_tex);
            while (gs_effect_loop(default_effect, "Draw")) {
                gs_draw_sprite(input_tex, 0, context->width, context->height);
            }
            os_atomic_inc_long(&context->script_render_count);
        }
        return;
    }

    // (Afterglow-only is handled by the "no effects" / default render path since afterglow is baked into the frame.)

    // Load CRT shader effect if not already loaded (only when effects are enabled)
    if (!context->crt_effect) {
        char *effect_path = obs_module_file("effects/crt_effect.effect");
        if (effect_path) {
            context->crt_effect = gs_effect_create_from_file(effect_path, NULL);
            bfree(effect_path);
            if (!context->crt_effect) {
                C64_LOG_ERROR("" EFFECT_LOG_PREFIX
                              " Failed to load CRT effect shader - falling back to default rendering");
            } else {
                // Do not reset synthetic timing during shader compilation; timestamps are derived from a shared origin.
            }
            if (!context->crt_effect) {
                // Fall back to default rendering
                gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
                if (default_effect) {
                    gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"),
                                          context->render_texture);
                    while (gs_effect_loop(default_effect, "Draw")) {
                        gs_draw_sprite(context->render_texture, 0, context->width, context->height);
                    }
                    os_atomic_inc_long(&context->script_render_count);
                }
                return;
            }
        } else {
            C64_LOG_ERROR("" EFFECT_LOG_PREFIX
                          " Failed to find CRT effect shader file - falling back to default rendering");
            gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
            if (default_effect) {
                gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), context->render_texture);
                while (gs_effect_loop(default_effect, "Draw")) {
                    gs_draw_sprite(context->render_texture, 0, context->width, context->height);
                }
                os_atomic_inc_long(&context->script_render_count);
            }
            return;
        }
    }

    // Create point sampler for sharp pixel rendering (nearest-neighbor filtering)
    // This is needed when blur_strength is 0 to avoid bilinear interpolation blur
    if (!context->point_sampler) {
        struct gs_sampler_info sampler_info = {
            .filter = GS_FILTER_POINT,
            .address_u = GS_ADDRESS_CLAMP,
            .address_v = GS_ADDRESS_CLAMP,
        };
        context->point_sampler = gs_samplerstate_create(&sampler_info);
    }

    // Set CRT shader parameters
    gs_eparam_t *image_param = gs_effect_get_param_by_name(context->crt_effect, "image");
    gs_effect_set_texture(image_param, input_tex);

    // Use point (nearest-neighbor) sampler when blur_strength is 0 for sharp pixel rendering
    // Otherwise OBS uses bilinear filtering which causes blur when upscaling
    if (context->blur_strength == 0.0f && context->point_sampler) {
        gs_effect_set_next_sampler(image_param, context->point_sampler);
    }

    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "scan_line_distance"),
                        context->scan_line_distance);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "scan_line_strength"),
                        context->scan_line_strength);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "pixel_width"), context->pixel_width);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "pixel_height"), context->pixel_height);
    // Note: blur/bloom strengths are set below with scale adjustment
    // Afterglow accumulation is done in `video_tick`; disable it in this render pass to avoid double persistence.
    gs_effect_set_int(gs_effect_get_param_by_name(context->crt_effect, "afterglow_duration_ms"), 0);
    gs_effect_set_int(gs_effect_get_param_by_name(context->crt_effect, "afterglow_curve"), context->afterglow.curve);
    gs_effect_set_int(gs_effect_get_param_by_name(context->crt_effect, "tint_mode"), context->tint_mode);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "tint_strength"), context->tint_strength);

    // Set afterglow parameters
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "dt_ms"), dt_ms);
    // Still bind something valid for safety, even though afterglow is disabled in this pass.
    gs_effect_set_texture(gs_effect_get_param_by_name(context->crt_effect, "texture_accum_prev"), input_tex);

    struct c64_effect_geometry geometry;
    c64_source_get_effect_geometry(context, &geometry);

    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "virtual_output_height"),
                        (float)geometry.virtual_height);

    // Set blur/bloom strengths directly - no scaling needed since we render at correct size
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "blur_strength"), context->blur_strength);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "bloom_strength"), context->bloom_strength);

    // Set source dimensions for UV snapping (sharp pixel expansion)
    // These are the original C64 dimensions before pixel_width/height scaling
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "source_width"), (float)context->width);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "source_height"), (float)context->height);

    while (gs_effect_loop(context->crt_effect, "Draw")) {
        gs_draw_sprite(input_tex, 0, geometry.draw_width, geometry.draw_height);
    }
    os_atomic_inc_long(&context->script_render_count);
}

void c64_try_init_stream_start_ns(struct c64_source *context, uint64_t packet_time_ns, const char *trigger)
{
    if (!context) {
        return;
    }

    if (os_atomic_load_bool(&context->stream_start_set)) {
        return;
    }

    if (pthread_mutex_lock(&context->stream_start_mutex) != 0) {
        return;
    }

    if (!os_atomic_load_bool(&context->stream_start_set)) {
        context->stream_start_ns = packet_time_ns;
        if (context->network_buffer && context->buffer_delay_ms > 0) {
            context->stream_start_ns += (uint64_t)context->buffer_delay_ms * 1000000ULL;
        }
        os_atomic_set_bool(&context->stream_start_set, true);

        // Reset per-stream synthetic counters at stream start.
        context->audio_packet_count = 0;
        context->last_audio_ts_seq = 0;
        context->audio_ts_seq_set = false;
        context->audio_packet_index = 0;
        context->audio_interval_ns = 0;
        context->last_audio_timestamp_validation = 0;

        context->last_video_ts_frame_num = 0;
        context->video_ts_frame_num_set = false;
        context->video_frame_index = 0;

        context->last_video_ts_ns = 0;
        context->last_audio_ts_ns = 0;

        context->first_video_ts_ns = 0;
        context->first_audio_ts_ns = 0;
        context->first_video_ts_logged = false;
        context->first_audio_ts_logged = false;
        context->initial_av_delta_logged = false;

        C64_LOG_INFO("STREAM START: stream_start_ns=%" PRIu64 " trigger=%s buffer_delay_ms=%u",
                     context->stream_start_ns, trigger ? trigger : "unknown", context->buffer_delay_ms);
    }

    pthread_mutex_unlock(&context->stream_start_mutex);
}

uint32_t c64_get_width(void *data)
{
    struct c64_source *context = data;
    if (!context)
        return 0;

    c64_update_format_hint_if_needed(context);
    struct c64_effect_geometry geometry;
    c64_source_get_effect_geometry(context, &geometry);
    return geometry.reported_width;
}

uint32_t c64_get_height(void *data)
{
    struct c64_source *context = data;
    if (!context)
        return 0;

    c64_update_format_hint_if_needed(context);
    struct c64_effect_geometry geometry;
    c64_source_get_effect_geometry(context, &geometry);
    return geometry.reported_height;
}

// Synchronous render callback removed - now using async video output via obs_source_output_video()

const char *c64_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("C64Stream");
}

obs_properties_t *c64_properties(void *data)
{
    return c64_create_properties(data);
}

void c64_defaults(obs_data_t *settings)
{
    c64_set_property_defaults(settings);
}

// Interaction callbacks for keyboard capture

void c64_mouse_click(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(event);
    UNUSED_PARAMETER(type);
    UNUSED_PARAMETER(mouse_up);
    UNUSED_PARAMETER(click_count);
    // No mouse interaction needed for C64 keyboard capture
}

void c64_mouse_move(void *data, const struct obs_mouse_event *event, bool mouse_leave)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(event);
    UNUSED_PARAMETER(mouse_leave);
    // No mouse interaction needed for C64 keyboard capture
}

void c64_mouse_wheel(void *data, const struct obs_mouse_event *event, int x_delta, int y_delta)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(event);
    UNUSED_PARAMETER(x_delta);
    UNUSED_PARAMETER(y_delta);
    // No mouse interaction needed for C64 keyboard capture
}

void c64_focus(void *data, bool focus)
{
    struct c64_source *context = (struct c64_source *)data;
    if (!context) {
        return;
    }

    if (focus) {
        // Enable capture when focused
        context->keyboard_capture_active = true;
        C64_LOG_INFO("Keyboard capture activated (source focused)");
        if (context->keyboard) {
            c64_keyboard_set_capture(context->keyboard, true);
        }
    } else if (!focus && context->keyboard_capture_active) {
        // Disable capture when focus lost
        context->keyboard_capture_active = false;
        C64_LOG_INFO("Keyboard capture deactivated (source lost focus)");
        if (context->keyboard) {
            c64_keyboard_set_capture(context->keyboard, false);
        }
    }
}

void c64_key_click(void *data, const struct obs_key_event *event, bool key_up)
{
    struct c64_source *context = (struct c64_source *)data;
    if (!context || !event) {
        C64_LOG_DEBUG("🕹 KEYBOARD: key_click called: context=%p, event=%p", (void *)context, (void *)event);
        return;
    }

    // Raw OBS key events are only useful in verbose mode; non-verbose logging happens
    // when the key is actually queued for C64 injection.
    C64_LOG_DEBUG("🕹 KEYBOARD: key_up=%d vkey=0x%04X scan=0x%02X mods=0x%02X text='%s'", key_up, event->native_vkey,
                  event->native_scancode, event->modifiers, event->text ? event->text : "");

    // UTF-8-aware printability: text is printable if it is non-empty, the first byte
    // is not a C0 control character (0x00-0x1F) or DEL (0x7F), and not a space.
    // This correctly handles multi-byte UTF-8 characters (e.g. £, ä, @-via-AltGr).
    const bool has_printable_text = (event->text && event->text[0] != '\0' && (unsigned char)event->text[0] > 0x20 &&
                                     (unsigned char)event->text[0] != 0x7F);

    const bool is_ctrl_key =
        (event->native_vkey == 0xFFE3 || event->native_vkey == 0xFFE4 || event->native_vkey == 0x11);
    const bool is_meta_key = (event->native_vkey == 0xFFEB || event->native_vkey == 0xFFEC ||
                              ((!has_printable_text) && (event->native_vkey == 0x5B || event->native_vkey == 0x5C)));
    const bool is_shift_key =
        (event->native_vkey == 0xFFE1 || event->native_vkey == 0xFFE2 || event->native_vkey == 0x10);
    // Linux/X11: AltGr sends XK_ISO_Level3_Shift (0xFE03), not in this list, so it
    // correctly does not set the CBM modifier bit.
    // Windows: AltGr generates VK_MENU (0x12). It is treated the same as left Alt here;
    // the synthetic Ctrl+Alt pattern is cleared at the point of keymap lookup below.
    const bool is_alt_key =
        (event->native_vkey == 0xFFE9 || event->native_vkey == 0xFFEA || event->native_vkey == 0x12);
    const bool is_modifier_key = (is_shift_key || is_ctrl_key || is_alt_key || is_meta_key);
    const bool is_escape_key = c64_interact_key_is_escape(event->native_vkey, event->native_scancode);
    const bool is_tab_key = c64_interact_key_is_tab(event->native_vkey, event->native_scancode);
    const bool shift_down = (event->modifiers & INTERACT_SHIFT_KEY) != 0;
    const bool ctrl_down = (event->modifiers & INTERACT_CONTROL_KEY) != 0;
    const bool alt_down = (event->modifiers & INTERACT_ALT_KEY) != 0;
    const bool meta_down = (event->modifiers & INTERACT_COMMAND_KEY) != 0;

    if (is_ctrl_key) {
        context->keyboard_ctrl_down = !key_up;
    }
    if (is_meta_key) {
        context->keyboard_meta_down = !key_up;
    }
    if (is_tab_key) {
        context->keyboard_tab_down = !key_up;
    }
    if (is_escape_key) {
        context->keyboard_escape_down = !key_up;
    }

    if (ctrl_down || alt_down || meta_down || key_up || !context->keyboard_tab_down || !context->keyboard_escape_down) {
        context->keyboard_reboot_consumed = false;
    }

    if (is_modifier_key) {
        if (!key_up && context->keyboard_ctrl_down && context->keyboard_meta_down &&
            !context->keyboard_ctrl_meta_armed && !context->keyboard_ctrl_meta_consumed) {
            context->keyboard_ctrl_meta_armed = true;
            if (context->keymap && context->keyboard) {
                c64_output_t output;
                if (c64_keymap_convert(context->keymap, "Ctrl+Meta", NULL, 0, &output)) {
                    c64_keyboard_queue_output(context->keyboard, &output);
                }
            }
        }

        if (key_up && (!context->keyboard_ctrl_down || !context->keyboard_meta_down)) {
            context->keyboard_ctrl_meta_armed = false;
            context->keyboard_ctrl_meta_consumed = false;
        }

        C64_LOG_DEBUG("🕹 KEYBOARD: Skipping modifier key");
        return;
    }

    // Only process key press events (not key up). Key-repeat on some platforms
    // can synthesize transient key-up events between repeats, so do not flush
    // non-verbose logging state here.
    if (key_up) {
        return;
    }

    // Check if capture is enabled
    if (!context->keyboard_capture_active) {
        C64_LOG_DEBUG("🕹 KEYBOARD: Capture not active (active=%d)", context->keyboard_capture_active);
        return;
    }

    // REST-backed machine control shortcuts.
    if (!context->keyboard_reboot_consumed &&
        c64_interact_should_reboot_chord(event->native_vkey, event->native_scancode, key_up, shift_down, ctrl_down,
                                         alt_down, meta_down, context->keyboard_escape_down,
                                         context->keyboard_tab_down)) {
        context->keyboard_reboot_consumed = true;
        C64_LOG_INFO("Keyboard: ESC+TAB pressed - performing C64 reboot");
        if (context->rest_client) {
            c64_rest_reboot(context->rest_client);
        }
        return;
    }

    if (is_escape_key && (ctrl_down || shift_down)) {
        C64_LOG_INFO("Keyboard: %s+ESC pressed - performing C64 reset", ctrl_down ? "Ctrl" : "Shift");
        if (context->rest_client) {
            c64_rest_reset(context->rest_client);
        }
        return;
    }

    // Convert OBS key event to keymap format and queue for injection
    if (context->keymap && context->keyboard) {
        if (context->keyboard_ctrl_meta_armed) {
            context->keyboard_ctrl_meta_consumed = true;
        }

        c64_interact_key_t key = {{0}};
        c64_interact_key_result_t key_result = c64_interact_translate_key_event(event->native_vkey, event->text, &key);
        if (key_result == C64_INTERACT_KEY_WARM_START) {
            if (context->keyboard) {
                c64_keyboard_basic_warm_start(context->keyboard);
            }
            return;
        }

        // Build modifiers bitmask
        int modifiers = 0;
        if (event->modifiers & INTERACT_SHIFT_KEY) {
            modifiers |= 0x01;
        }
        if (event->modifiers & INTERACT_CONTROL_KEY) {
            modifiers |= 0x02;
        }
        if (event->modifiers & INTERACT_ALT_KEY) {
            modifiers |= 0x04;
        }
        if (event->modifiers & INTERACT_COMMAND_KEY) {
            modifiers |= 0x08;
        }

        // AltGr separation: on Windows, AltGr is delivered as Ctrl+Alt (synthetic Ctrl
        // precedes right-Alt). If both Ctrl and Alt are set and the key produced printable
        // text, this is AltGr acting as a Level 3 shift - not a CBM modifier. Clear both
        // bits so the text-based lookup path handles the character instead.
        if ((modifiers & 0x06) == 0x06 && has_printable_text) {
            modifiers &= ~0x06;
        }

        C64_LOG_DEBUG("🕹 KEYBOARD: normalized code=%s text=%s mods=0x%02X", key.code[0] ? key.code : "<none>",
                      key.text[0] ? key.text : "<none>", modifiers);

        // Convert key to C64 output
        c64_output_t output;
        if (c64_keymap_convert(context->keymap, key.code, key.text, modifiers, &output)) {
            c64_keyboard_queue_output(context->keyboard, &output);
        }
    }
}
