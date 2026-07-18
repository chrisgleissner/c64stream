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
#include <math.h>
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
#include "c64-dimensions.h"
#include "c64-effect-clamp.h"
#include "c64-audio.h"
#include "c64-interact-key.h"
#include "c64-joystick-emulation.h"
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
static void c64_refresh_obs_ip(struct c64_source *context);
static void c64_set_expected_peer_ip(struct c64_source *context, const char *ip_string);
static void c64_stop_streaming_to(struct c64_source *context, const char *host, uint32_t control_port);
static void c64_complete_pending_device_transition(struct c64_source *context);
static void *c64_stop_streaming_thread(void *data);
static void c64_abort_stream_start(struct c64_source *context);
static void c64_attempt_script_autostart(struct c64_source *context, obs_data_t *settings);
static void c64_queue_properties_refresh(struct c64_source *context);
static void c64_rebuild_rest_client(struct c64_source *context);
static void c64_release_joystick_inputs(struct c64_source *context);
static bool c64_start_streaming_inner(struct c64_source *context);

static volatile long c64_next_default_port_pair;

static void c64_clamp_effect_params(struct c64_source *context)
{
    if (!context) {
        return;
    }

    context->pixel_width = c64_clamp_effect_float(context->pixel_width, 0.5f, 4.0f, 1.0f);
    context->pixel_height = c64_clamp_effect_float(context->pixel_height, 0.5f, 4.0f, 1.0f);
    context->scan_line_distance = c64_clamp_effect_float(context->scan_line_distance, 0.0f, 2.0f, 0.0f);
    context->scan_line_strength = c64_clamp_effect_float(context->scan_line_strength, 0.0f, 1.0f, 0.0f);
    context->blur_strength = c64_clamp_effect_float(context->blur_strength, 0.0f, 1.0f, 0.0f);
    context->bloom_strength = c64_clamp_effect_float(context->bloom_strength, 0.0f, 1.0f, 0.0f);
    context->tint_strength = c64_clamp_effect_float(context->tint_strength, 0.0f, 1.0f, 0.0f);
    context->afterglow.duration_ms = c64_clamp_effect_int(context->afterglow.duration_ms, 0, 3000);
    context->afterglow.curve = c64_clamp_effect_int(context->afterglow.curve, 0, 3);
    context->tint_mode = c64_clamp_effect_int(context->tint_mode, 0, 3);
}

