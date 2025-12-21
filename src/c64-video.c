/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h> // For atomic operations
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include "c64-network.h"
#include "c64-network-buffer.h"

#include "c64-logging.h"
#include "c64-video.h"
#include "c64-logo.h"
#include "c64-audio.h"
#include "c64-color.h"
#include "c64-types.h"
#include "c64-protocol.h"
#include "c64-record-network.h"
#include "c64-protocol.h"
#include "c64-record.h"
#include "c64-source.h"

#ifdef _WIN32
#include <timeapi.h>
#pragma comment(lib, "winmm.lib") // For timeBeginPeriod/timeEndPeriod
#endif

#include "c64-protocol.h"

// Forward declarations
static uint64_t c64_calculate_ideal_timestamp(struct c64_source *context, uint16_t frame_num);

// Helper functions for frame assembly (updated to use lock-free implementation)
void c64_init_frame_assembly(struct frame_assembly *frame, uint16_t frame_num)
{
    memset(frame, 0, sizeof(struct frame_assembly));
    frame->frame_num = frame_num;
    frame->start_time = os_gettime_ns();
    frame->received_packets = 0;
    frame->expected_packets = 0;
    frame->complete = false;
    frame->packets_received_mask = 0;
}

bool c64_is_frame_complete(struct frame_assembly *frame)
{
    uint16_t received = frame->received_packets;
    uint16_t expected = frame->expected_packets;

    if (expected == 0) {
        return false;
    }

    bool complete = (received >= expected);

    static uint16_t last_debug_frame = 0;
    static uint64_t last_debug_time = 0;
    uint64_t now = os_gettime_ns();

    if (frame->frame_num != last_debug_frame && received > 0 &&
        (last_debug_time == 0 || (now - last_debug_time) > 1000000000ULL)) {
        C64_LOG_DEBUG("🎬 Frame completion check: frame %u has %u/%u packets (complete=%d)", frame->frame_num, received,
                      expected, complete);
        last_debug_frame = frame->frame_num;
        last_debug_time = now;
    }

    if (complete && !frame->complete) {
        frame->complete = true;
        // Periodic frame completion monitoring (every 5 minutes)
        static int completion_debug_count = 0;
        static uint64_t last_completion_log_time = 0;
        uint64_t now = os_gettime_ns();
        if ((++completion_debug_count % 5000) == 0 ||
            (now - last_completion_log_time >= 300000000000ULL)) { // Every 5k completions OR 5 minutes
            C64_LOG_DEBUG("🎬 Frame COMPLETION SPOT CHECK: frame %u with %u/%u packets! (total count: %d)",
                          frame->frame_num, received, expected, completion_debug_count);
            last_completion_log_time = now;
        }
    }

    return complete;
}

bool c64_is_frame_timeout(struct frame_assembly *frame)
{
    uint64_t elapsed = (os_gettime_ns() - frame->start_time) / 1000000; // Convert to ms
    return elapsed > C64_FRAME_TIMEOUT_MS;
}

