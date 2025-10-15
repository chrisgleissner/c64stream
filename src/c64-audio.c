/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h> // For atomic operations
#include <inttypes.h>       // For PRIu64, PRId64 format specifiers
#include "c64-network.h"    // Include network header first to avoid Windows header conflicts
#include "c64-logging.h"
#include "c64-audio.h"
#include "c64-types.h"
#include "c64-protocol.h"
#include "c64-video.h"
#include "c64-record.h"
#include "c64-record-network.h"

// Audio thread function
void *audio_thread_func(void *data)
{
    struct c64_source *context = data;
    uint8_t packet[C64_AUDIO_PACKET_SIZE];

    C64_LOG_DEBUG("Audio receiver thread started on port %u", context->audio_port);

    while (os_atomic_load_bool(&context->thread_active)) {
        // Check socket validity before each recv call (prevents Windows WSAENOTSOCK errors)
        if (context->audio_socket == INVALID_SOCKET_VALUE) {
            os_sleep_ms(10); // Wait a bit before checking again
            continue;
        }

        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        ssize_t received = recvfrom(context->audio_socket, (char *)packet, (int)sizeof(packet), 0,
                                    (struct sockaddr *)&sender_addr, &sender_len);

        if (received < 0) {
            int error = c64_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
                os_sleep_ms(1); // 1ms delay
                continue;
            }
            // On Windows, WSAENOTSOCK means socket was closed - this is normal during shutdown
            if (error == WSAENOTSOCK && context->audio_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("Audio socket closed (WSAENOTSOCK) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
            // On Windows, WSAESHUTDOWN means socket was shutdown - this is normal during reconnection
            if (error == WSAESHUTDOWN) {
                C64_LOG_DEBUG("Audio socket shutdown (WSAESHUTDOWN) - waiting for reconnection");
                os_sleep_ms(100); // Wait for reconnection to complete
                continue;         // Continue waiting instead of exiting thread
            }
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
                os_sleep_ms(1); // 1ms delay
                continue;
            }
            // On POSIX, EBADF means socket was closed - this is normal during shutdown
            if (error == EBADF && context->audio_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("Audio socket closed (EBADF) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
#endif
            C64_LOG_ERROR("Audio socket error: %s (error code: %d)", c64_get_socket_error_string(error), error);
            break;
        }

        if (received != C64_AUDIO_PACKET_SIZE) {
            // Small packets (2-4 bytes) are normal during stream startup/buffer changes
            static uint64_t last_incomplete_log_time = 0;
            uint64_t now = os_gettime_ns();
            if (now - last_incomplete_log_time >= 2000000000ULL) { // Throttle to every 2 seconds
                if (received <= 4) {
                    C64_LOG_DEBUG("Audio startup/control packets: " SSIZE_T_FORMAT
                                  " bytes (normal during initialization)",
                                  SSIZE_T_CAST(received));
                } else {
                    C64_LOG_WARNING("Received incomplete audio packet: " SSIZE_T_FORMAT " bytes (expected %d)",
                                    SSIZE_T_CAST(received), C64_AUDIO_PACKET_SIZE);
                }
                last_incomplete_log_time = now;
            }
            continue;
        }

        // Update timestamp for timeout detection - UDP packet received successfully
        uint64_t packet_time = os_gettime_ns();
        context->last_udp_packet_time = packet_time; // DEPRECATED - kept for compatibility
        context->last_audio_packet_time = packet_time;

        // Update audio statistics
        os_atomic_set_long(&context->audio_packets_received, os_atomic_load_long(&context->audio_packets_received) + 1);
        os_atomic_set_long(&context->audio_bytes_received,
                           os_atomic_load_long(&context->audio_bytes_received) + (long)received);

        // Log network packet at UDP reception (conditional - no parsing overhead if disabled)
        c64_log_audio_packet_if_enabled(context, packet, received, packet_time);

        // Batch process audio statistics
        uint64_t audio_now = os_gettime_ns();
        c64_process_audio_statistics_batch(context, audio_now);

        // Push audio packet to network buffer for queuing and later processing in render thread
        if (context->network_buffer) {
            c64_network_buffer_push_audio(context->network_buffer, packet, received, audio_now);
        }
    }

    C64_LOG_DEBUG("Audio thread stopped for C64S source '%s'", obs_source_get_name(context->source));
    return NULL;
}

// Audio timestamp validation to prevent OBS TS_SMOOTHING_THRESHOLD warnings
static void validate_audio_timestamp_progression(struct c64_source *context, uint64_t current_timestamp)
{
    if (context->last_audio_timestamp_validation > 0) {
        int64_t timestamp_delta = (int64_t)(current_timestamp - context->last_audio_timestamp_validation);
        // Expected delta is ~4ms (4,000,000 ns), warn if significantly off
        if (timestamp_delta < 2000000 || timestamp_delta > 6000000) {
            C64_LOG_DEBUG("Audio timestamp jump detected [%s]: delta=%" PRId64 "ns (expected ~4000000ns)",
                          obs_source_get_name(context->source), timestamp_delta);
        }
    }
    context->last_audio_timestamp_validation = current_timestamp;
}

// Generate monotonic audio timestamps at exactly 4ms intervals with drift correction
static uint64_t generate_monotonic_audio_timestamp(struct c64_source *context)
{
    // Audio packets must have exactly 4ms intervals (192 samples at 48kHz = 4000000ns)
    // This creates synthetic timestamps with periodic drift correction

    uint64_t current_real_time = os_gettime_ns();

    // Initialize on first call using system time
    if (context->audio_base_time == 0) {
        context->audio_base_time = current_real_time;
        context->audio_packet_count = 0;
        C64_LOG_DEBUG("Audio synthetic timestamps initialized for source '%s': base=%" PRIu64,
                      obs_source_get_name(context->source), context->audio_base_time);
    }

    // Calculate current timestamp: base + (packet_count * 4ms)
    uint64_t synthetic_timestamp = context->audio_base_time + (context->audio_packet_count * 4000000ULL);
    context->audio_packet_count++;

    // Drift correction: periodically adjust base time to prevent excessive drift
    // Check every 250 packets (1 second of audio) to see if we're drifting too far
    if ((context->audio_packet_count % 250) == 0) {
        int64_t drift_ns = (int64_t)(synthetic_timestamp - current_real_time);
        const int64_t MAX_DRIFT_NS = 100000000LL; // 100ms maximum drift

        if (llabs(drift_ns) > MAX_DRIFT_NS) {
            // Adjust base time to reduce drift while maintaining monotonic progression
            int64_t adjustment = drift_ns / 2; // Correct half the drift gradually
            context->audio_base_time -= adjustment;
            synthetic_timestamp -= adjustment;

            C64_LOG_DEBUG("Audio drift correction [%s]: drift=%" PRId64 "ms, adjusted by %" PRId64 "ms",
                          obs_source_get_name(context->source), drift_ns / 1000000, adjustment / 1000000);
        }
    }

    // Debug logging every 100 packets to verify progression
    if ((context->audio_packet_count % 1000) == 0) {
        int64_t drift_ms = (int64_t)(synthetic_timestamp - current_real_time) / 1000000;
        C64_LOG_DEBUG("Audio synthetic TS [%s]: count=%" PRIu64 ", drift=%" PRId64 "ms",
                      obs_source_get_name(context->source), context->audio_packet_count - 1, drift_ms);
    }

    return synthetic_timestamp;
}

// Process audio packet and send to OBS for playback
void c64_process_audio_packet(struct c64_source *context, const uint8_t *audio_data, size_t data_size,
                              uint64_t timestamp_ns)
{
    if (!context || !audio_data || data_size < 2) {
        return;
    }

    // Generate synthetic audio timestamp for smooth monotonic progression
    // Coordinated with video timing base resets to maintain A/V sync during buffer changes
    uint64_t audio_timestamp = generate_monotonic_audio_timestamp(context);

    // Skip the 2-byte sequence number header to get to audio samples
    const uint8_t *samples = audio_data + 2;
    size_t samples_size = data_size - 2;

    // According to C64 Ultimate spec: 192 stereo samples per packet, 16-bit signed little-endian
    // Each stereo sample = 4 bytes (2 bytes left + 2 bytes right)
    if (samples_size < 768) { // 192 * 4 = 768 bytes
        C64_LOG_WARNING("Audio packet too small: %zu bytes (expected 768)", samples_size);
        return;
    }

    // Validate timestamp progression for debugging
    validate_audio_timestamp_progression(context, audio_timestamp);

    // Set up OBS audio data structure - optimized for minimal latency
    struct obs_source_audio audio_output = {0};
    audio_output.frames = 192;            // 192 stereo samples per packet (4ms at C64 rate)
    audio_output.samples_per_sec = 47976; // Exact C64 Ultimate sample rate to avoid resampling
    audio_output.format = AUDIO_FORMAT_16BIT;
    audio_output.speakers = SPEAKERS_STEREO;
    audio_output.timestamp = audio_timestamp; // Use synthetic timestamp for smooth playback

    // Point to the audio data (OBS expects planar format, but we have interleaved)
    // For now, send interleaved data directly - OBS can handle it
    audio_output.data[0] = (uint8_t *)samples;

    // Send audio to OBS for playback
    obs_source_output_audio(context->source, &audio_output);

    // Log audio delivery to CSV if enabled (high-level event: audio samples delivered to OBS)
    if (context->timing_file) {
        uint64_t calculated_timestamp_ms = audio_timestamp / 1000000; // Convert ns to ms
        uint64_t actual_timestamp_ms = os_gettime_ns() / 1000000;
        c64_obs_log_audio_event(context, calculated_timestamp_ms, actual_timestamp_ms, samples_size);
    }

    // Very rare spot checks for audio timestamp debugging (every 10 minutes)
    static int audio_timestamp_debug_count = 0;
    static uint64_t last_audio_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++audio_timestamp_debug_count % 50000) == 0 ||
        (now - last_audio_log_time >= 600000000000ULL)) { // Every 50k packets OR 10 minutes
        uint64_t delivery_delay = now - audio_timestamp;
        C64_LOG_DEBUG("🎵 AUDIO SPOT CHECK: audio_ts=%" PRIu64 ", packet_ts=%" PRIu64 ", delivery_delay=%" PRIu64
                      "ms (processed: %d)",
                      audio_timestamp, timestamp_ns, delivery_delay / 1000000, audio_timestamp_debug_count);
        last_audio_log_time = now;
    }

    // Also record to file if recording is enabled (use processed samples, not raw packet)
    c64_record_audio_data(context, samples, samples_size);
}
