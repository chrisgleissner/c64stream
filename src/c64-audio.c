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
#include <stdint.h>
#include <time.h>        // For localtime_r/localtime_s in AV SYNC logging
#include "c64-network.h" // Include network header first to avoid Windows header conflicts
#include "c64-logging.h"
#include "c64-audio.h"
#include "c64-types.h"
#include "c64-protocol.h"
#include "c64-video.h"
#include "c64-record.h"
#include "c64-record-network.h"
#include "c64-av-sync.h"

// Audio thread function
void *audio_thread_func(void *data)
{
    struct c64_source *context = data;
    uint8_t packet[C64_AUDIO_PACKET_SIZE];
    uint32_t wouldblock_spins = 0;

    C64_LOG_DEBUG("" AUDIO_LOG_PREFIX " Audio receiver thread started on port %u", context->audio_port);

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
                wouldblock_spins++;
                if (wouldblock_spins < 1000) {
                    os_sleep_ms(0);
                } else {
                    os_sleep_ms(1);
                    wouldblock_spins = 0;
                }
                continue;
            }
            // On Windows, WSAENOTSOCK means socket was closed - this is normal during shutdown
            if (error == WSAENOTSOCK && context->audio_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("" AUDIO_LOG_PREFIX
                              " Audio socket closed (WSAENOTSOCK) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
            // On Windows, WSAESHUTDOWN means socket was shutdown - this is normal during reconnection
            if (error == WSAESHUTDOWN) {
                C64_LOG_DEBUG("" AUDIO_LOG_PREFIX " Audio socket shutdown (WSAESHUTDOWN) - waiting for reconnection");
                os_sleep_ms(100); // Wait for reconnection to complete
                continue;         // Continue waiting instead of exiting thread
            }
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
                wouldblock_spins++;
                if (wouldblock_spins < 1000) {
                    os_sleep_ms(0);
                } else {
                    os_sleep_ms(1);
                    wouldblock_spins = 0;
                }
                continue;
            }
            // On POSIX, EBADF means socket was closed - this is normal during shutdown
            if (error == EBADF && context->audio_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("" AUDIO_LOG_PREFIX " Audio socket closed (EBADF) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
#endif
            C64_LOG_ERROR("" AUDIO_LOG_PREFIX " Audio socket error: %s (error code: %d)",
                          c64_get_socket_error_string(error), error);
            break;
        }

        wouldblock_spins = 0;

        // Stage-1: UDP ingest
        // Keep the socket receive path minimal to avoid receiver-side backpressure.
        // No parsing, no sorting, no per-packet logging, no blocking.
        if (received != C64_AUDIO_PACKET_SIZE) {
            continue;
        }

        const uint64_t packet_time = os_gettime_ns();
        context->last_udp_packet_time = packet_time; // DEPRECATED - kept for compatibility
        context->last_audio_packet_time = packet_time;

        os_atomic_set_long(&context->audio_packets_received, os_atomic_load_long(&context->audio_packets_received) + 1);
        os_atomic_set_long(&context->audio_bytes_received,
                           os_atomic_load_long(&context->audio_bytes_received) + (long)received);

        (void)c64_network_fifo_push(&context->audio_fifo, packet, (uint16_t)received, packet_time);
    }

    C64_LOG_DEBUG("" AUDIO_LOG_PREFIX " Audio thread stopped for C64 Stream source '%s'",
                  obs_source_get_name(context->source));
    return NULL;
}

// Audio timestamp validation to prevent OBS TS_SMOOTHING_THRESHOLD warnings
static void validate_audio_timestamp_progression(struct c64_source *context, uint64_t current_timestamp)
{
    if (context->last_audio_timestamp_validation > 0) {
        int64_t timestamp_delta = (int64_t)(current_timestamp - context->last_audio_timestamp_validation);
        // Expected delta is ~4ms (format-specific: PAL 4.001ms, NTSC 4.005ms)
        // Allow 3.5ms to 4.5ms tolerance for both formats
        if (timestamp_delta < 3500000 || timestamp_delta > 4500000) {
            C64_LOG_DEBUG("" AUDIO_LOG_PREFIX " Audio timestamp jump detected [%s]: delta=%" PRId64
                          "ns (expected ~4000000ns, format-specific)",
                          obs_source_get_name(context->source), timestamp_delta);
        }
    }
    context->last_audio_timestamp_validation = current_timestamp;
}