// Direct frame rendering with row interpolation for missing packets
void c64_render_frame_direct(struct c64_source *context, struct frame_assembly *frame, uint64_t timestamp_ns)
{
    // First, assemble the frame with interpolation for missing rows
    c64_assemble_frame_with_interpolation(context, frame);

    // Generate monotonic timestamp based on frame sequence for butter-smooth playback
    uint64_t monotonic_timestamp = c64_calculate_ideal_timestamp(context, frame->frame_num);

    // Save frame to disk if enabled
    if (context->save_frames) {
        c64_save_frame_as_bmp(context, context->frame_buffer);

        // Note: CSV logging for video events is now handled independently in the video processor thread
    }

    // Record frame to video file if recording is enabled
    if (context->record_video) {
        c64_record_video_frame(context, context->frame_buffer);
    }

    // Direct async video output - optimized for low latency
    // This ensures the source always shows video regardless of CRT effects
    struct obs_source_frame obs_frame = {0};

    // Set up frame data - RGBA format optimized for immediate display
    obs_frame.data[0] = (uint8_t *)context->frame_buffer;
    obs_frame.linesize[0] = context->width * 4; // 4 bytes per pixel (RGBA)
    obs_frame.width = context->width;
    obs_frame.height = context->height;
    obs_frame.format = VIDEO_FORMAT_RGBA;
    obs_frame.timestamp = monotonic_timestamp; // Synthetic timestamp for smooth low-latency playback
    obs_frame.flip = false;                    // No vertical flip needed

    // Output frame directly to OBS
    obs_source_output_video(context->source, &obs_frame);

    // Log video frame delivery to CSV if enabled (high-level event: complete frame delivered to OBS)
    if (context->timing_file) {
        uint64_t calculated_timestamp_ms = monotonic_timestamp / 1000000; // Convert ns to ms
        uint64_t actual_timestamp_ms = os_gettime_ns() / 1000000;
        size_t frame_size = context->width * context->height * 4; // RGBA bytes
        c64_obs_log_video_event(context, frame->frame_num, calculated_timestamp_ms, actual_timestamp_ms, frame_size);
    }

    // Update timing and status
    context->last_frame_time = monotonic_timestamp;
    context->frames_delivered_to_obs++;
    os_atomic_set_long(&context->video_frames_processed, os_atomic_load_long(&context->video_frames_processed) + 1);

    // Periodic timestamp debugging (every 5 minutes)
    static int timestamp_debug_count = 0;
    static uint64_t last_timestamp_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++timestamp_debug_count % 10000) == 0 ||
        (now - last_timestamp_log_time >= 300000000000ULL)) { // Every 10k frames OR 5 minutes
        C64_LOG_DEBUG("🎬 MONOTONIC SPOT CHECK: frame=%u, monotonic_ts=%" PRIu64 ", packet_ts=%" PRIu64
                      ", delta=%+" PRId64 ", packets=%u/%u (count: %d)",
                      frame->frame_num, monotonic_timestamp, timestamp_ns,
                      (int64_t)(monotonic_timestamp - timestamp_ns), frame->received_packets, frame->expected_packets,
                      timestamp_debug_count);
        last_timestamp_log_time = now;
    }
}

// Simplified frame assembly with row interpolation for missing packets
void c64_assemble_frame_with_interpolation(struct c64_source *context, struct frame_assembly *frame)
{
    // Periodic frame assembly monitoring (every 5 minutes)
    static int assembly_debug_count = 0;
    static uint64_t last_assembly_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++assembly_debug_count % 5000) == 0 ||
        (now - last_assembly_log_time >= 300000000000ULL)) { // Every 5k frames OR 5 minutes
        C64_LOG_DEBUG("🎬 ASSEMBLY SPOT CHECK: frame %u with %u/%u packets (count: %d)", frame->frame_num,
                      frame->received_packets, frame->expected_packets, assembly_debug_count);
        last_assembly_log_time = now;
    }

    // Track which lines have been written
    bool *line_written = calloc(context->height, sizeof(bool));
    if (!line_written) {
        C64_LOG_ERROR("Failed to allocate line tracking array");
        return;
    }

    // First pass: assemble all received packets
    for (int i = 0; i < C64_MAX_PACKETS_PER_FRAME; i++) {
        struct frame_packet *packet = &frame->packets[i];
        if (!packet->received)
            continue;

        uint16_t line_num = packet->line_num;
        uint8_t lines_per_packet = packet->lines_per_packet;

        for (int line = 0; line < (int)lines_per_packet && (int)(line_num + line) < (int)context->height; line++) {
            uint32_t current_line = line_num + line;
            if (current_line >= context->height)
                break;

            uint32_t dst_line_offset = current_line * C64_PIXELS_PER_LINE;
            uint32_t *dst_line = context->frame_buffer + dst_line_offset;
            uint8_t *src_line = packet->packet_data + (line * C64_BYTES_PER_LINE);

            c64_convert_pixels_optimized(src_line, dst_line, C64_BYTES_PER_LINE);
            line_written[current_line] = true;
        }
    }

    // Second pass: interpolate missing lines by duplicating the nearest valid line above
    for (uint32_t line = 0; line < context->height; line++) {
        if (!line_written[line]) {
            // Find nearest valid line above this one
            uint32_t source_line = 0;
            for (uint32_t search = line; search > 0; search--) {
                if (line_written[search - 1]) {
                    source_line = search - 1;
                    break;
                }
            }

            // Copy the source line to fill the gap
            uint32_t *dst = context->frame_buffer + (line * C64_PIXELS_PER_LINE);
            uint32_t *src = context->frame_buffer + (source_line * C64_PIXELS_PER_LINE);
            memcpy(dst, src, C64_PIXELS_PER_LINE * sizeof(uint32_t));
        }
    }

    free(line_written);
}

