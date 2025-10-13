/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <util/threading.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include "c64-network.h"
#include "c64-network-buffer.h"
#include "c64-logging.h"
#include "c64-source.h"
#include "c64-types.h"
#include "c64-protocol.h"
#include "c64-video.h"
#include "c64-color.h"
#include "c64-audio.h"
#include "c64-logo.h"
#include "c64-record.h"
#include "c64-version.h"
#include "c64-properties.h"
#include "plugin-support.h"

// Forward declarations
static void close_and_reset_sockets(struct c64_source *context);

// Async retry task - runs in OBS thread pool (NOT render thread)
void c64_async_retry_task(void *data)
{
    struct c64_source *context = (struct c64_source *)data;

    if (!context) {
        C64_LOG_WARNING("Async retry task called with NULL context");
        return;
    }

    C64_LOG_INFO("Async retry attempt %u - %s", context->retry_count,
                 context->streaming ? "sending start commands" : "starting streaming");

    bool tcp_success = false;

    if (!context->streaming) {
        // Initial streaming start - full setup with fresh UDP sockets
        c64_start_streaming(context);
        tcp_success = true; // c64_start_streaming handles TCP commands internally
    } else {
        // Already streaming - test connectivity and send start commands
        // Use quick connectivity test instead of recreating sockets (avoids race conditions)
        if (c64_test_connectivity(context->ip_address, context->control_port)) {
            c64_send_control_command(context, true, 0); // Video
            c64_send_control_command(context, true, 1); // Audio
            tcp_success = true;
            context->consecutive_failures = 0; // Reset failure counter on success
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
    context->retry_in_progress = false;
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
    C64_LOG_INFO("Creating C64S source");

    // C64S source creation

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

    // Load configuration file before initializing settings-dependent values
    c64_load_configuration(settings);

    context->source = source;

    // Initialize configuration from settings
    const char *host = obs_data_get_string(settings, "c64_host");
    const char *hostname = host ? host : C64_DEFAULT_HOST;

    // Store the original hostname/IP as entered by user
    strncpy(context->hostname, hostname, sizeof(context->hostname) - 1);
    context->hostname[sizeof(context->hostname) - 1] = '\0';

    // Get configured DNS server IP
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");

    // Resolve hostname to IP address for actual connections
    if (!c64_resolve_hostname_with_dns(hostname, dns_server_ip, context->ip_address, sizeof(context->ip_address))) {
        // If hostname resolution fails, store the hostname as-is (might be invalid IP like 0.0.0.0)
        strncpy(context->ip_address, hostname, sizeof(context->ip_address) - 1);
        context->ip_address[sizeof(context->ip_address) - 1] = '\0';
        C64_LOG_WARNING("Could not resolve hostname '%s', using as-is: %s", hostname, context->ip_address);
    } else {
        C64_LOG_INFO("Resolved C64 Ultimate host '%s' to IP: %s", hostname, context->ip_address);
    }

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
        C64_LOG_INFO("Using configured OBS IP address: %s", context->obs_ip_address);
    } else if (context->auto_detect_ip) {
        // Auto-detect local IP address only if auto-detection is enabled
        if (c64_detect_local_ip(context->obs_ip_address, sizeof(context->obs_ip_address))) {
            C64_LOG_INFO("Auto-detected OBS IP address: %s", context->obs_ip_address);
            context->initial_ip_detected = true;
            // Save the detected IP to settings for future use
            obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
        } else {
            C64_LOG_WARNING("Failed to auto-detect OBS IP address, will use localhost fallback");
            context->initial_ip_detected = false;
        }
    } else {
        C64_LOG_INFO("Auto-detection disabled, will use localhost fallback");
        context->initial_ip_detected = false;
    }

    // Ensure we have a valid OBS IP address - use localhost as last resort
    if (strlen(context->obs_ip_address) == 0) {
        C64_LOG_INFO("No OBS IP configured, using localhost as fallback");
        strncpy(context->obs_ip_address, "127.0.0.1", sizeof(context->obs_ip_address) - 1);
        obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
    }

    // Set default ports if not configured
    if (context->video_port == 0)
        context->video_port = C64_DEFAULT_VIDEO_PORT;
    if (context->audio_port == 0)
        context->audio_port = C64_DEFAULT_AUDIO_PORT;

    // Initialize video format (start with PAL, will be detected from stream)
    context->width = C64_PAL_WIDTH;
    context->height = C64_PAL_HEIGHT;

    // Allocate single frame buffer for direct async video output (RGBA, 4 bytes per pixel)
    size_t frame_size = context->width * context->height * sizeof(uint32_t);
    context->frame_buffer = bmalloc(frame_size);
    if (!context->frame_buffer) {
        C64_LOG_ERROR("Failed to allocate frame buffer");
        return NULL;
    }
    memset(context->frame_buffer, 0, frame_size);

    // Allocate pre-allocated recording buffers to eliminate malloc/free in hot paths
    context->recording_buffer_size = frame_size;                               // Same as RGBA frame size
    context->bmp_row_buffer = bmalloc(context->width * 4 + 4);                 // Row + padding for BMP
    context->bgr_frame_buffer = bmalloc(context->width * context->height * 3); // BGR24 frame
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
    context->expected_fps = 50.125; // Default to PAL timing until detected

    // Initialize mutexes (frame_mutex no longer needed for async video output)
    if (pthread_mutex_init(&context->assembly_mutex, NULL) != 0) {
        C64_LOG_ERROR("Failed to initialize assembly mutex");
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
    context->audio_info.samples_per_sec = 47976; // Exact C64 Ultimate sample rate
    context->audio_info.format = AUDIO_FORMAT_16BIT;
    context->audio_info.speakers = SPEAKERS_STEREO;

    // Log low-latency configuration
    C64_LOG_INFO("Low-latency mode: buffer_delay=%ums, audio_rate=%uHz", context->buffer_delay_ms,
                 context->audio_info.samples_per_sec);

    // Initialize statistics counters
    os_atomic_set_long(&context->video_packets_received, 0);
    os_atomic_set_long(&context->video_bytes_received, 0);
    os_atomic_set_long(&context->video_sequence_errors, 0);
    os_atomic_set_long(&context->video_frames_processed, 0);
    os_atomic_set_long(&context->audio_packets_received, 0);
    os_atomic_set_long(&context->audio_bytes_received, 0);
    context->last_stats_log_time = os_gettime_ns();

    // Initialize render callback timeout system
    uint64_t now = os_gettime_ns();
    context->last_udp_packet_time = now; // DEPRECATED - kept for compatibility
    context->last_video_packet_time = now;
    context->last_audio_packet_time = now;
    context->retry_in_progress = false;
    context->retry_count = 0;
    context->consecutive_failures = 0;

    // Initialize ideal timestamp generation
    context->stream_start_time_ns = 0;
    context->first_frame_num = 0;
    context->timestamp_base_set = false;
    context->frame_interval_ns = C64_PAL_FRAME_INTERVAL_NS; // Default to PAL, will be updated on detection

    // Initialize debug logging from settings (must be done before any debug logs)
    c64_debug_logging = obs_data_get_bool(settings, "debug_logging");
    C64_LOG_DEBUG("Debug logging initialized: %s", c64_debug_logging ? "enabled" : "disabled");

    // Initialize logo system with pre-rendered frame
    if (!c64_logo_init(context)) {
        C64_LOG_WARNING("Logo system initialization failed - continuing without logo");
    }

    // Initialize recording for this source
    c64_record_init(context);

    // Initialize CRT effect state from settings
    context->scan_line_distance = (float)obs_data_get_double(settings, "scan_line_distance");
    context->scan_line_strength = (float)obs_data_get_double(settings, "scan_line_strength");
    context->pixel_width = (float)obs_data_get_double(settings, "pixel_width");
    context->pixel_height = (float)obs_data_get_double(settings, "pixel_height");
    context->blur_strength = (float)obs_data_get_double(settings, "blur_strength");
    context->bloom_strength = (float)obs_data_get_double(settings, "bloom_strength");
    context->bloom_enable = context->bloom_strength > 0.0f;
    // TODO: Afterglow effect needs more work - force disabled for now
    context->afterglow_duration_ms = 0;
    context->afterglow_enable = false;
    context->afterglow_curve = 0;
    context->tint_mode = (int)obs_data_get_int(settings, "tint_mode");
    context->tint_strength = (float)obs_data_get_double(settings, "tint_strength");
    context->tint_enable = (context->tint_mode > 0 && context->tint_strength > 0.0f);
    context->render_texture = NULL;
    context->crt_effect = NULL;
    context->afterglow_accum_prev = NULL;
    context->afterglow_accum_next = NULL;
    context->last_frame_time_ns = 0;

    // Start initial connection asynchronously to avoid blocking OBS startup
    C64_LOG_INFO("C64S source created successfully - queuing async initial connection");
    context->retry_in_progress = true; // Prevent render thread from also starting retry
    obs_queue_task(OBS_TASK_UI, c64_async_retry_task, context, false);

    return context;
}

void c64_destroy(void *data)
{
    struct c64_source *context = data;
    if (!context)
        return;

    C64_LOG_INFO("Destroying C64S source");

    // No retry thread to shutdown - using async delegation approach

    // Stop streaming if active
    if (context->streaming) {
        C64_LOG_DEBUG("Stopping active streaming during destruction");
        context->streaming = false;
        os_atomic_set_bool(&context->thread_active, false);

        close_and_reset_sockets(context);
        if (os_atomic_load_bool(&context->video_thread_active)) {
            pthread_join(context->video_thread, NULL);
            os_atomic_set_bool(&context->video_thread_active, false);
        }
        if (os_atomic_load_bool(&context->video_processor_thread_active)) {
            pthread_join(context->video_processor_thread, NULL);
            os_atomic_set_bool(&context->video_processor_thread_active, false);
        }
        if (os_atomic_load_bool(&context->audio_thread_active)) {
            pthread_join(context->audio_thread, NULL);
            os_atomic_set_bool(&context->audio_thread_active, false);
        }
    }

    c64_record_cleanup(context);

    // Cleanup logo system
    c64_logo_cleanup(context);

    // Cleanup CRT effect resources (must be done in graphics context)
    obs_enter_graphics();
    if (context->render_texture) {
        gs_texture_destroy(context->render_texture);
        context->render_texture = NULL;
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
    obs_leave_graphics();

    // Cleanup resources
    pthread_mutex_destroy(&context->assembly_mutex);
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

    bfree(context);
    C64_LOG_INFO("C64S source destroyed");
}

void c64_update(void *data, obs_data_t *settings)
{
    struct c64_source *context = data;
    if (!context)
        return;

    // Update debug logging setting
    c64_debug_logging = obs_data_get_bool(settings, "debug_logging");
    C64_LOG_DEBUG("Debug logging %s", c64_debug_logging ? "enabled" : "disabled"); // Update IP detection setting
    bool new_auto_detect = obs_data_get_bool(settings, "auto_detect_ip");
    if (new_auto_detect != context->auto_detect_ip || new_auto_detect) {
        context->auto_detect_ip = new_auto_detect;
        if (new_auto_detect) {
            // Re-detect IP address
            if (c64_detect_local_ip(context->obs_ip_address, sizeof(context->obs_ip_address))) {
                C64_LOG_INFO("Updated OBS IP address: %s", context->obs_ip_address);
                // Save the updated IP to settings
                obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
            } else {
                C64_LOG_WARNING("Failed to update OBS IP address");
            }
        }
    }

    // Update configuration
    const char *new_host = obs_data_get_string(settings, "c64_host");
    const char *new_obs_ip = obs_data_get_string(settings, "obs_ip_address");
    uint32_t new_video_port = (uint32_t)obs_data_get_int(settings, "video_port");
    uint32_t new_audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");
    uint32_t new_control_port = (uint32_t)obs_data_get_int(settings, "control_port");

    // Set defaults
    if (!new_host)
        new_host = C64_DEFAULT_HOST;
    if (new_video_port == 0)
        new_video_port = C64_DEFAULT_VIDEO_PORT;
    if (new_audio_port == 0)
        new_audio_port = C64_DEFAULT_AUDIO_PORT;
    if (new_control_port == 0)
        new_control_port = C64_CONTROL_PORT;

    // Check if ports have changed (requires socket recreation)
    bool ports_changed = (new_video_port != context->video_port) || (new_audio_port != context->audio_port) ||
                         (new_control_port != context->control_port);

    if (ports_changed && context->streaming) {
        C64_LOG_INFO("Port configuration changed (video: %u->%u, audio: %u->%u, control: %u->%u), recreating sockets",
                     context->video_port, new_video_port, context->audio_port, new_audio_port, context->control_port,
                     new_control_port);

        // Stop streaming and close existing sockets
        c64_stop_streaming(context);

        // Give the C64 Ultimate device time to process stop commands
        os_sleep_ms(100);
    }

    // Update configuration - hostname and IP resolution
    strncpy(context->hostname, new_host, sizeof(context->hostname) - 1);
    context->hostname[sizeof(context->hostname) - 1] = '\0';

    // Get configured DNS server IP
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");

    // Resolve hostname to IP address for connections
    if (!c64_resolve_hostname_with_dns(new_host, dns_server_ip, context->ip_address, sizeof(context->ip_address))) {
        // If hostname resolution fails, store the hostname as-is (might be invalid IP like 0.0.0.0)
        strncpy(context->ip_address, new_host, sizeof(context->ip_address) - 1);
        context->ip_address[sizeof(context->ip_address) - 1] = '\0';
        C64_LOG_WARNING("Could not resolve hostname '%s' during update, using as-is: %s", new_host,
                        context->ip_address);
    } else {
        C64_LOG_DEBUG("Resolved C64 Ultimate host '%s' to IP: %s", new_host, context->ip_address);
    }
    if (new_obs_ip) {
        strncpy(context->obs_ip_address, new_obs_ip, sizeof(context->obs_ip_address) - 1);
        context->obs_ip_address[sizeof(context->obs_ip_address) - 1] = '\0';
    }
    context->video_port = new_video_port;
    context->audio_port = new_audio_port;
    context->control_port = new_control_port;

    // Update buffer delay setting with debouncing to prevent timestamp reset storms
    uint32_t new_buffer_delay_ms = (uint32_t)obs_data_get_int(settings, "buffer_delay_ms");
    if (new_buffer_delay_ms != context->buffer_delay_ms) {
        uint32_t old_buffer_delay_ms = context->buffer_delay_ms;
        C64_LOG_INFO("Buffer delay changed from %u to %u ms", old_buffer_delay_ms, new_buffer_delay_ms);

        context->buffer_delay_ms = new_buffer_delay_ms;

        // Update network buffer delay (this adjusts UDP packet buffering only)
        if (context->network_buffer) {
            c64_network_buffer_set_delay(
                context->network_buffer, new_buffer_delay_ms,
                new_buffer_delay_ms); // Adjust timing bases forward to maintain monotonic timestamps for OBS
            // This prevents audio pipeline confusion while correcting for buffer delay changes
            uint64_t delay_adjustment_ns = (uint64_t)(old_buffer_delay_ms - new_buffer_delay_ms) * 1000000ULL;

            // Adjust video timing base forward by the delay difference
            if (context->timestamp_base_set) {
                context->stream_start_time_ns += delay_adjustment_ns;
                C64_LOG_DEBUG("📐 Video timing base adjusted forward by %llu ms", delay_adjustment_ns / 1000000ULL);
            }

            // Adjust audio timing base forward by the delay difference
            if (context->audio_base_time != 0) {
                context->audio_base_time += delay_adjustment_ns;
                C64_LOG_DEBUG("🎵 Audio timing base adjusted forward by %llu ms", delay_adjustment_ns / 1000000ULL);
            }

            C64_LOG_INFO("🔄 A/V timing bases adjusted forward due to buffer delay change (%ums -> %ums)",
                         old_buffer_delay_ms, new_buffer_delay_ms);

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

    // Check if dimension-affecting effects were previously disabled
    bool prev_dimension_effects = (context->scan_line_distance > 0.0f) || (context->pixel_width != 1.0f) ||
                                  (context->pixel_height != 1.0f);

    // Update CRT effect settings
    context->scan_line_distance = (float)obs_data_get_double(settings, "scan_line_distance");
    context->scan_line_strength = (float)obs_data_get_double(settings, "scan_line_strength");
    context->pixel_width = (float)obs_data_get_double(settings, "pixel_width");
    context->pixel_height = (float)obs_data_get_double(settings, "pixel_height");
    context->blur_strength = (float)obs_data_get_double(settings, "blur_strength");
    context->bloom_strength = (float)obs_data_get_double(settings, "bloom_strength");
    context->bloom_enable = context->bloom_strength > 0.0f;
    // TODO: Afterglow effect needs more work - force disabled for now
    context->afterglow_duration_ms = 0;
    context->afterglow_enable = false;
    context->afterglow_curve = 0;
    context->tint_mode = (int)obs_data_get_int(settings, "tint_mode");
    context->tint_strength = (float)obs_data_get_double(settings, "tint_strength");
    context->tint_enable = (context->tint_mode > 0 && context->tint_strength > 0.0f);

    // Check if dimension-affecting effects are now enabled
    bool new_dimension_effects = (context->scan_line_distance > 0.0f) || (context->pixel_width != 1.0f) ||
                                 (context->pixel_height != 1.0f);

    // Reset timing base if dimension-affecting effects were just enabled during streaming
    if (!prev_dimension_effects && new_dimension_effects && context->timestamp_base_set && context->streaming) {
        context->timestamp_base_set = false;
        C64_LOG_INFO("🔄 Dimension-affecting effects activated - timing base reset to maintain A/V sync");
    }

    // Start streaming with current configuration (will create new sockets if needed)
    C64_LOG_INFO("Applying configuration and starting streaming");
    c64_start_streaming(context);
}

void c64_start_streaming(struct c64_source *context)
{
    if (!context) {
        C64_LOG_WARNING("Cannot start streaming - invalid context");
        return;
    }

    C64_LOG_INFO("Starting C64S streaming to C64 %s (OBS IP: %s, video:%u, audio:%u)...", context->ip_address,
                 context->obs_ip_address, context->video_port, context->audio_port);

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

    // Reset audio timestamp state for clean reconnection
    context->audio_base_time = 0;
    context->audio_packet_count = 0;
    context->last_audio_timestamp_validation = 0;
    C64_LOG_DEBUG("Audio timestamp state reset for reconnection");

    // Send start commands to C64 Ultimate
    c64_send_control_command(context, true, 0); // Start video
    c64_send_control_command(context, true, 1); // Start audio

    // Start fresh worker threads
    os_atomic_set_bool(&context->thread_active, true);
    context->streaming = true;

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

    C64_LOG_INFO("C64S streaming started successfully");
}

void c64_stop_streaming(struct c64_source *context)
{
    if (!context || !context->streaming) {
        C64_LOG_WARNING("Cannot stop streaming - invalid context or not streaming");
        return;
    }

    C64_LOG_INFO("Stopping C64S streaming...");

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

    C64_LOG_INFO("C64S streaming stopped");
}

// Video tick callback - updates texture from async frame buffer when CRT effects are enabled
void c64_video_tick(void *data, float seconds)
{
    UNUSED_PARAMETER(seconds);
    struct c64_source *context = data;
    if (!context)
        return;

    // Always update texture from frame buffer for consistent rendering
    // Update render texture if needed (create or recreate on size change)
    if (!context->render_texture || gs_texture_get_width(context->render_texture) != context->width ||
        gs_texture_get_height(context->render_texture) != context->height) {

        obs_enter_graphics();
        if (context->render_texture) {
            gs_texture_destroy(context->render_texture);
        }
        context->render_texture =
            gs_texture_create(context->width, context->height, GS_RGBA, 1, (const uint8_t **)&context->frame_buffer, 0);

        // Create afterglow accumulation textures (ping-pong buffers)
        // Use render dimensions (which include scaling effects) not base dimensions
        uint32_t render_width = c64_get_width(context);
        uint32_t render_height = c64_get_height(context);

        if (context->afterglow_accum_prev) {
            gs_texture_destroy(context->afterglow_accum_prev);
        }
        if (context->afterglow_accum_next) {
            gs_texture_destroy(context->afterglow_accum_next);
        }
        context->afterglow_accum_prev =
            gs_texture_create(render_width, render_height, GS_RGBA, 1, NULL, GS_RENDER_TARGET);
        context->afterglow_accum_next =
            gs_texture_create(render_width, render_height, GS_RGBA, 1, NULL, GS_RENDER_TARGET);

        obs_leave_graphics();
        if (!context->render_texture) {
            C64_LOG_ERROR("Failed to create render texture");
        }
        if (!context->afterglow_accum_prev || !context->afterglow_accum_next) {
            C64_LOG_ERROR("Failed to create afterglow accumulation textures");
        }

    } else {
        // Update texture with latest frame data
        // Validate frame buffer integrity before updating texture
        if (context->frame_buffer && context->width > 0 && context->height > 0) {
            obs_enter_graphics();
            gs_texture_set_image(context->render_texture, (const uint8_t *)context->frame_buffer, context->width * 4,
                                 false);
            obs_leave_graphics();
        } else {
            C64_LOG_WARNING("Frame buffer validation failed in video_tick - skipping texture update");
        }
    }
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
    } // Calculate delta time for afterglow effect
    uint64_t current_time_ns = obs_get_video_frame_time();
    float dt_ms = 0.0f;
    if (context->last_frame_time_ns != 0) {
        dt_ms = (float)(current_time_ns - context->last_frame_time_ns) / 1000000.0f; // Convert ns to ms
    }
    context->last_frame_time_ns = current_time_ns;

    // Check if any CRT effects are enabled
    bool any_effects_enabled =
        (context->scan_line_distance > 0.0f) || (context->bloom_strength > 0.0f) ||
        (context->afterglow_duration_ms > 0) || (context->tint_mode > 0 && context->tint_strength > 0.0f) ||
        (context->pixel_width != 1.0f || context->pixel_height != 1.0f) || context->blur_strength > 0.0f;

    // If no effects are enabled, use simple default rendering
    if (!any_effects_enabled) {
        gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        if (default_effect) {
            gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), context->render_texture);
            while (gs_effect_loop(default_effect, "Draw")) {
                gs_draw_sprite(context->render_texture, 0, context->width, context->height);
            }
        }
        return;
    }

    // Load CRT shader effect if not already loaded (only when effects are enabled)
    if (!context->crt_effect) {
        char *effect_path = obs_module_file("effects/crt_effect.effect");
        if (effect_path) {
            context->crt_effect = gs_effect_create_from_file(effect_path, NULL);
            bfree(effect_path);
            if (!context->crt_effect) {
                C64_LOG_ERROR("Failed to load CRT effect shader - falling back to default rendering");
            } else {
                // Reset timing base to prevent sync drift caused by shader compilation delay
                // Only reset if we're actually streaming (have established timing)
                if (context->timestamp_base_set) {
                    context->timestamp_base_set = false;
                    C64_LOG_INFO("🔄 CRT effect loaded during stream - timing base reset to maintain A/V sync");
                }
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
                }
                return;
            }
        } else {
            C64_LOG_ERROR("Failed to find CRT effect shader file - falling back to default rendering");
            gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
            if (default_effect) {
                gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), context->render_texture);
                while (gs_effect_loop(default_effect, "Draw")) {
                    gs_draw_sprite(context->render_texture, 0, context->width, context->height);
                }
            }
            return;
        }
    }

    // Set CRT shader parameters
    gs_effect_set_texture(gs_effect_get_param_by_name(context->crt_effect, "image"), context->render_texture);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "scan_line_distance"),
                        context->scan_line_distance);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "scan_line_strength"),
                        context->scan_line_strength);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "pixel_width"), context->pixel_width);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "pixel_height"), context->pixel_height);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "blur_strength"), context->blur_strength);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "bloom_strength"), context->bloom_strength);
    gs_effect_set_int(gs_effect_get_param_by_name(context->crt_effect, "afterglow_duration_ms"),
                      context->afterglow_duration_ms);
    gs_effect_set_int(gs_effect_get_param_by_name(context->crt_effect, "afterglow_curve"), context->afterglow_curve);
    gs_effect_set_int(gs_effect_get_param_by_name(context->crt_effect, "tint_mode"), context->tint_mode);
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "tint_strength"), context->tint_strength);

    // Set afterglow parameters
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "dt_ms"), dt_ms);
    if (context->afterglow_accum_prev) {
        gs_effect_set_texture(gs_effect_get_param_by_name(context->crt_effect, "texture_accum_prev"),
                              context->afterglow_accum_prev);
    }

    // Render the texture with the CRT effect using scaled dimensions
    uint32_t render_width = c64_get_width(context);
    uint32_t render_height = c64_get_height(context);

    // Set output resolution for scanline calculation
    gs_effect_set_float(gs_effect_get_param_by_name(context->crt_effect, "output_height"), (float)render_height);

    // If afterglow is enabled, we need to render to the accumulation buffer first,
    // then display from that buffer (creating proper temporal feedback)
    if (context->afterglow_enable && context->afterglow_accum_prev && context->afterglow_accum_next) {
        // === RENDER TO ACCUMULATION BUFFER (WITH AFTERGLOW APPLIED) ===
        gs_viewport_push();
        gs_projection_push();

        // Set up render target for accumulation buffer
        gs_set_render_target(context->afterglow_accum_next, NULL);
        gs_clear(GS_CLEAR_COLOR, &(struct vec4){0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0);

        // Set up orthographic projection for the accumulation buffer
        gs_ortho(0.0f, (float)render_width, 0.0f, (float)render_height, -100.0f, 100.0f);
        gs_set_viewport(0, 0, render_width, render_height);

        // Render with CRT effect (including afterglow) to accumulation buffer
        // This captures the shader output which includes the blended afterglow
        while (gs_effect_loop(context->crt_effect, "Draw")) {
            gs_draw_sprite(context->render_texture, 0, render_width, render_height);
        }

        // Restore previous render target and projection
        gs_set_render_target(NULL, NULL);
        gs_projection_pop();
        gs_viewport_pop();

        // === DISPLAY FROM ACCUMULATION BUFFER ===
        // Now draw the accumulated result (with afterglow) to the screen
        // Use default effect for simple texture display
        gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        if (default_effect) {
            gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), context->afterglow_accum_next);
            while (gs_effect_loop(default_effect, "Draw")) {
                gs_draw_sprite(context->afterglow_accum_next, 0, render_width, render_height);
            }
        }

        // Swap accumulation buffers for next frame
        // Next frame will use this frame's output as its "previous frame"
        gs_texture_t *temp = context->afterglow_accum_prev;
        context->afterglow_accum_prev = context->afterglow_accum_next;
        context->afterglow_accum_next = temp;
    } else {
        // === NO AFTERGLOW: RENDER DIRECTLY ===
        while (gs_effect_loop(context->crt_effect, "Draw")) {
            gs_draw_sprite(context->render_texture, 0, render_width, render_height);
        }
    }
}