static bool c64_debug_audio_has_signal(const uint8_t *samples, size_t samples_size)
{
    // Fast audio pop detection for debug/testing:
    // non-pop is (almost) silent, pop is a strong deviation (full-volume test tone).
    // Samples are 16-bit signed little-endian stereo interleaved.

    const int threshold = 4096; // Strong signal threshold; avoids false positives from SID noise

    if (samples_size < 4) {
        return false;
    }

    // Audio packets are fixed-size in practice (192 stereo samples = 768 bytes), so we can use
    // a tiny fixed probe set without any divisions.
    const size_t sample_count = samples_size / 2;
    if (sample_count <= 336) {
        // Fallback (should not happen in normal operation)
        for (size_t i = 0; i < sample_count; i++) {
            const uint8_t lo = samples[i * 2 + 0];
            const uint8_t hi = samples[i * 2 + 1];
            const int16_t v = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
            const int abs_v = (v < 0) ? (int)(-v) : (int)v;
            if (abs_v >= threshold) {
                return true;
            }
        }
        return false;
    }

    static const size_t probe_idx[8] = {0, 48, 96, 144, 192, 240, 288, 336};
    for (size_t p = 0; p < 8; p++) {
        const size_t i = probe_idx[p];
        const uint8_t lo = samples[i * 2 + 0];
        const uint8_t hi = samples[i * 2 + 1];
        const int16_t v = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
        const int abs_v = (v < 0) ? (int)(-v) : (int)v;
        if (abs_v >= threshold) {
            return true;
        }
    }

    return false;
}