void c64_process_video_statistics_batch(struct c64_source *context, uint64_t current_time)
{
    static const uint64_t STATS_INTERVAL_NS = 5000000000ULL;

    uint64_t time_since_last_log = current_time - context->last_stats_log_time;
    if (time_since_last_log < STATS_INTERVAL_NS) {
        return;
    }
    uint64_t packets_received = (uint64_t)os_atomic_load_long(&context->video_packets_received);
    uint64_t bytes_received = (uint64_t)os_atomic_load_long(&context->video_bytes_received);
    uint32_t frames_processed = (uint32_t)os_atomic_load_long(&context->video_frames_processed);
    os_atomic_set_long(&context->video_packets_received, 0);
    os_atomic_set_long(&context->video_bytes_received, 0);
    os_atomic_set_long(&context->video_frames_processed, 0);

    double duration_seconds = time_since_last_log / 1000000000.0;
    double packets_per_second = packets_received / duration_seconds;
    double bandwidth_mbps = (bytes_received * 8.0) / (duration_seconds * 1000000.0);
    double frames_per_second = frames_processed / duration_seconds;
    // No loss percentage calculation - we don't track sequence errors anymore

    double expected_fps = context->format_detected ? context->expected_fps : 50.0;
    double frame_delivery_rate = context->frames_delivered_to_obs / duration_seconds;
    double frame_completion_rate = context->frames_completed / duration_seconds;
    double capture_drop_pct =
        context->frames_expected > 0
            ? (100.0 * (context->frames_expected - context->frames_captured)) / context->frames_expected
            : 0.0;
    double delivery_drop_pct =
        context->frames_completed > 0
            ? (100.0 * (context->frames_completed - context->frames_delivered_to_obs)) / context->frames_completed
            : 0.0;
    double avg_pipeline_latency = context->frames_delivered_to_obs > 0
                                      ? context->total_pipeline_latency / (context->frames_delivered_to_obs * 1000000.0)
                                      : 0.0;
    if (packets_received > 0) {
        C64_LOG_INFO("📺 VIDEO: %.1f fps | %.2f Mbps | %.0f pps | Frames: %u", frames_per_second, bandwidth_mbps,
                     packets_per_second, (uint32_t)frames_processed);
        C64_LOG_INFO("🎯 DELIVERY: Expected %.0f fps | Captured %.1f fps | Delivered %.1f fps | Completed %.1f fps",
                     expected_fps, context->frames_captured / duration_seconds, frame_delivery_rate,
                     frame_completion_rate);
        C64_LOG_INFO("📊 PIPELINE: Capture drops %.1f%% | Delivery drops %.1f%% | Avg latency %.1f ms",
                     capture_drop_pct, delivery_drop_pct, avg_pipeline_latency);
    }

    // Reset diagnostic counters and update timestamp
    context->frames_expected = 0;
    context->frames_captured = 0;
    context->frames_delivered_to_obs = 0;
    context->frames_completed = 0;
    context->total_pipeline_latency = 0;
    context->last_stats_log_time = current_time;
}