// Helper function to get scanline scaling parameters based on distance setting
static void get_scanline_scaling_info(float scan_line_distance, uint32_t *total_pixels, uint32_t *scanline_pixels)
{
    if (scan_line_distance <= 0.25f) { // Tight
        *total_pixels = 5;             // spacing (Scanline, Gap): S1S1S1S1G1 S2S2S2S2G2 ...
        *scanline_pixels = 4;
    } else if (scan_line_distance <= 0.5f) { // Normal
        *total_pixels = 3;                   // spacing (Scanline, Gap): S1S1G1 S2S2G2 ...
        *scanline_pixels = 2;
    } else if (scan_line_distance <= 1.0f) { // Wide
        *total_pixels = 4;                   // spacing (Scanline, Gap): S1S1G1G1 S2S2G2G2 ...
        *scanline_pixels = 2;
    } else {               // Extra Wide (2.0f)
        *total_pixels = 3; // spacing (Scanline, Gap, Gap): S1G1G1 S2G2G2 ...
        *scanline_pixels = 1;
    }
}

uint32_t c64_get_width(void *data)
{
    struct c64_source *context = data;
    if (!context)
        return 0;

    // Check if any effects that change dimensions are enabled
    bool dimension_effects_enabled = (context->scan_line_distance > 0.0f) || context->pixel_width != 1.0f;

    if (!dimension_effects_enabled) {
        return context->width;
    }

    // Apply pixel geometry scaling for CRT effects
    float width_scale = context->pixel_width;

    // Scanlines require upscaling to accommodate gaps with integer pixel alignment
    // Each C64 pixel column needs an integer number of output pixels for crisp rendering
    if (context->scan_line_distance > 0.0f) {
        uint32_t total_pixels_per_unit, scanline_pixels_per_unit;
        get_scanline_scaling_info(context->scan_line_distance, &total_pixels_per_unit, &scanline_pixels_per_unit);

        // Total width = original_pixels * total_pixels_per_unit
        width_scale *= (float)total_pixels_per_unit;
    }

    return (uint32_t)((float)context->width * width_scale);
}

uint32_t c64_get_height(void *data)
{
    struct c64_source *context = data;
    if (!context)
        return 0;

    // Check if any effects that change dimensions are enabled
    bool dimension_effects_enabled = (context->scan_line_distance > 0.0f) || context->pixel_height != 1.0f;

    if (!dimension_effects_enabled) {
        return context->height;
    }

    // Apply pixel geometry scaling for CRT effects
    float height_scale = context->pixel_height;

    // Scanlines require upscaling to accommodate gaps with integer pixel alignment
    // Each C64 scanline needs an integer number of output pixels for crisp rendering
    if (context->scan_line_distance > 0.0f) {
        uint32_t total_pixels_per_unit, scanline_pixels_per_unit;
        get_scanline_scaling_info(context->scan_line_distance, &total_pixels_per_unit, &scanline_pixels_per_unit);

        // Total height = original_scanlines * total_pixels_per_unit
        height_scale *= (float)total_pixels_per_unit;
    }

    return (uint32_t)((float)context->height * height_scale);
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