static void c64_rebuild_rest_client(struct c64_source *context)
{
    if (!context) {
        return;
    }

    // C64STR-003: ip_address and c64_password are written by c64_update on the
    // OBS UI thread under config_mutex; this runs on the retry thread. Snapshot
    // both as a coherent pair under the lock so the REST URL/credential is never
    // built from a torn value.
    char ip_snapshot[64];
    char password_snapshot[256];
    pthread_mutex_lock(&context->config_mutex);
    snprintf(ip_snapshot, sizeof(ip_snapshot), "%s", context->ip_address);
    snprintf(password_snapshot, sizeof(password_snapshot), "%s", context->c64_password);
    pthread_mutex_unlock(&context->config_mutex);

    const bool have_valid_ip = ip_snapshot[0] && strcmp(ip_snapshot, "0.0.0.0") != 0;

    // C64STR-017 / C64STR-002: retarget the existing client in place whenever we
    // have a valid new target. This keeps the same rest_client and keyboard
    // objects alive, so a running C64Script -- whose runtime caches these
    // transport pointers -- continues on the new device instead of being
    // stopped and truncated. It also makes a live password change take effect
    // without tearing anything down.
    if (context->rest_client && have_valid_ip) {
        char new_base_url[sizeof(context->rest_base_url)];
        snprintf(new_base_url, sizeof(new_base_url), "http://%s", ip_snapshot);
        if (c64_rest_client_retarget(context->rest_client, new_base_url, password_snapshot)) {
            snprintf(context->rest_base_url, sizeof(context->rest_base_url), "%s", new_base_url);
            // The keyboard keeps the same (now retargeted) rest_client pointer;
            // only refresh its keymap/transport selection.
            if (context->keyboard) {
                c64_keyboard_set_keymap(context->keyboard, context->keymap);
                c64_keyboard_set_transport(context->keyboard, context->stream_control_transport);
            }
            c64_record_on_rest_client_ready(context);
            return;
        }
        C64_LOG_WARNING("REST client retarget failed; falling back to full rebuild");
    }

    // Full rebuild path: no existing client, an invalid/cleared target, or a
    // failed in-place retarget. Only here do we stop a running script, because
    // its cached transport pointers are about to be freed.
    if (context->script_executor && c64_script_executor_is_running(context->script_executor)) {
        C64_LOG_INFO("Stopping active script before replacing its keyboard transport");
        c64_script_executor_stop(context->script_executor);
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
    if (!have_valid_ip) {
        return;
    }
    snprintf(context->rest_base_url, sizeof(context->rest_base_url), "http://%s", ip_snapshot);
    context->rest_client = c64_rest_client_create(context->rest_base_url, password_snapshot);
    if (context->rest_client) {
        context->keyboard = c64_keyboard_create(context->rest_client);
        if (context->keyboard) {
            c64_keyboard_set_keymap(context->keyboard, context->keymap);
            c64_keyboard_set_transport(context->keyboard, context->stream_control_transport);
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

    const float configured_value = (float)obs_data_get_double(settings, key);
    return obs_data_has_user_value(settings, key) || isfinite(configured_value) ? configured_value : current_value;
}

static int c64_obs_data_get_int_or_current(obs_data_t *settings, const char *key, int current_value)
{
    if (!settings || !key) {
        return current_value;
    }

    (void)current_value;
    return (int)obs_data_get_int(settings, key);
}

void c64_source_apply_palette(struct c64_source *context, obs_data_t *settings)
{
    if (!context) {
        return;
    }

    const char *palette_id = settings ? obs_data_get_string(settings, "palette") : NULL;
    if (!palette_id || !palette_id[0]) {
        palette_id = "Default";
    }

    // Resolve the catalogue colours for this palette (loads from VPL on demand).
    // This reads only shared read-mostly catalogue state, never a shared LUT.
    uint32_t colors[16];
    if (!c64_palette_resolve_colors(palette_id, colors)) {
        // Unknown palette (e.g. deleted): fall back to Default so the source
        // still renders with a valid LUT rather than leaving it stale.
        if (!c64_palette_resolve_colors("Default", colors)) {
            memcpy(colors, c64_default_palette, sizeof(colors));
        }
        palette_id = "Default";
    }

    // Apply this source's own per-colour overrides (stored per source in its
    // OBS settings), so colour edits stay isolated to this instance.
    if (settings) {
        for (int i = 0; i < 16; i++) {
            char key[32];
            snprintf(key, sizeof(key), "palette_color_%d", i);
            if (obs_data_has_user_value(settings, key)) {
                uint32_t obs_color = (uint32_t)obs_data_get_int(settings, key);
                colors[i] = (obs_color & 0x00FFFFFF) | 0xFF000000;
            }
        }
    }

    pthread_mutex_lock(&context->palette_mutex);
    if (!context->palette_initialized) {
        c64_color_lut_init(&context->color_lut, colors);
        context->palette_initialized = true;
    } else {
        c64_color_lut_update(&context->color_lut, colors);
    }
    strncpy(context->palette_id, palette_id, sizeof(context->palette_id) - 1);
    context->palette_id[sizeof(context->palette_id) - 1] = '\0';
    pthread_mutex_unlock(&context->palette_mutex);
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
    c64_refresh_obs_ip(context);
    if (os_atomic_compare_swap_long(&context->rest_rebuild_pending, 1, 0)) {
        c64_rebuild_rest_client(context);
    }
    c64_complete_pending_device_transition(context);

    char ip_address[64];
    char obs_ip_address[64];
    uint32_t video_port;
    uint32_t audio_port;
    uint32_t control_port;
    pthread_mutex_lock(&context->config_mutex);
    snprintf(ip_address, sizeof(ip_address), "%s", context->ip_address);
    snprintf(obs_ip_address, sizeof(obs_ip_address), "%s", context->obs_ip_address);
    video_port = context->video_port;
    audio_port = context->audio_port;
    control_port = context->control_port;
    pthread_mutex_unlock(&context->config_mutex);

    bool tcp_success = false;

    if (!context->streaming) {
        // Initial streaming start - full setup with fresh UDP sockets
        tcp_success = c64_start_streaming(context);
        if (tcp_success) {
            context->consecutive_failures = 0;
        } else {
            context->consecutive_failures++;
        }
    } else {
        // Already streaming - test connectivity and send start commands
        // Use quick connectivity test instead of recreating sockets (avoids race conditions)
        if (c64_test_connectivity(ip_address, control_port)) {
            // We are explicitly requesting the peer to (re)start streaming now.
            context->last_start_command_time_ns = os_gettime_ns();
            char video_dest[C64_STREAM_DEST_MAX];
            char audio_dest[C64_STREAM_DEST_MAX];
            if (c64_build_stream_dest(video_dest, sizeof(video_dest), obs_ip_address, video_port) &&
                c64_build_stream_dest(audio_dest, sizeof(audio_dest), obs_ip_address, audio_port) &&
                c64_stream_control_to(context, ip_address, control_port, true, 0, video_dest) &&
                c64_stream_control_to(context, ip_address, control_port, true, 1, audio_dest)) {
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

    pthread_mutex_lock(&context->retry_thread_mutex);
    // C64STR-001: once destruction has begun, never spawn a new retry worker.
    // Checked under retry_thread_mutex, which destroy also holds while it sets
    // the flag and joins the outstanding worker, so a late scheduler either
    // creates a thread destroy will still join, or observes the flag and bails.
    if (context->retry_shutting_down) {
        pthread_mutex_unlock(&context->retry_thread_mutex);
        os_atomic_set_long(&context->retry_in_progress, 0);
        os_atomic_set_long(&context->retry_thread_active, 0);
        return;
    }
    if (context->retry_thread_valid) {
        const int join_result = pthread_join(context->retry_thread, NULL);
        if (join_result != 0) {
            C64_LOG_WARNING("Failed to join completed retry thread (err=%d)", join_result);
            pthread_mutex_unlock(&context->retry_thread_mutex);
            os_atomic_set_long(&context->retry_in_progress, 0);
            os_atomic_set_long(&context->retry_thread_active, 0);
            return;
        }
        context->retry_thread_valid = false;
    }

    int err = pthread_create(&context->retry_thread, NULL, c64_retry_thread_main, context);
    if (err != 0) {
        pthread_mutex_unlock(&context->retry_thread_mutex);
        os_atomic_set_long(&context->retry_in_progress, 0);
        os_atomic_set_long(&context->retry_thread_active, 0);
        C64_LOG_WARNING("Failed to start retry thread (%s)", reason ? reason : "no reason");
    } else {
        context->retry_thread_valid = true;
        pthread_mutex_unlock(&context->retry_thread_mutex);
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

    if (fclose(dst) != 0) {
        ok = false;
    }
    fclose(src);
    if (!ok) {
        remove(dst_path);
    }
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

/* Ultimate firmware needs a routable numeric address for its UDP destination;
 * discover it only from the background retry task. */
static void c64_refresh_obs_ip(struct c64_source *context)
{
    if (!context) {
        return;
    }

    char current_ip[64];
    char remote_ip[64];
    pthread_mutex_lock(&context->config_mutex);
    snprintf(current_ip, sizeof(current_ip), "%s", context->obs_ip_address);
    snprintf(remote_ip, sizeof(remote_ip), "%s", context->ip_address);
    pthread_mutex_unlock(&context->config_mutex);
    if (c64_stream_dest_is_ipv4(current_ip)) {
        return;
    }

    char detected_ip[64] = {0};
    if (!c64_detect_local_ip_for_host(remote_ip, NULL, detected_ip, sizeof(detected_ip)) &&
        !c64_detect_local_ip(detected_ip, sizeof(detected_ip))) {
        C64_LOG_ERROR(NETWORK_LOG_PREFIX " Unable to determine a numeric OBS IP address for stream destination");
        return;
    }

    pthread_mutex_lock(&context->config_mutex);
    snprintf(context->obs_ip_address, sizeof(context->obs_ip_address), "%s", detected_ip);
    pthread_mutex_unlock(&context->config_mutex);
    C64_LOG_INFO(NETWORK_LOG_PREFIX " Using numeric OBS IP address for Ultimate stream destination: %s", detected_ip);
}

// Helper function to safely close and reset sockets
static void close_and_reset_sockets(struct c64_source *context)
{
    uint32_t video_port;
    uint32_t audio_port;
    pthread_mutex_lock(&context->config_mutex);
    video_port = context->video_port;
    audio_port = context->audio_port;
    pthread_mutex_unlock(&context->config_mutex);
    if (context->video_socket != INVALID_SOCKET_VALUE) {
        C64_LOG_DEBUG("Closing video socket (port %u)", video_port);
        close(context->video_socket);
        context->video_socket = INVALID_SOCKET_VALUE;
        C64_LOG_DEBUG("Video socket closed and reset to INVALID_SOCKET_VALUE");
    }
    if (context->audio_socket != INVALID_SOCKET_VALUE) {
        C64_LOG_DEBUG("Closing audio socket (port %u)", audio_port);
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

    // C64STR-014: colour LUTs are per-source now; each instance builds its own
    // in c64_source_apply_palette below, so there is no global LUT to prime.

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
    const char *initial_device_id = obs_data_get_string(settings, "c64_device");
    snprintf(context->active_device_id, sizeof(context->active_device_id), "%s",
             initial_device_id ? initial_device_id : "");

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
    const bool has_saved_video_port = obs_data_has_user_value(settings, "video_port");
    const bool has_saved_audio_port = obs_data_has_user_value(settings, "audio_port");
    if (!has_saved_video_port && !has_saved_audio_port) {
        const long port_pair = os_atomic_inc_long(&c64_next_default_port_pair) - 1;
        uint32_t video_port = 0;
        uint32_t audio_port = 0;
        c64_default_ports_for_pair(port_pair, &video_port, &audio_port);
        obs_data_set_int(settings, "video_port", video_port);
        obs_data_set_int(settings, "audio_port", audio_port);
        C64_LOG_INFO("" NETWORK_LOG_PREFIX " Assigned default UDP ports video:%u audio:%u", video_port, audio_port);
    }
    context->video_port = (uint32_t)obs_data_get_int(settings, "video_port");
    context->audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");
    context->control_port = (uint32_t)obs_data_get_int(settings, "control_port");
    context->stream_control_transport = (int)obs_data_get_int(settings, "stream_control_transport");
    context->joystick_mode_active = obs_data_get_bool(settings, "joystick_mode_active");
    context->joystick_emulation_port = (int)obs_data_get_int(settings, "joystick_emulation_port");
    if (context->joystick_emulation_port != 1 && context->joystick_emulation_port != 2) {
        context->joystick_emulation_port = 2;
    }
    os_atomic_set_long(&context->rest_rebuild_pending, 0);
    context->streaming = false;

    // Read the configured OBS IP. Numeric route detection occurs in the
    // background retry task, never during source creation on the OBS UI thread.
    memset(context->obs_ip_address, 0, sizeof(context->obs_ip_address));
    const char *saved_obs_ip = obs_data_get_string(settings, "obs_ip_address");

    if (saved_obs_ip && strlen(saved_obs_ip) > 0) {
        strncpy(context->obs_ip_address, saved_obs_ip, sizeof(context->obs_ip_address) - 1);
        C64_LOG_INFO("" NETWORK_LOG_PREFIX " Using configured OBS IP address: %s", context->obs_ip_address);
    }
    context->initial_ip_detected = c64_stream_dest_is_ipv4(context->obs_ip_address);

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

    if (pthread_mutex_init(&context->retry_thread_mutex, NULL) != 0) {
        C64_LOG_ERROR("Failed to initialize retry thread mutex");
        c64_network_buffer_destroy(context->network_buffer);
        pthread_mutex_destroy(&context->stream_start_mutex);
        pthread_mutex_destroy(&context->config_mutex);
        pthread_mutex_destroy(&context->assembly_mutex);
        bfree(context->frame_buffer);
        bfree(context->bmp_row_buffer);
        bfree(context->bgr_frame_buffer);
        bfree(context);
        return NULL;
    }

    // C64STR-014: serialises per-source palette writers (UI-thread selects and
    // colour edits); readers stay lock-free via c64_color_lut_acquire.
    if (pthread_mutex_init(&context->palette_mutex, NULL) != 0) {
        C64_LOG_ERROR("Failed to initialize palette mutex");
        pthread_mutex_destroy(&context->retry_thread_mutex);
        c64_network_buffer_destroy(context->network_buffer);
        pthread_mutex_destroy(&context->stream_start_mutex);
        pthread_mutex_destroy(&context->config_mutex);
        pthread_mutex_destroy(&context->assembly_mutex);
        bfree(context->frame_buffer);
        bfree(context->bmp_row_buffer);
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
            pthread_mutex_destroy(&context->palette_mutex);
            pthread_mutex_destroy(&context->retry_thread_mutex);
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
    context->retry_thread_valid = false;
    os_atomic_set_bool(&context->udp_port_conflict, false);

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
    c64_clamp_effect_params(context);
    context->bloom_enable = context->bloom_strength > 0.0f;
    context->tint_enable = (context->tint_mode > 0 && context->tint_strength > 0.0f);

    context->frame_dirty = false;

    // Initialize palette from settings (must be done after palette system init)
    // Always select Default if settings are empty (first startup guarantee).
    // c64_palette_select tracks the catalogue's active id for the properties
    // dropdown; c64_source_apply_palette builds THIS source's own LUT so two
    // sources render with independent palettes (C64STR-014).
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
    c64_source_apply_palette(context, settings);

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
        if (context->keyboard) {
            c64_keyboard_set_keymap(context->keyboard, context->keymap);
            c64_keyboard_set_transport(context->keyboard, context->stream_control_transport);
        }
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

    // Start only after all transport inputs exist. The retry worker probes REST
    // capability, so scheduling it before the REST client is initialized would
    // incorrectly select the legacy transport on a capable device.
    C64_LOG_INFO("C64 Stream source created successfully - scheduling background initial connection");
    c64_schedule_retry(context, "initial connection");

    return context;
}

void c64_destroy(void *data)
{
    struct c64_source *context = data;
    if (!context)
        return;

    C64_LOG_INFO("Destroying C64 Stream source");

    c64_av_sync_cleanup(context);

    // Stop any background retry thread, including a completed joinable retry.
    // C64STR-001: latch retry_shutting_down under the same mutex so a video
    // processor thread that reaches c64_schedule_retry_task after this point
    // cannot spawn a new worker on the context we are about to free.
    pthread_mutex_lock(&context->retry_thread_mutex);
    context->retry_shutting_down = true;
    if (context->retry_thread_valid) {
        int join_result = pthread_join(context->retry_thread, NULL);
        if (join_result != 0) {
            C64_LOG_WARNING("Failed to join retry thread during destroy (err=%d)", join_result);
        }
        context->retry_thread_valid = false;
        os_atomic_set_long(&context->retry_thread_active, 0);
        os_atomic_set_long(&context->retry_in_progress, 0);
    }
    pthread_mutex_unlock(&context->retry_thread_mutex);

    // Stop streaming if active. This sends release_all and explicit remote
    // stream stops before closing local sockets.
    if (context->streaming) {
        C64_LOG_DEBUG("Stopping active streaming during destruction");
        pthread_t stop_thread;
        if (pthread_create(&stop_thread, NULL, c64_stop_streaming_thread, context) == 0) {
            pthread_join(stop_thread, NULL);
        } else {
            C64_LOG_ERROR("Failed to start background teardown thread; stopping source directly");
            c64_stop_streaming(context);
        }
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
    pthread_mutex_destroy(&context->retry_thread_mutex);
    pthread_mutex_destroy(&context->palette_mutex);
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
    if (!context)
        return;

    c64_device_registry_migrate_legacy(settings);
    const char *selected_device_id = obs_data_get_string(settings, "c64_device");
    if (strcmp(context->active_device_id, selected_device_id ? selected_device_id : "") != 0) {
        c64_device_registry_apply_selected(settings);
        snprintf(context->active_device_id, sizeof(context->active_device_id), "%s",
                 selected_device_id ? selected_device_id : "");
    }

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

    // Route discovery is deferred to the background retry task.
    bool new_auto_detect = obs_data_get_bool(settings, "auto_detect_ip");
    const bool auto_detect_changed = new_auto_detect != context->auto_detect_ip;
    context->auto_detect_ip = new_auto_detect;

    // Update configuration
    const char *new_host = obs_data_get_string(settings, "c64_host");
    const char *new_password = obs_data_get_string(settings, "c64_password");
    const char *new_obs_ip = obs_data_get_string(settings, "obs_ip_address");
    uint32_t new_video_port = (uint32_t)obs_data_get_int(settings, "video_port");
    uint32_t new_audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");
    uint32_t new_control_port = (uint32_t)obs_data_get_int(settings, "control_port");
    context->stream_control_transport = (int)obs_data_get_int(settings, "stream_control_transport");
    if (context->keyboard) {
        c64_keyboard_set_transport(context->keyboard, context->stream_control_transport);
    }
    const bool new_joystick_mode_active = obs_data_get_bool(settings, "joystick_mode_active");
    int new_joystick_emulation_port = (int)obs_data_get_int(settings, "joystick_emulation_port");
    if (new_joystick_emulation_port != 1 && new_joystick_emulation_port != 2) {
        new_joystick_emulation_port = 2;
    }
    // Changing either setting while a direction is held must release it before
    // the old route becomes unreachable. Without this, a key-up after a port
    // switch targets the new port and leaves the old port stuck pressed.
    if (context->joystick_mode_active &&
        (!new_joystick_mode_active || context->joystick_emulation_port != new_joystick_emulation_port)) {
        c64_release_joystick_inputs(context);
    }
    context->joystick_mode_active = new_joystick_mode_active;
    context->joystick_emulation_port = new_joystick_emulation_port;

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
    char old_ip_address[64];
    char old_dns_server_ip[64];
    char old_obs_ip[64];
    char old_password[sizeof(context->c64_password)];
    uint32_t old_video_port;
    uint32_t old_audio_port;
    uint32_t old_control_port;
    pthread_mutex_lock(&context->config_mutex);
    strncpy(old_hostname, context->hostname, sizeof(old_hostname) - 1);
    old_hostname[sizeof(old_hostname) - 1] = '\0';
    strncpy(old_dns_server_ip, context->dns_server_ip, sizeof(old_dns_server_ip) - 1);
    old_dns_server_ip[sizeof(old_dns_server_ip) - 1] = '\0';
    strncpy(old_ip_address, context->ip_address, sizeof(old_ip_address) - 1);
    old_ip_address[sizeof(old_ip_address) - 1] = '\0';
    snprintf(old_obs_ip, sizeof(old_obs_ip), "%s", context->obs_ip_address);
    snprintf(old_password, sizeof(old_password), "%s", context->c64_password);
    old_video_port = context->video_port;
    old_audio_port = context->audio_port;
    old_control_port = context->control_port;
    pthread_mutex_unlock(&context->config_mutex);

    const bool host_changed = (strcmp(old_hostname, new_host) != 0);
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");
    const char *new_dns_server_ip = (dns_server_ip && dns_server_ip[0] != '\0') ? dns_server_ip : "";
    const bool dns_changed = (strcmp(old_dns_server_ip, new_dns_server_ip) != 0);
    const char *new_obs_ip_str = new_obs_ip ? new_obs_ip : "";
    const bool obs_ip_changed = (strcmp(old_obs_ip, new_obs_ip_str) != 0);

    // Check if ports have changed (requires socket recreation)
    bool ports_changed = (new_video_port != old_video_port) || (new_audio_port != old_audio_port) ||
                         (new_control_port != old_control_port);

    // `streaming` only turns true at the very end of c64_start_streaming, but
    // that function has already told the device to stream long before then.
    // Testing `streaming` alone lets a switch that lands inside the start
    // window record no transition, stranding the previous device streaming at
    // OBS with nothing left to stop it. If neither flag is set, no start has
    // read ip_address yet, so the retry worker will start the new host directly
    // and there is genuinely nothing to stop.
    const bool peer_may_be_streaming = context->streaming || os_atomic_load_bool(&context->stream_start_in_flight);
    const bool needs_device_transition = (ports_changed || host_changed) && peer_may_be_streaming;
    if (needs_device_transition) {
        if (ports_changed) {
            C64_LOG_INFO(
                "Port configuration changed (video: %u->%u, audio: %u->%u, control: %u->%u), recreating sockets",
                old_video_port, new_video_port, old_audio_port, new_audio_port, old_control_port, new_control_port);
        }
        if (host_changed) {
            C64_LOG_INFO("C64 host changed (%s->%s), restarting streaming", old_hostname, new_host);
        }

        /* The retry worker owns remote teardown so no network I/O runs on
         * OBS's UI thread. Keep the existing REST client alive until it has
         * released keys and stopped this explicit old endpoint. */
        if (!context->device_transition_pending) {
            snprintf(context->device_transition_host, sizeof(context->device_transition_host), "%s", old_ip_address);
            context->device_transition_control_port = old_control_port;
            context->device_transition_pending = true;
        }
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
    if (new_obs_ip) {
        strncpy(context->obs_ip_address, new_obs_ip, sizeof(context->obs_ip_address) - 1);
        context->obs_ip_address[sizeof(context->obs_ip_address) - 1] = '\0';
    }
    if (new_auto_detect && auto_detect_changed) {
        context->obs_ip_address[0] = '\0';
    }
    context->video_port = new_video_port;
    context->audio_port = new_audio_port;
    context->control_port = new_control_port;
    c64_set_expected_peer_ip(context, context->ip_address);
    pthread_mutex_unlock(&context->config_mutex);

    const bool password_changed = strcmp(old_password, new_password ? new_password : "") != 0;
    if ((host_changed || password_changed) && !needs_device_transition) {
        context->stream_rest_demoted_until_ns = 0;
        os_atomic_set_long(&context->rest_rebuild_pending, 1);
    }
    if (ports_changed || obs_ip_changed) {
        os_atomic_set_bool(&context->udp_port_conflict, false);
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

    // Update palette selection if changed. Keep the catalogue's active id in
    // sync for the properties dropdown, then rebuild this source's own LUT
    // from its settings (id + per-source colour overrides) — C64STR-014.
    const char *palette_id = obs_data_get_string(settings, "palette");
    if (palette_id && palette_id[0]) {
        const char *current_palette = c64_palette_get_active_id();
        if (!current_palette || strcmp(current_palette, palette_id) != 0) {
            c64_palette_select(palette_id);
        }
    }
    c64_source_apply_palette(context, settings);

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
    c64_clamp_effect_params(context);
    context->bloom_enable = context->bloom_strength > 0.0f;
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
                                       dns_changed || auto_detect_changed || password_changed;
    if (should_schedule_retry) {
        const char *reason = !context->streaming ? "update (not streaming)"
                             : host_changed      ? "update (host changed)"
                             : ports_changed     ? "update (ports changed)"
                             : obs_ip_changed    ? "update (OBS IP changed)"
                             : dns_changed       ? "update (DNS changed)"
                             : password_changed  ? "update (password changed)"
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

bool c64_start_streaming(struct c64_source *context)
{
    if (!context) {
        C64_LOG_WARNING("Cannot start streaming - invalid context");
        return false;
    }

    // Must be raised before the first read of ip_address below, so that any
    // concurrent c64_update either sees this flag (and records the endpoint we
    // are about to start, so it gets stopped) or has already rewritten
    // ip_address (so we start the new host and there is nothing to strand).
    os_atomic_set_bool(&context->stream_start_in_flight, true);
    const bool started = c64_start_streaming_inner(context);
    os_atomic_set_bool(&context->stream_start_in_flight, false);
    return started;
}

static bool c64_start_streaming_inner(struct c64_source *context)
{
    char ip_address[64];
    char obs_ip_address[64];
    uint32_t video_port;
    uint32_t audio_port;
    uint32_t control_port;
    pthread_mutex_lock(&context->config_mutex);
    snprintf(ip_address, sizeof(ip_address), "%s", context->ip_address);
    snprintf(obs_ip_address, sizeof(obs_ip_address), "%s", context->obs_ip_address);
    video_port = context->video_port;
    audio_port = context->audio_port;
    control_port = context->control_port;
    pthread_mutex_unlock(&context->config_mutex);

    C64_LOG_INFO("Starting C64 Stream streaming to C64 %s (OBS IP: %s, video:%u, audio:%u)...", ip_address,
                 obs_ip_address, video_port, audio_port);

    // Proactively disconnect all streams before starting to ensure clean state
    // This prevents stale streaming state on the C64U from previous sessions
    if (strcmp(ip_address, "0.0.0.0") != 0) {
        C64_LOG_DEBUG("Sending proactive disconnect for all streams before starting");
        c64_stream_control_to(context, ip_address, control_port, false, 0, NULL); // Stop video
        c64_stream_control_to(context, ip_address, control_port, false, 1, NULL); // Stop audio
        // Brief delay to ensure stop commands are processed before start commands
        os_sleep_ms(50);
    }

    // Ensure expected peer IP matches current ip_address before binding sockets
    c64_set_expected_peer_ip(context, ip_address);

    // Stop existing threads BEFORE closing sockets (prevents race conditions on Windows)
    if (context->streaming) {
        context->streaming = false;
        os_atomic_set_bool(&context->thread_active, false);

        // Wait for existing threads to finish BEFORE closing their sockets.
        // All three must be joined: the processor thread's handle is overwritten
        // by the pthread_create below, so skipping it would leak a running
        // thread that can never be joined and let two processors race over the
        // same FIFO.
        if (os_atomic_load_bool(&context->video_thread_active) && pthread_join(context->video_thread, NULL) != 0) {
            C64_LOG_WARNING("Failed to join existing video thread during reconnection");
        }
        if (os_atomic_load_bool(&context->video_processor_thread_active) &&
            pthread_join(context->video_processor_thread, NULL) != 0) {
            C64_LOG_WARNING("Failed to join existing video processor thread during reconnection");
        }
        if (os_atomic_load_bool(&context->audio_thread_active) && pthread_join(context->audio_thread, NULL) != 0) {
            C64_LOG_WARNING("Failed to join existing audio thread during reconnection");
        }
        os_atomic_set_bool(&context->video_thread_active, false);
        os_atomic_set_bool(&context->video_processor_thread_active, false);
        os_atomic_set_bool(&context->audio_thread_active, false);
    }

    // Now safe to close existing sockets after threads have stopped
    close_and_reset_sockets(context);

    // Create fresh UDP sockets (required for reconnection after C64 restart)
    bool video_port_in_use = false;
    bool audio_port_in_use = false;
    context->video_socket = c64_create_udp_socket(video_port, &video_port_in_use);
    context->audio_socket = c64_create_udp_socket(audio_port, &audio_port_in_use);
    os_atomic_set_bool(&context->udp_port_conflict, video_port_in_use || audio_port_in_use);

    if (context->video_socket == INVALID_SOCKET_VALUE || context->audio_socket == INVALID_SOCKET_VALUE) {
        C64_LOG_ERROR("Failed to create UDP sockets for streaming");
        close_and_reset_sockets(context);
        return false;
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
        c64_audio_timeline_reset(&context->audio_timeline);
        context->audio_last_sample_set = false;
        context->network_error_last_warning_ns = 0;
        context->network_error_window_lost = 0;
        context->network_error_window_concealed = 0;
        context->network_error_window_late = 0;
        context->network_error_window_duplicates = 0;
        context->network_error_window_resyncs = 0;
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
    if (!c64_build_stream_dest(video_dest, sizeof(video_dest), obs_ip_address, video_port) ||
        !c64_build_stream_dest(audio_dest, sizeof(audio_dest), obs_ip_address, audio_port)) {
        C64_LOG_ERROR("" NETWORK_LOG_PREFIX " Failed to build stream destination for start command");
        close_and_reset_sockets(context);
        return false;
    }
    if (!c64_stream_control_to(context, ip_address, control_port, true, 0, video_dest) ||
        !c64_stream_control_to(context, ip_address, control_port, true, 1, audio_dest)) {
        C64_LOG_ERROR("" NETWORK_LOG_PREFIX " Failed to start C64 stream control");
        c64_abort_stream_start(context);
        return false;
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
        c64_abort_stream_start(context);
        return false;
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
        c64_abort_stream_start(context);
        return false;
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
        c64_abort_stream_start(context);
        return false;
    }
    os_atomic_set_bool(&context->audio_thread_active, true);

    C64_LOG_INFO("C64 Stream streaming started successfully");
    return true;
}

static void c64_abort_stream_start(struct c64_source *context)
{
    if (!context) {
        return;
    }

    /* A successful remote start can precede a local worker failure. Never
     * leave the device streaming or a matrix key held in that partial state. */
    if (context->keyboard) {
        c64_keyboard_release_all(context->keyboard);
    } else if (context->rest_client) {
        c64_rest_release_all(context->rest_client);
    }
    c64_stream_control(context, false, 0, NULL);
    c64_stream_control(context, false, 1, NULL);
    close_and_reset_sockets(context);
}

static void c64_stop_streaming_to(struct c64_source *context, const char *host, uint32_t control_port)
{
    if (!context || !host || !host[0]) {
        return;
    }

    /* Remote teardown precedes socket teardown so the old device is never left
     * streaming after a switch. release_all is deliberately attempted even if
     * stream control fails. */
    if (context->keyboard) {
        c64_keyboard_release_all(context->keyboard);
    } else if (context->rest_client) {
        c64_rest_release_all(context->rest_client);
    }
    if (!c64_stream_control_to(context, host, control_port, false, 0, NULL) ||
        !c64_stream_control_to(context, host, control_port, false, 1, NULL)) {
        C64_LOG_WARNING("Remote stream stop failed for %s", host);
    }
}

static void c64_stop_streaming_local(struct c64_source *context)
{
    if (!context || !context->streaming) {
        return;
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

void c64_stop_streaming(struct c64_source *context)
{
    if (!context) {
        return;
    }

    C64_LOG_INFO("Stopping C64 Stream streaming...");
    const char *host = context->device_transition_pending ? context->device_transition_host : context->ip_address;
    const uint32_t control_port = context->device_transition_pending ? context->device_transition_control_port
                                                                     : context->control_port;
    c64_stop_streaming_to(context, host, control_port);
    c64_stop_streaming_local(context);
}

static void *c64_stop_streaming_thread(void *data)
{
    c64_stop_streaming(data);
    return NULL;
}

static void c64_complete_pending_device_transition(struct c64_source *context)
{
    if (!context || !context->device_transition_pending) {
        return;
    }

    char old_host[64];
    snprintf(old_host, sizeof(old_host), "%s", context->device_transition_host);
    const uint32_t old_control_port = context->device_transition_control_port;
    context->device_transition_pending = false;
    context->device_transition_host[0] = '\0';
    context->device_transition_control_port = 0;

    C64_LOG_INFO("Completing asynchronous device transition: stopping %s before starting %s", old_host,
                 context->ip_address);
    c64_stop_streaming_to(context, old_host, old_control_port);
    c64_stop_streaming_local(context);
    context->stream_rest_demoted_until_ns = 0;
    c64_rebuild_rest_client(context);
}

// Video tick callback - updates texture from async frame buffer when CRT effects are enabled
void c64_video_tick(void *data, float seconds)
{
    struct c64_source *context = data;
    if (!context)
        return;

    /* C64STR-008: snapshot the format as one coherent pair under the same lock
     * the video processor publishes it with. Never retain the assembly lock
     * over graphics calls. */
    uint32_t frame_width = 0;
    uint32_t frame_height = 0;
    c64_dimensions_snapshot(&context->assembly_mutex, &context->width, &context->height, &frame_width, &frame_height);

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
    if (!context->render_texture || context->render_texture_width != frame_width ||
        context->render_texture_height != frame_height) {
        obs_enter_graphics();
        if (context->render_texture) {
            gs_texture_destroy(context->render_texture);
        }
        // Must be dynamic: we update it every tick via gs_texture_set_image (which maps internally).
        context->render_texture = gs_texture_create(frame_width, frame_height, GS_RGBA, 1, NULL, GS_DYNAMIC);
        context->render_texture_width = frame_width;
        context->render_texture_height = frame_height;

        // Upload initial pixels immediately to avoid a black/undefined frame and to keep update path consistent.
        if (context->render_texture && context->frame_buffer) {
            gs_texture_set_image(context->render_texture, (const uint8_t *)context->frame_buffer, frame_width * 4,
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
        if (context->frame_buffer && frame_width > 0 && frame_height > 0) {
            const uint32_t *src_pixels = context->frame_buffer;
            if (context->afterglow_enable && context->afterglow.accum && context->afterglow.accum_valid &&
                context->afterglow.duration_ms > 0) {
                src_pixels = context->afterglow.accum;
            }
            obs_enter_graphics();
            gs_texture_set_image(context->render_texture, (const uint8_t *)src_pixels, frame_width * 4, false);
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
        c64_audio_timeline_reset(&context->audio_timeline);
        context->audio_last_sample_set = false;
        context->network_error_last_warning_ns = 0;
        context->network_error_window_lost = 0;
        context->network_error_window_concealed = 0;
        context->network_error_window_late = 0;
        context->network_error_window_duplicates = 0;
        context->network_error_window_resyncs = 0;
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

/* Joystick directions are held (press on key-down, release on key-up), unlike
 * the tap-oriented keyboard path. Whenever the joystick path stops receiving
 * key-ups -- focus loss, or F10 leaving joystick mode -- any direction still
 * down would stay pressed on the device forever, because the matching release
 * either never arrives or no longer routes here. release_all clears the whole
 * matrix, which is exactly the desired state on both transitions. */
static void c64_release_joystick_inputs(struct c64_source *context)
{
    if (!context || !context->joystick_mode_active) {
        return;
    }
    // C64STR-022: called from the OBS interact thread (focus loss, F10 toggle);
    // dispatch release-all asynchronously so it never blocks on device latency.
    if (context->keyboard) {
        c64_machine_command_t cmd = {.type = C64_MACHINE_CMD_RELEASE_ALL};
        c64_keyboard_queue_machine_command(context->keyboard, &cmd);
    } else if (context->rest_client) {
        c64_rest_release_all(context->rest_client);
    }
    // The device just cleared every held input; forget our own held-key state so
    // a later key-up does not emit a stray release for something already gone.
    memset(context->held_keys, 0, sizeof(context->held_keys));
}

// Held-key tracker (vkey -> slot). The `active` flag marks occupancy; a plain
// vkey==0 sentinel would be wrong because macOS Carbon keycode 0x00 is a real
// key ('A').
static int c64_find_held_slot(struct c64_source *context, uint32_t vkey)
{
    for (size_t i = 0; i < sizeof(context->held_keys) / sizeof(context->held_keys[0]); i++) {
        if (context->held_keys[i].active && context->held_keys[i].vkey == vkey) {
            return (int)i;
        }
    }
    return -1;
}

static int c64_alloc_held_slot(struct c64_source *context, uint32_t vkey)
{
    int existing = c64_find_held_slot(context, vkey);
    if (existing >= 0) {
        return existing; // already held (auto-repeat) -- caller dedupes
    }
    for (size_t i = 0; i < sizeof(context->held_keys) / sizeof(context->held_keys[0]); i++) {
        if (!context->held_keys[i].active) {
            return (int)i;
        }
    }
    return -1;
}

// Map a modifier key event to its C64 matrix input for positional ("game")
// keymaps, where the modifiers are literal C64 matrix keys and are mirrored as
// held press/release: Shift -> the two keyboard flippers (left/right shift are
// distinct C64 matrix positions), Ctrl -> the C64 Ctrl key, and Alt -> the
// Commodore (CBM) key (positional keymaps use Alt for CBM). The caller passes
// the modifier classification it already computed so the vkey lists are not
// duplicated; the vkey/scancode only disambiguate left vs right shift.
// Meta is deliberately not mirrored (it drives the Ctrl+Meta charset chord).
static const char *c64_positional_modifier_input(bool is_shift, bool is_ctrl, bool is_alt, uint32_t native_vkey,
                                                 uint32_t native_scancode)
{
    if (is_ctrl) {
        return "ctrl";
    }
    if (is_alt) {
        return "commodore";
    }
    if (is_shift) {
#if defined(__APPLE__)
        return native_vkey == 0x3C ? "right_shift" : "left_shift"; // kVK_RightShift
#else
        if (native_vkey == 0xFFE2) {
            return "right_shift"; // XK_Shift_R
        }
        if (native_vkey == 0x10) {
            // Windows VK_SHIFT is generic; the scancode disambiguates (0x36 = right).
            return native_scancode == 0x36 ? "right_shift" : "left_shift";
        }
        return "left_shift";
#endif
    }
    return NULL;
}

// Release every held keyboard/joystick input and clear the tracker. Used on
// focus loss so nothing stays stuck down on the C64 when capture stops.
static void c64_release_held_keys(struct c64_source *context)
{
    if (!context) {
        return;
    }
    bool any = false;
    for (size_t i = 0; i < sizeof(context->held_keys) / sizeof(context->held_keys[0]); i++) {
        if (context->held_keys[i].active) {
            any = true;
            break;
        }
    }
    if (!any) {
        return;
    }
    if (context->keyboard) {
        c64_machine_command_t cmd = {.type = C64_MACHINE_CMD_RELEASE_ALL};
        c64_keyboard_queue_machine_command(context->keyboard, &cmd);
    } else if (context->rest_client) {
        c64_rest_release_all(context->rest_client);
    }
    memset(context->held_keys, 0, sizeof(context->held_keys));
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
        c64_release_joystick_inputs(context);
        // Release any keys/flippers held at the moment focus was lost so they do
        // not stay pressed on the C64 (release_all clears joystick too, but the
        // above only fires in joystick mode; this covers held keystrokes).
        c64_release_held_keys(context);
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

    const bool is_ctrl_key = (event->native_vkey == 0xFFE3 || event->native_vkey == 0xFFE4 || event->native_vkey == 0x11
#ifdef __APPLE__
                              || event->native_vkey == 0x3B || event->native_vkey == 0x3E
#endif
    );
    const bool is_meta_key = (event->native_vkey == 0xFFEB || event->native_vkey == 0xFFEC ||
                              ((!has_printable_text) && (event->native_vkey == 0x5B || event->native_vkey == 0x5C))
#ifdef __APPLE__
                              || event->native_vkey == 0x37 || event->native_vkey == 0x36
#endif
    );
    const bool is_shift_key = (event->native_vkey == 0xFFE1 || event->native_vkey == 0xFFE2 ||
                               event->native_vkey == 0x10
#ifdef __APPLE__
                               || event->native_vkey == 0x38 || event->native_vkey == 0x3C
#endif
    );
    // Linux/X11: AltGr sends XK_ISO_Level3_Shift (0xFE03), not in this list, so it
    // correctly does not set the CBM modifier bit.
    // Windows: AltGr generates VK_MENU (0x12). It is treated the same as left Alt here;
    // the synthetic Ctrl+Alt pattern is cleared at the point of keymap lookup below.
    const bool is_alt_key = (event->native_vkey == 0xFFE9 || event->native_vkey == 0xFFEA || event->native_vkey == 0x12
#ifdef __APPLE__
                             || event->native_vkey == 0x3A || event->native_vkey == 0x3D
#endif
    );
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
        // Positional ("game") keymaps treat the modifiers as literal C64 matrix
        // keys, so a standalone Shift / Ctrl / Commodore(Alt) is mirrored as a
        // held press/release: the two Shift keys drive the keyboard flippers
        // (pinball), and games that read Ctrl or the Commodore key see them held.
        // Symbolic keymaps consume these modifiers to select characters, so they
        // stay skipped there and typing is unaffected.
        if (context->keyboard_capture_active && context->keyboard && c64_keymap_is_positional(context->keymap)) {
            const char *mod_input = c64_positional_modifier_input(is_shift_key, is_ctrl_key, is_alt_key,
                                                                  event->native_vkey, event->native_scancode);
            if (mod_input) {
                const int slot = c64_find_held_slot(context, event->native_vkey);
                const bool held = slot >= 0;
                // Dedupe: press only on the first key-down, release only if held.
                if ((!key_up && !held) || (key_up && held)) {
                    c64_machine_command_t cmd = {.type = C64_MACHINE_CMD_KEY, .key_press = !key_up};
                    snprintf(cmd.key_input, sizeof(cmd.key_input), "%s", mod_input);
                    c64_keyboard_queue_machine_command(context->keyboard, &cmd);
                    if (key_up) {
                        context->held_keys[slot].active = false;
                    } else {
                        const int free_slot = c64_alloc_held_slot(context, event->native_vkey);
                        if (free_slot >= 0) {
                            context->held_keys[free_slot].active = true;
                            context->held_keys[free_slot].vkey = event->native_vkey;
                            context->held_keys[free_slot].is_joystick = false;
                        }
                    }
                }
                return;
            }
        }

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

    // Check if capture is enabled. Checked before the key-up filter below
    // because joystick emulation needs both press and release (held
    // movement), unlike the tap-oriented keyboard path.
    if (!context->keyboard_capture_active) {
        C64_LOG_DEBUG("🕹 KEYBOARD: Capture not active (active=%d)", context->keyboard_capture_active);
        return;
    }

    // Joystick emulation mode (F10 toggles): cursor keys and space become
    // joystick press/release on the selected port instead of C64 keystrokes.
    // F9/F10/F11 themselves are handled below, on key-down only, so they
    // always work to toggle back out of this mode.
    if (context->joystick_mode_active) {
        const char *joystick_input = c64_joystick_input_for_vkey(event->native_vkey);
        if (joystick_input) {
            // Dedupe held directions: OS auto-repeat resends key-down for a key
            // that is already down, which would re-press and stutter the
            // joystick. Press only on the first key-down, release only if it was
            // actually held.
            const int slot = c64_find_held_slot(context, event->native_vkey);
            const bool held = slot >= 0;
            if (!key_up && held) {
                return; // auto-repeat of an already-held direction
            }
            if (key_up && !held) {
                return; // release for a direction we never pressed
            }
            // C64STR-022: enqueue for the async worker; never block the UI thread.
            if (context->keyboard) {
                c64_machine_command_t cmd = {.type = C64_MACHINE_CMD_JOYSTICK,
                                             .joystick_port = context->joystick_emulation_port,
                                             .joystick_press = !key_up};
                snprintf(cmd.joystick_input, sizeof(cmd.joystick_input), "%s", joystick_input);
                c64_keyboard_queue_machine_command(context->keyboard, &cmd);
            }
            if (key_up) {
                context->held_keys[slot].active = false;
            } else {
                const int free_slot = c64_alloc_held_slot(context, event->native_vkey);
                if (free_slot >= 0) {
                    context->held_keys[free_slot].active = true;
                    context->held_keys[free_slot].vkey = event->native_vkey;
                    context->held_keys[free_slot].is_joystick = true;
                    snprintf(context->held_keys[free_slot].joystick_input,
                             sizeof(context->held_keys[free_slot].joystick_input), "%s", joystick_input);
                }
            }
            return;
        }
    }

    // Release a held interactive key on its key-up so the C64 sees the key held
    // for exactly as long as the host key was down (real-time games). Keys that
    // were tapped (shifted chords, unmapped) are not tracked and fall through.
    if (key_up) {
        const int slot = c64_find_held_slot(context, event->native_vkey);
        if (slot >= 0 && !context->held_keys[slot].is_joystick) {
            if (context->keyboard) {
                // Replay the held key's resolved byte as a matrix RELEASE.
                c64_output_t release = {.mode = C64_OUTPUT_PETSCII,
                                        .data.petscii = context->held_keys[slot].output_byte};
                c64_keyboard_queue_output_ex(context->keyboard, &release, C64_KEY_RELEASE);
            }
            context->held_keys[slot].active = false;
        }
        return;
    }

    // REST-backed machine control shortcuts.
    if (!context->keyboard_reboot_consumed &&
        c64_interact_should_reboot_chord(event->native_vkey, event->native_scancode, key_up, shift_down, ctrl_down,
                                         alt_down, meta_down, context->keyboard_escape_down,
                                         context->keyboard_tab_down)) {
        context->keyboard_reboot_consumed = true;
        C64_LOG_INFO("Keyboard: ESC+TAB pressed - performing C64 reboot");
        // C64STR-022: async so a slow device cannot freeze the OBS UI thread.
        if (context->keyboard) {
            c64_machine_command_t cmd = {.type = C64_MACHINE_CMD_REBOOT};
            c64_keyboard_queue_machine_command(context->keyboard, &cmd);
        }
        return;
    }

    if (is_escape_key && (ctrl_down || shift_down)) {
        C64_LOG_INFO("Keyboard: %s+ESC pressed - performing C64 reset", ctrl_down ? "Ctrl" : "Shift");
        if (context->keyboard) {
            c64_machine_command_t cmd = {.type = C64_MACHINE_CMD_RESET};
            c64_keyboard_queue_machine_command(context->keyboard, &cmd);
        }
        return;
    }

    c64_interact_key_t key = {{0}};
    c64_interact_key_result_t key_result = c64_interact_translate_key_event(event->native_vkey, event->text, &key);
    if (key_result == C64_INTERACT_KEY_WARM_START) {
        if (context->keyboard) {
            c64_keyboard_basic_warm_start(context->keyboard);
        }
        return;
    }

    // F9 toggles the device's on-screen menu via REST; F10/F11 toggle
    // joystick emulation mode/port. All three are hoisted above the
    // keymap/keyboard guard below so they stay active (even while
    // joystick_mode_active, or while the keymap/keyboard worker failed to
    // load) so F10 can always toggle back out of joystick mode.
    if (strcmp(key.code, "F9") == 0) {
        C64_LOG_INFO("Keyboard: F9 pressed - toggling device menu");
        if (context->keyboard) {
            c64_machine_command_t cmd = {.type = C64_MACHINE_CMD_MENU};
            c64_keyboard_queue_machine_command(context->keyboard, &cmd);
        }
        return;
    }

    const bool is_f10 = c64_joystick_classify_hotkey(key.code) == C64_JOYSTICK_HOTKEY_F10;
    const bool is_f11 = c64_joystick_classify_hotkey(key.code) == C64_JOYSTICK_HOTKEY_F11;
    if (is_f10 || is_f11) {
        if (is_f10) {
            // Release before clearing the flag: a direction held right now
            // would otherwise never see its key-up reach the joystick path.
            c64_release_joystick_inputs(context);
            context->joystick_mode_active = !context->joystick_mode_active;
            C64_LOG_INFO("Keyboard: F10 pressed - joystick emulation %s",
                         context->joystick_mode_active ? "enabled" : "disabled");
        } else {
            // A release after switching ports would otherwise be sent to the
            // new port, leaving the direction held on the old one.
            c64_release_joystick_inputs(context);
            context->joystick_emulation_port = context->joystick_emulation_port == 1 ? 2 : 1;
            C64_LOG_INFO("Keyboard: F11 pressed - joystick emulation port %d", context->joystick_emulation_port);
        }
        obs_data_t *toggle_settings = obs_source_get_settings(context->source);
        if (toggle_settings) {
            obs_data_set_bool(toggle_settings, "joystick_mode_active", context->joystick_mode_active);
            obs_data_set_int(toggle_settings, "joystick_emulation_port", context->joystick_emulation_port);
            obs_source_update(context->source, toggle_settings);
            obs_data_release(toggle_settings);
        }
        c64_queue_properties_refresh(context);
        return;
    }

    // Convert OBS key event to keymap format and queue for injection
    if (context->keymap && context->keyboard) {
        if (context->keyboard_ctrl_meta_armed) {
            context->keyboard_ctrl_meta_consumed = true;
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
            // Hold semantics: a key that resolves to a single unshifted matrix
            // key is pressed on key-down and released on its key-up (tracked by
            // vkey), so held keys reach the C64 held down for real-time games.
            // Auto-repeat key-downs for an already-held key are ignored. Shifted
            // chords and multi-byte text are not holdable and are tapped as
            // before (and not tracked), so typing is unchanged.
            uint8_t hold_byte = 0;
            if (c64_keyboard_output_is_holdable(&output, &hold_byte)) {
                if (c64_find_held_slot(context, event->native_vkey) >= 0) {
                    return; // auto-repeat of an already-held key
                }
                const int free_slot = c64_alloc_held_slot(context, event->native_vkey);
                if (free_slot >= 0) {
                    context->held_keys[free_slot].active = true;
                    context->held_keys[free_slot].vkey = event->native_vkey;
                    context->held_keys[free_slot].is_joystick = false;
                    context->held_keys[free_slot].output_byte = hold_byte;
                    c64_keyboard_queue_output_ex(context->keyboard, &output, C64_KEY_PRESS);
                } else {
                    // Tracker full (>16 keys down): tap so the key is not lost.
                    c64_keyboard_queue_output(context->keyboard, &output);
                }
            } else {
                c64_keyboard_queue_output(context->keyboard, &output);
            }
        }
    }
}