void c64_process_audio_statistics_batch(struct c64_source *context, uint64_t current_time)
{
    static const uint64_t STATS_INTERVAL_NS = 5000000000ULL;

    uint64_t time_since_last_log = current_time - context->last_stats_log_time;
    if (time_since_last_log < STATS_INTERVAL_NS) {
        return;
    }
    uint64_t packets_received = (uint64_t)os_atomic_load_long(&context->audio_packets_received);
    uint64_t bytes_received = (uint64_t)os_atomic_load_long(&context->audio_bytes_received);
    os_atomic_set_long(&context->audio_packets_received, 0);
    os_atomic_set_long(&context->audio_bytes_received, 0);

    if (packets_received > 0) {
        double duration_seconds = time_since_last_log / 1000000000.0;
        double packets_per_second = packets_received / duration_seconds;
        double bandwidth_mbps = (bytes_received * 8.0) / (duration_seconds * 1000000.0);

        C64_LOG_INFO("🔊 AUDIO: %.2f Mbps | %.0f pps | Packets: %llu", bandwidth_mbps, packets_per_second,
                     (unsigned long long)packets_received);
    }
}

bool c64_try_add_packet_lockfree(struct frame_assembly *frame, uint16_t packet_index)
{
    if (packet_index >= C64_MAX_PACKETS_PER_FRAME) {
        return false;
    }

    uint64_t packet_mask = 1ULL << packet_index;
    uint64_t old_mask = frame->packets_received_mask;
    frame->packets_received_mask |= packet_mask;

    if (old_mask & packet_mask) {
        return false;
    }

    frame->received_packets++;
    return true;
}