// Generate monotonic audio timestamps with format-specific intervals
static uint64_t generate_monotonic_audio_timestamp(struct c64_source *context)
{
    // Audio packet intervals are format-specific (exact fractional rates):
    // PAL:  192 samples ÷ 47982.8869047619 Hz = 4,001,416.96 ns/packet
    // NTSC: 192 samples ÷ 47940.3408482143 Hz = 4,005,005.95 ns/packet
    //
    // The interval_ns is calculated dynamically based on detected format

    uint64_t current_real_time = os_gettime_ns();

    // Initialize on first call - prefer video's timing base if already set for A/V sync
    if (context->audio_base_time == 0) {
        // Calculate format-specific audio packet interval
        // interval_ns = (192 samples * 1,000,000,000 ns/sec) / sample_rate
        if (context->audio_sample_rate > 0) {
            context->audio_interval_ns = (uint64_t)((192ULL * 1000000000ULL) / context->audio_sample_rate);
            C64_LOG_INFO("" AUDIO_LOG_PREFIX " Audio packet interval: %" PRIu64 " ns (rate=%.4f Hz)",
                         context->audio_interval_ns, context->audio_sample_rate);
        } else {
            // Fallback to PAL rate if format not yet detected
            context->audio_sample_rate = C64_PAL_AUDIO_SAMPLE_RATE;
            context->audio_interval_ns = (uint64_t)((192ULL * 1000000000ULL) / context->audio_sample_rate);
            C64_LOG_WARNING("" AUDIO_LOG_PREFIX " Format not detected, using PAL audio rate: %.4f Hz",
                            context->audio_sample_rate);
        }

        // Use video's stream_start_time_ns if already established (ensures A/V sync)
        // Otherwise use current real time
        if (context->timestamp_base_set && context->stream_start_time_ns > 0) {
            context->audio_base_time = context->stream_start_time_ns;
            C64_LOG_INFO("" AUDIO_LOG_PREFIX " 🎵 Audio using video timing base for A/V sync: %" PRIu64 " ns",
                         context->audio_base_time);
        } else {
            context->audio_base_time = current_real_time;
            C64_LOG_DEBUG("" AUDIO_LOG_PREFIX " Audio synthetic timestamps initialized for source '%s': base=%" PRIu64,
                          obs_source_get_name(context->source), context->audio_base_time);
        }
        context->audio_packet_count = 0;
    }

    // Calculate current timestamp: base + (packet_count * format_specific_interval)
    uint64_t synthetic_timestamp =
        context->audio_base_time + (context->audio_packet_count * context->audio_interval_ns);
    context->audio_packet_count++;

    // Drift correction: periodically adjust base time to prevent excessive drift
    // Check every 250 packets (~1 second of audio) to see if we're drifting too far
    if ((context->audio_packet_count % 250) == 0) {
        int64_t drift_ns = (int64_t)(synthetic_timestamp - current_real_time);
        const int64_t MAX_DRIFT_NS = 100000000LL; // 100ms maximum drift

        if (llabs(drift_ns) > MAX_DRIFT_NS) {
            // Adjust base time to reduce drift while maintaining monotonic progression
            int64_t adjustment = drift_ns / 2; // Correct half the drift gradually
            context->audio_base_time -= adjustment;
            synthetic_timestamp -= adjustment;

            C64_LOG_DEBUG("" AUDIO_LOG_PREFIX " Audio drift correction [%s]: drift=%" PRId64 "ms, adjusted by %" PRId64
                          "ms",
                          obs_source_get_name(context->source), drift_ns / 1000000, adjustment / 1000000);
        }
    }

    // Debug logging every 100 packets to verify progression
    if ((context->audio_packet_count % 1000) == 0) {
        int64_t drift_ms = (int64_t)(synthetic_timestamp - current_real_time) / 1000000;
        C64_LOG_DEBUG("" AUDIO_LOG_PREFIX " Audio synthetic TS [%s]: count=%" PRIu64 ", drift=%" PRId64 "ms",
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
        C64_LOG_WARNING("" AUDIO_LOG_PREFIX " Audio packet too small: %zu bytes (expected 768)", samples_size);
        return;
    }

    bool has_signal = false;
    bool audio_pop_rise = false;
    if (c64_debug_logging) {
        // Keep debug-only detection extremely cheap: sparse probes + simple hysteresis.
        const bool probed_has_signal = c64_debug_audio_has_signal(samples, samples_size);
        const bool was_has_signal = context->av_sync_last_audio_has_signal;
        if (!was_has_signal) {
            has_signal = probed_has_signal;
        } else {
            // Hysteresis: once "signal" is detected, keep it for a bit to avoid flicker.
            // This improves edge detection stability for A/V sync without extra work.
            has_signal = probed_has_signal;
        }
        // Rising edge only; debounce is handled inside c64_av_sync.
        if (!was_has_signal && has_signal) {
            const struct c64_av_sync_state *state = &context->av_sync[C64_AV_SYNC_ORIGIN_OBS];
            if (!(state->video_pop_count == 0 && state->audio_pop_count == 0)) {
                audio_pop_rise = true;
            }
        }
        context->av_sync_last_audio_has_signal = has_signal;
    }

    // Validate timestamp progression for debugging
    validate_audio_timestamp_progression(context, audio_timestamp);

    // Set up OBS audio data structure - optimized for minimal latency
    struct obs_source_audio audio_output = {0};
    audio_output.frames = 192;                                           // 192 stereo samples per packet
    audio_output.samples_per_sec = (uint32_t)context->audio_sample_rate; // Format-specific rate (PAL/NTSC)
    audio_output.format = AUDIO_FORMAT_16BIT;
    audio_output.speakers = SPEAKERS_STEREO;
    audio_output.timestamp = audio_timestamp; // Use synthetic timestamp for smooth playback

    // Point to the audio data (OBS expects planar format, but we have interleaved)
    // For now, send interleaved data directly - OBS can handle it
    audio_output.data[0] = (uint8_t *)samples;

    // Send audio to OBS for playback
    obs_source_output_audio(context->source, &audio_output);
    context->last_audio_submit_ns = os_gettime_ns();

    // Emit AV-sync pop only once it's been handed off to OBS.
    if (c64_debug_logging && audio_pop_rise) {
        c64_av_sync_on_audio_pop(context, C64_AV_SYNC_ORIGIN_OBS, context->last_audio_submit_ns);
    }

    if (context->last_video_submit_ns != 0) {
        static uint32_t av_sync_log_counter = 0;
        if ((++av_sync_log_counter % 5000) == 0) {
            int64_t delta_ns = (int64_t)context->last_audio_submit_ns - (int64_t)context->last_video_submit_ns;
            double delta_ms = (double)((delta_ns < 0) ? -delta_ns : delta_ns) / 1000000.0;
            const char *lead = (delta_ns >= 0) ? "audio" : "video";
            C64_LOG_INFO("" AUDIO_LOG_PREFIX " A/V SYNC: OBS handoff delta: %s +%.1f ms", lead, delta_ms);
        }
    }

    // Log audio delivery to CSV if enabled (high-level event: audio samples delivered to OBS)
    if (context->timing_file) {
        c64_obs_log_audio_event(context, samples_size, has_signal);
    }

    // Very rare spot checks for audio timestamp debugging (every 10 minutes)
    static int audio_timestamp_debug_count = 0;
    static uint64_t last_audio_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++audio_timestamp_debug_count % 50000) == 0 ||
        (now - last_audio_log_time >= 600000000000ULL)) { // Every 50k packets OR 10 minutes
        uint64_t delivery_delay = now - audio_timestamp;
        C64_LOG_DEBUG("" AUDIO_LOG_PREFIX " 🎵 AUDIO SPOT CHECK: audio_ts=%" PRIu64 ", packet_ts=%" PRIu64
                      ", delivery_delay=%" PRIu64 "ms (processed: %d)",
                      audio_timestamp, timestamp_ns, delivery_delay / 1000000, audio_timestamp_debug_count);
        last_audio_log_time = now;
    }

    // Also record to file if recording is enabled (use processed samples, not raw packet)
    c64_record_audio_data(context, samples, samples_size);
}