// Video thread function
void *c64_video_thread_func(void *data)
{
    struct c64_source *context = data;
    uint8_t packet[C64_VIDEO_PACKET_SIZE];

    C64_LOG_DEBUG("Video receiver thread started on port %u", context->video_port);

#ifdef _WIN32
    HANDLE thread_handle = GetCurrentThread();
    if (SetThreadPriority(thread_handle, THREAD_PRIORITY_ABOVE_NORMAL)) {
        C64_LOG_DEBUG("Set video receiver thread to above-normal priority on Windows");
    } else {
        C64_LOG_WARNING("Failed to set video receiver thread priority on Windows");
    }

    timeBeginPeriod(1);
#endif

    C64_LOG_DEBUG("Video thread function started with optimized scheduling");

    while (os_atomic_load_bool(&context->thread_active)) {
        // Check socket validity before each recv call (prevents Windows WSAENOTSOCK errors)
        if (context->video_socket == INVALID_SOCKET_VALUE) {
            os_sleep_ms(10); // Wait a bit before checking again
            continue;
        }

        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        ssize_t received = recvfrom(context->video_socket, (char *)packet, (int)sizeof(packet), 0,
                                    (struct sockaddr *)&sender_addr, &sender_len);

        if (received < 0) {
            int error = c64_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
                Sleep(0);
                continue;
            }
            // On Windows, WSAENOTSOCK means socket was closed - this is normal during shutdown
            if (error == WSAENOTSOCK && context->video_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("Video socket closed (WSAENOTSOCK) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
            // On Windows, WSAESHUTDOWN means socket was shutdown - this is normal during reconnection
            if (error == WSAESHUTDOWN) {
                C64_LOG_DEBUG("Video socket shutdown (WSAESHUTDOWN) - waiting for reconnection");
                os_sleep_ms(100); // Wait for reconnection to complete
                continue;         // Continue waiting instead of exiting thread
            }
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
                os_sleep_ms(1);
                continue;
            }
            // On POSIX, EBADF means socket was closed - this is normal during shutdown
            if (error == EBADF && context->video_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("Video socket closed (EBADF) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
#endif
            C64_LOG_ERROR("Video socket error: %s (error code: %d)", c64_get_socket_error_string(error), error);
            break;
        }

        if (received != C64_VIDEO_PACKET_SIZE) {
            // Small packets (2-4 bytes) are normal during stream startup/buffer changes
            // Log as debug to avoid confusing users with normal control/startup packets
            static uint64_t last_incomplete_log_time = 0;
            uint64_t now = os_gettime_ns();
            if (now - last_incomplete_log_time >= 2000000000ULL) { // Throttle to every 2 seconds
                if (received <= 4) {
                    C64_LOG_DEBUG("Video startup/control packets: " SSIZE_T_FORMAT
                                  " bytes (normal during initialization)",
                                  SSIZE_T_CAST(received));
                } else {
                    C64_LOG_WARNING("Received incomplete video packet: " SSIZE_T_FORMAT " bytes (expected %d)",
                                    SSIZE_T_CAST(received), C64_VIDEO_PACKET_SIZE);
                }
                last_incomplete_log_time = now;
            }
            continue;
        }

        uint64_t packet_time = os_gettime_ns();
        context->last_udp_packet_time = packet_time; // DEPRECATED - kept for compatibility
        context->last_video_packet_time = packet_time;

        os_atomic_set_long(&context->video_packets_received, os_atomic_load_long(&context->video_packets_received) + 1);
        os_atomic_set_long(&context->video_bytes_received,
                           os_atomic_load_long(&context->video_bytes_received) + (long)received);

        // Log network packet at UDP reception (conditional - no parsing overhead if disabled)
        c64_log_video_packet_if_enabled(context, packet, received, packet_time);

        // Parse packet header for validation (always needed for packet validation)
        uint16_t pixels_per_line = *(uint16_t *)(packet + 6);
        uint8_t lines_per_packet = packet[8];
        uint8_t bits_per_pixel = packet[9];

        // Simple approach: just count packets received, no complex sequence tracking
        uint64_t now = os_gettime_ns();
        c64_process_video_statistics_batch(context, now);

        if (lines_per_packet != C64_LINES_PER_PACKET || pixels_per_line != C64_PIXELS_PER_LINE || bits_per_pixel != 4) {
            C64_LOG_WARNING("Invalid packet format: lines=%u, pixels=%u, bits=%u", lines_per_packet, pixels_per_line,
                            bits_per_pixel);
            continue;
        }

        if (context->network_buffer) {
            c64_network_buffer_push_video(context->network_buffer, packet, received, now);
        } else {
            c64_process_video_packet_direct(context, packet, received, now);
        }
    }

    C64_LOG_DEBUG("Video receiver thread stopped");

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    return NULL;
}

// Calculate ideal timestamp for a frame based on sequence number and video standard
static uint64_t c64_calculate_ideal_timestamp(struct c64_source *context, uint16_t frame_num)
{
    // Initialize timing base if not already set (could be set by audio)
    if (!context->timestamp_base_set) {
        context->stream_start_time_ns = os_gettime_ns();
        context->timestamp_base_set = true;
        C64_LOG_INFO("📐 Video timing base established: %" PRIu64 " ns", context->stream_start_time_ns);
    }

    // Set first frame reference for video calculations
    if (context->first_frame_num == 0 || frame_num < context->first_frame_num) {
        context->first_frame_num = frame_num;
        C64_LOG_INFO("📐 Video first frame reference: %u", frame_num);
    }

    // Calculate frame offset from the first frame
    int32_t frame_offset = (int32_t)(frame_num - context->first_frame_num);

    // Handle sequence number wraparound (16-bit counter)
    if (frame_offset < -32768) {
        frame_offset += 65536; // Wrapped forward
    } else if (frame_offset > 32768) {
        frame_offset -= 65536; // Wrapped backward
    }

    // Calculate ideal timestamp: base + (frame_offset * frame_interval)
    // Handle negative offsets correctly by using signed arithmetic first
    int64_t signed_offset_ns = (int64_t)frame_offset * (int64_t)context->frame_interval_ns;
    uint64_t ideal_timestamp = context->stream_start_time_ns + signed_offset_ns;

    // Debug log occasionally to verify timestamp calculation
    static uint32_t log_counter = 0;
    if ((log_counter++ % 250) == 0) { // Log every 250 frames (~5 seconds at 50Hz)
        C64_LOG_DEBUG("📐 Ideal timestamp: frame %u (offset %d) = %" PRIu64 " ns", frame_num, frame_offset,
                      ideal_timestamp);
    }

    return ideal_timestamp;
}

// Render black screen fallback when no logo is available
static void c64_render_black_screen(struct c64_source *context, uint64_t timestamp_ns)
{
    if (!context->frame_buffer) {
        return;
    }

    // Fill frame buffer with black (fully transparent in RGBA)
    uint32_t *buffer = context->frame_buffer;
    uint32_t width = context->width;
    uint32_t height = context->height;

    // Black screen: 0x00000000 (fully transparent black in RGBA)
    memset(buffer, 0, width * height * sizeof(uint32_t));

    // Output black frame via async video - ALWAYS output to maintain video stream
    struct obs_source_frame obs_frame = {0};
    obs_frame.data[0] = (uint8_t *)context->frame_buffer;
    obs_frame.linesize[0] = context->width * 4; // 4 bytes per pixel (RGBA)
    obs_frame.width = context->width;
    obs_frame.height = context->height;
    obs_frame.format = VIDEO_FORMAT_RGBA;
    obs_frame.timestamp = timestamp_ns;
    obs_frame.flip = false;

    obs_source_output_video(context->source, &obs_frame);

    // Periodic black screen monitoring (every 10 minutes)
    static int black_screen_debug_count = 0;
    static uint64_t last_black_screen_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++black_screen_debug_count % 10000) == 0 ||
        (now - last_black_screen_log_time >= 600000000000ULL)) { // Every 10k renders OR 10 minutes
        C64_LOG_DEBUG("⚫ BLACK SCREEN SPOT CHECK: %ux%u RGBA, timestamp=%" PRIu64 " (total count: %d)", context->width,
                      context->height, timestamp_ns, black_screen_debug_count);
        last_black_screen_log_time = now;
    }
}

// Direct packet processing function
void c64_process_video_packet_direct(struct c64_source *context, const uint8_t *packet, size_t packet_size,
                                     uint64_t timestamp_ns)
{
    if (!context || !packet || packet_size != C64_VIDEO_PACKET_SIZE) {
        return;
    }

    // Parse packet header (streamlined - only what we need for processing)
    uint16_t seq_num = *(uint16_t *)(packet + 0);
    uint16_t frame_num = *(uint16_t *)(packet + 2);
    uint16_t line_num = *(uint16_t *)(packet + 4);
    uint8_t lines_per_packet = packet[8];

    bool last_packet = (line_num & 0x8000) != 0;
    line_num &= 0x7FFF;

    // Process packet with frame assembly and double buffering
    if (pthread_mutex_lock(&context->assembly_mutex) == 0) {
        // Track frame capture timing for diagnostics (per-frame, not per-packet)
        uint64_t capture_time = timestamp_ns;

        // Check if this is a new frame
        if (context->current_frame.frame_num != frame_num) {
            // Log frame transitions to detect skips and duplicates
            if (context->current_frame.frame_num != 0) {
                uint16_t expected_next = context->current_frame.frame_num + 1;
                int16_t frame_diff = (int16_t)(frame_num - expected_next);

                if (frame_diff > 0) {
                    C64_LOG_WARNING("📽️ FRAME SKIP: Expected frame %u, got %u (skipped %d frames)", expected_next,
                                    frame_num, frame_diff);
                } else if (frame_diff < 0) {
                    C64_LOG_WARNING("Frame sequence regression: Expected frame %u, got %u (offset %d frames)",
                                    expected_next, frame_num, -frame_diff);
                }
            }

            // Count expected and captured frames only on new frame start
            if (context->last_capture_time > 0) {
                context->frames_expected++;
            }
            context->frames_captured++;
            context->last_capture_time = capture_time;

            // Complete previous frame if it exists and is reasonably complete
            if (context->current_frame.received_packets > 0) {
                if (c64_is_frame_complete(&context->current_frame) || c64_is_frame_timeout(&context->current_frame)) {
                    if (c64_is_frame_complete(&context->current_frame)) {
                        if (context->last_completed_frame != context->current_frame.frame_num) {
                            // Calculate ideal timestamp for smooth OBS async video rendering
                            uint64_t ideal_timestamp =
                                c64_calculate_ideal_timestamp(context, context->current_frame.frame_num);

                            // Direct rendering - assembly and output with ideal timestamp!
                            c64_render_frame_direct(context, &context->current_frame, ideal_timestamp);
                            context->last_completed_frame = context->current_frame.frame_num;

                            // Track diagnostics
                            context->frames_completed++;
                            context->total_pipeline_latency += (os_gettime_ns() - capture_time);
                        }
                    } else {
                        // Frame timeout - log drop and continue
                        C64_LOG_WARNING("⏰ FRAME TIMEOUT: Frame %u timed out with %u/%u packets (%.1f%% complete)",
                                        context->current_frame.frame_num, context->current_frame.received_packets,
                                        context->current_frame.expected_packets,
                                        (context->current_frame.received_packets * 100.0f) /
                                            context->current_frame.expected_packets);
                        context->frame_drops++;
                    }
                }
            }

            // Initialize new frame
            c64_init_frame_assembly(&context->current_frame, frame_num);
        }

        // Add packet to current frame
        uint32_t packet_index = line_num / lines_per_packet;
        if (packet_index < C64_MAX_PACKETS_PER_FRAME) {
            struct frame_packet *fp = &context->current_frame.packets[packet_index];
            if (!fp->received) {
                fp->line_num = line_num;
                fp->lines_per_packet = lines_per_packet;
                fp->received = true;
                memcpy(fp->packet_data, packet + C64_VIDEO_HEADER_SIZE, C64_VIDEO_PACKET_SIZE - C64_VIDEO_HEADER_SIZE);
                context->current_frame.received_packets++;
            }
        } else {
            C64_LOG_WARNING("❌ INVALID PACKET: Frame %u, Line %u out of range (packet_index %u >= %d) - seq %u",
                            frame_num, line_num, packet_index, C64_MAX_PACKETS_PER_FRAME, seq_num);
            context->packet_drops++;
        }

        // Update expected packet count and detect video format based on last packet
        if (last_packet && context->current_frame.expected_packets == 0) {
            context->current_frame.expected_packets = packet_index + 1;

            // Detect PAL vs NTSC format from frame height
            uint32_t frame_height = line_num + lines_per_packet;
            if (!context->format_detected || context->detected_frame_height != frame_height) {
                context->detected_frame_height = frame_height;
                context->format_detected = true;

                // Calculate expected FPS and frame interval based on detected format
                if (frame_height == C64_PAL_HEIGHT) {
                    context->expected_fps = 50.125;
                    context->frame_interval_ns = C64_PAL_FRAME_INTERVAL_NS;
                    context->last_connected_format_was_pal = true; // Update logo format preference
                    C64_LOG_INFO("🎥 Detected PAL format: 384x%u @ %.3f Hz", frame_height, context->expected_fps);
                } else if (frame_height == C64_NTSC_HEIGHT) {
                    context->expected_fps = 59.826;
                    context->frame_interval_ns = C64_NTSC_FRAME_INTERVAL_NS;
                    context->last_connected_format_was_pal = false; // Update logo format preference
                    C64_LOG_INFO("🎥 Detected NTSC format: 384x%u @ %.3f Hz", frame_height, context->expected_fps);
                } else {
                    // Unknown format, estimate based on packet count
                    context->expected_fps = (frame_height <= 250) ? 59.826 : 50.125;
                    context->frame_interval_ns = (frame_height <= 250) ? C64_NTSC_FRAME_INTERVAL_NS
                                                                       : C64_PAL_FRAME_INTERVAL_NS;
                    context->last_connected_format_was_pal = (frame_height > 250); // Assume PAL for larger heights
                    C64_LOG_WARNING("⚠️ Unknown video format: 384x%u, assuming %.3f Hz", frame_height,
                                    context->expected_fps);
                }

                // Update context dimensions if they changed
                if (context->height != frame_height) {
                    context->height = frame_height;
                    context->width = C64_PIXELS_PER_LINE; // Always 384
                }
            }
        }

        // Note: Frame completion is handled by the "complete previous frame" logic
        // when transitioning to a new frame. This avoids duplicate frame deliveries.

        pthread_mutex_unlock(&context->assembly_mutex);
    }
}

void *c64_video_processor_thread_func(void *data)
{
    struct c64_source *context = data;
    uint64_t last_logo_frame_time = 0;
    uint64_t last_retry_attempt = 0;
    const uint64_t logo_frame_interval_ns = 20000000ULL; // 50Hz (20ms) for logo frames
    const uint64_t retry_interval_ns = 1000000000ULL;    // 1 second retry interval

    C64_LOG_DEBUG("Video processor thread started");

    // Initialize last_frame_time to 0 so logo shows immediately on startup
    context->last_frame_time = 0;

    while (os_atomic_load_bool(&context->thread_active)) {
        uint64_t current_time = os_gettime_ns();
        bool packet_processed = false;

        if (context->network_buffer) {
            const uint8_t *video_data, *audio_data;
            size_t video_size, audio_size;
            uint64_t timestamp_us;

            if (c64_network_buffer_pop(context->network_buffer, &video_data, &video_size, &audio_data, &audio_size,
                                       &timestamp_us)) {

                if (video_data && video_size > 0) {
                    c64_process_video_packet_direct(context, video_data, video_size, timestamp_us * 1000);

                    // Reset retry count on successful video packet processing
                    if (context->retry_count > 0) {
                        C64_LOG_INFO("Video stream restored, resetting retry count (was %u)", context->retry_count);
                        context->retry_count = 0;
                    }
                }

                if (audio_data && audio_size > 0) {
                    c64_process_audio_packet(context, audio_data, audio_size, timestamp_us * 1000);
                }

                context->last_frame_time = current_time;
                packet_processed = true;
            }
        }

        // If no packets processed and enough time has passed, show logo at 50Hz
        if (!packet_processed) {
            uint64_t time_since_last_frame = current_time - context->last_frame_time;
            // Calculate time differences, handling potential underflow from timing precision issues
            uint64_t time_since_last_video = 0;
            if (current_time >= context->last_video_packet_time) {
                time_since_last_video = current_time - context->last_video_packet_time;
            } else {
                // Handle underflow case where last_video_packet_time is slightly in the future
                uint64_t timing_diff = context->last_video_packet_time - current_time;
                // Only log if the timing difference is very significant (>10ms), indicating a real timing problem
                if (timing_diff > 10000000) { // 10 milliseconds - anything less is normal precision variance
                    C64_LOG_DEBUG("Significant timing issue: last_video_packet_time ahead by %" PRIu64
                                  "ns (%.1fms) - investigating",
                                  timing_diff, (double)timing_diff / 1000000.0);
                }
                time_since_last_video = 0;
            }

            uint64_t time_since_last_logo = current_time - last_logo_frame_time;
            uint64_t time_since_last_retry = current_time - last_retry_attempt;

            // Sanity check to prevent timestamp overflow (should never exceed ~1 hour)
            if (time_since_last_video > 3600000000000ULL) {
                C64_LOG_DEBUG("Long-running stream: resetting video timing base after %" PRIu64 "ns (%.1f hours)",
                              time_since_last_video, (double)time_since_last_video / 3600000000000.0);
                context->last_video_packet_time = current_time;
                time_since_last_video = 0;
            }

            // Show logo if no frames for 3 seconds AND we haven't shown logo recently
            // Increased from 1s to 3s to reduce logo flashing during slider adjustments
            if (time_since_last_frame > 3000000000ULL && time_since_last_logo >= logo_frame_interval_ns) {
                if (c64_logo_is_available(context)) {
                    c64_logo_render_to_frame(context, current_time);
                } else {
                    // Fallback: render black screen if no logo available
                    c64_render_black_screen(context, current_time);
                }
                last_logo_frame_time = current_time;
                context->last_frame_time = current_time;
            }

            // Retry TCP connection and recreate UDP sockets if no VIDEO packets for 1+ seconds
            if (time_since_last_video > retry_interval_ns && time_since_last_retry >= retry_interval_ns &&
                !os_atomic_load_long(&context->retry_in_progress)) {
                uint64_t time_since_last_audio = current_time - context->last_audio_packet_time;
                C64_LOG_INFO(
                    "No video packets for %.1fs (audio: %.1fs), retrying TCP commands and recreating UDP sockets",
                    time_since_last_video / 1000000000.0, time_since_last_audio / 1000000000.0);

                last_retry_attempt = current_time;

                // Schedule retry in a background thread (never on OBS UI thread).
                c64_schedule_retry_task(context, "no video packets");
            }

            os_sleep_ms(1);
        }
    }

    C64_LOG_DEBUG("Video processor thread stopped");
    return NULL;
}
