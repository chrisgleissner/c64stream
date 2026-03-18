/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include <obs-module.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "c64-network.h"
#include <util/platform.h>
#include "c64-logging.h"
#include "c64-protocol.h"
#include "c64-source.h"
#include "c64-av-sync.h"
#include "c64-types.h"
#include "c64-video.h"
#include "c64-record-network.h"

/* ============================================================================
 * Hot packet-level pop marker detection (debug/testing)
 * ============================================================================
 *
 * Used for "full-frame-pop" scenarios where the packet payload itself contains
 * deterministic pop markers.
 *
 * Rules:
 * - O(1)
 * - portable C only
 */

static inline bool c64_udp_packet_pop_fast(const uint8_t *payload_768)
{
    if (!payload_768) {
        return false;
    }

    // Robust full-frame pop detection for debug/testing.
    //
    // The synthetic generator's --full-frame-pop fills the payload with 0x11
    // (VIC color index 1 in both nibbles). The real-device A/V sync generator
    // also produces a solid white flash. A single-pixel probe is too fragile
    // because normal frames contain UI elements and borders.
    //
    // We sample a handful of pixels across the 4 lines in this packet and
    // declare a pop only when almost all samples are VIC white.

    // Helper: read one 4-bit pixel from packed 4bpp payload (low nibble first).
    // Layout is 4 lines × 384 pixels = 768 bytes.
    const uint16_t xs[2] = {64, 320};
    size_t white = 0;
    size_t samples = 0;

    for (uint16_t line = 0; line < 4; line++) {
        const size_t base = (size_t)line * (size_t)C64_BYTES_PER_LINE;
        for (size_t i = 0; i < (sizeof(xs) / sizeof(xs[0])); i++) {
            const uint16_t x = xs[i];
            const size_t byte_index = base + (size_t)(x >> 1);
            const uint8_t b = payload_768[byte_index];
            const uint8_t pix = (x & 1u) ? ((b >> 4u) & 0x0Fu) : (b & 0x0Fu);

            samples++;
            if (pix == 1u) {
                white++;
            }
        }
    }

    // Require at least 7/8 samples to be white.
    return (samples != 0) && (white + 1 >= samples);
}

static inline int16_t c64_load_le_i16_unaligned(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline bool c64_audio_pop_fast_i16_le(const uint8_t *samples, size_t samples_size)
{
    if (!samples || samples_size < 4) {
        return false;
    }

    // Robust O(1) pop detection for debug/testing:
    // - Audio pops are short, high-energy bursts (noise / tone).
    // - Sampling only a couple points can miss the peak (especially for noise).
    //
    // We sample a small, fixed set of samples across the packet and declare a
    // pop when the max absolute amplitude crosses a conservative threshold.

    const size_t n = samples_size / 2; // number of int16 samples (interleaved stereo)
    if (n < 32) {
        return false;
    }

    const size_t probe_count = 16;
    const size_t step = n / probe_count;
    if (step == 0) {
        return false;
    }

    int32_t max_abs = 0;
    for (size_t i = 0; i < probe_count; i++) {
        const size_t idx = (i * step) * 2;
        if (idx + 1 >= samples_size) {
            break;
        }

        const int16_t s = c64_load_le_i16_unaligned(samples + idx);
        const int32_t a = (s < 0) ? -(int32_t)s : (int32_t)s;
        if (a > max_abs) {
            max_abs = a;
        }
    }

    return max_abs >= 8000;
}

void c64_send_control_command(struct c64_source *context, bool enable, uint8_t stream_id)
{
    if (strcmp(context->ip_address, "0.0.0.0") == 0) {
        C64_LOG_DEBUG("" NETWORK_LOG_PREFIX " Skipping control command - no IP configured (0.0.0.0)");
        return;
    }

    if (enable) {
        // Get the OBS IP to send as destination
        const char *client_ip = context->obs_ip_address;

        // Ensure we have a valid OBS IP address
        if (!client_ip || strlen(client_ip) == 0) {
            C64_LOG_WARNING("" NETWORK_LOG_PREFIX " No OBS IP address configured, cannot send stream start command");
            return;
        }

        // Destination string for the control protocol.
        // In practice, the C64U expects/accepts "IP:PORT" and will stream to that UDP port.
        // This must match the local UDP ports we bind for the selected stream.
        const uint16_t client_port = (stream_id == 0) ? context->video_port : context->audio_port;

        char dest_str[64];
        size_t dest_len = 0;
        {
            const size_t ip_len = strlen(client_ip);
            const size_t max_port_chars = 5; // "65535"

            // Ensure there is room for: <ip> ':' <port> '\0'
            if (ip_len == 0 || ip_len > (sizeof(dest_str) - (1 + max_port_chars + 1))) {
                C64_LOG_ERROR("" NETWORK_LOG_PREFIX " OBS IP address too long for control destination string: '%s'",
                              client_ip);
                return;
            }

            memcpy(dest_str, client_ip, ip_len);
            dest_len = ip_len;
            dest_str[dest_len++] = ':';

            const int n = snprintf(dest_str + dest_len, sizeof(dest_str) - dest_len, "%u", (unsigned)client_port);
            if (n < 0 || (size_t)n >= (sizeof(dest_str) - dest_len)) {
                C64_LOG_ERROR("" NETWORK_LOG_PREFIX " Failed to format control destination port: %u",
                              (unsigned)client_port);
                return;
            }

            dest_len += (size_t)n;
        }

        // The C64U may keep its streaming state/destination unless the stream is explicitly stopped first.
        // Send stop right before enabling.
        // IMPORTANT: send stop and start on separate TCP connections to avoid coalescing both commands into
        // a single TCP read on the receiver side (our synthetic E2E mock server reads only one command).
        {
            socket_t stop_sock = c64_create_tcp_socket(context->ip_address, context->control_port);
            if (stop_sock != INVALID_SOCKET_VALUE) {
                uint8_t stop_cmd[4];
                stop_cmd[0] = 0x30 + stream_id; // FF3n (disable stream)
                stop_cmd[1] = 0xFF;
                stop_cmd[2] = 0x00; // No parameters
                stop_cmd[3] = 0x00;
                (void)send(stop_sock, (const char *)stop_cmd, (int)sizeof(stop_cmd), 0);
                close(stop_sock);
            }
        }

        socket_t sock = c64_create_tcp_socket(context->ip_address, context->control_port);
        if (sock == INVALID_SOCKET_VALUE) {
            return; // Error already logged in c64_create_tcp_socket
        }

        // Enable stream command with destination string
        // Command structure: <command word LE> <param length LE> <duration LE> <destination string>
        // According to docs: FF2n where n is stream ID (0=video, 1=audio)
        uint8_t cmd[140];          // Large enough buffer for destination string + header bytes
        cmd[0] = 0x20 + stream_id; // 0x20 for video (stream 0), 0x21 for audio (stream 1)
        cmd[1] = 0xFF;
        cmd[2] = (uint8_t)(2 + dest_len); // Parameter length: 2 bytes duration + destination string length
        cmd[3] = 0x00;
        cmd[4] = 0x00; // Duration: 0 = forever (little endian)
        cmd[5] = 0x00;
        memcpy(&cmd[6], dest_str, dest_len);

        int cmd_len = 6 + (int)dest_len;
        C64_LOG_INFO("" NETWORK_LOG_PREFIX " Sending start command for stream %u to %s with client destination: %s",
                     stream_id, context->ip_address, dest_str);

        ssize_t sent = send(sock, (const char *)cmd, cmd_len, 0);
        if (sent != (ssize_t)cmd_len) {
            int error = c64_get_socket_error();
            C64_LOG_ERROR("" NETWORK_LOG_PREFIX " Failed to send start control command: %s",
                          c64_get_socket_error_string(error));
        } else {
            C64_LOG_DEBUG("" NETWORK_LOG_PREFIX " Start control command sent successfully");
        }

        close(sock);
    } else {
        socket_t sock = c64_create_tcp_socket(context->ip_address, context->control_port);
        if (sock == INVALID_SOCKET_VALUE) {
            return; // Error already logged in c64_create_tcp_socket
        }

        // Disable stream command: FF3n where n is stream ID
        uint8_t cmd[4];
        cmd[0] = 0x30 + stream_id; // FF3n (disable stream)
        cmd[1] = 0xFF;
        cmd[2] = 0x00; // No parameters
        cmd[3] = 0x00;
        int cmd_len = 4;
        C64_LOG_INFO("" NETWORK_LOG_PREFIX " Sending stop command for stream %u to C64 %s", stream_id,
                     context->ip_address);

        ssize_t sent = send(sock, (const char *)cmd, cmd_len, 0);
        if (sent != (ssize_t)cmd_len) {
            int error = c64_get_socket_error();
            C64_LOG_ERROR("" NETWORK_LOG_PREFIX " Failed to send stop control command: %s",
                          c64_get_socket_error_string(error));
        } else {
            C64_LOG_DEBUG("" NETWORK_LOG_PREFIX " Stop control command sent successfully");
        }

        close(sock);
    }
}

/**
 * Parse and log video packet at UDP reception (conditional execution)
 * Only performs parsing and logging if network recording is enabled
 * @param context Source context (checked for network_file != NULL)
 * @param packet Raw UDP packet data
 * @param packet_size Size of received packet
 * @param timestamp_ns Nanosecond timestamp when packet was received
 */
void c64_log_video_packet_if_enabled(struct c64_source *context, const uint8_t *packet, size_t packet_size,
                                     uint64_t timestamp_ns)
{
    const bool do_csv_log = (context && context->network_file);
    const bool do_debug_pop_detect = c64_debug_logging;

    // Early return if we have nothing to do.
    if (!do_csv_log && !do_debug_pop_detect) {
        return;
    }

    // Parse video packet header (needed for CSV logging and for network-origin A/V sync correlation).
    const uint16_t seq_num = *(uint16_t *)(packet + 0);
    const uint16_t frame_num = *(uint16_t *)(packet + 2);
    uint16_t line_num = *(uint16_t *)(packet + 4);

    bool is_last_packet = (line_num & 0x8000) != 0;
    line_num &= 0x7FFF; // Remove last packet flag

    // Calculate jitter (simplified - would need timing reference for real jitter)
    int64_t jitter_us = 0; // Placeholder for now
    size_t data_payload = packet_size - C64_VIDEO_HEADER_SIZE;

    bool is_all_white = false;
    if (do_debug_pop_detect) {
        const uint8_t *payload = packet + C64_VIDEO_HEADER_SIZE;
        if (data_payload >= 768) {
            is_all_white = c64_udp_packet_pop_fast(payload);
        }

        // Emit A/V sync events from packet-level marker (rising edge only).
        const bool was_marker = context->av_sync_last_video_pop_marker;
        context->av_sync_last_video_pop_marker = is_all_white;
        if (!was_marker && is_all_white) {
            c64_av_sync_on_video_pop(context, C64_AV_SYNC_ORIGIN_NETWORK, frame_num, timestamp_ns);
        }
    }

    if (do_csv_log) {
        c64_network_log_video_packet(context, seq_num, frame_num, line_num, is_last_packet, packet_size, data_payload,
                                     jitter_us, is_all_white, timestamp_ns);
    }
}

/**
 * Parse and log audio packet at UDP reception (conditional execution)
 * Only performs parsing and logging if network recording is enabled
 * @param context Source context (checked for network_file != NULL)
 * @param packet Raw UDP packet data
 * @param packet_size Size of received packet
 * @param timestamp_ns Nanosecond timestamp when packet was received
 */
void c64_log_audio_packet_if_enabled(struct c64_source *context, const uint8_t *packet, size_t packet_size,
                                     uint64_t timestamp_ns)
{
    const bool do_csv_log = (context && context->network_file);
    const bool do_debug_pop_detect = c64_debug_logging;
    const bool do_av_sync_track = (context && context->record_av_sync);

    // Early return if we have nothing to do.
    if (!do_csv_log && !do_debug_pop_detect && !do_av_sync_track) {
        return;
    }

    // Parse audio packet header (only needed for CSV logging)
    const uint16_t seq_num = *(uint16_t *)(packet + 0);

    // Calculate jitter (simplified - would need timing reference for real jitter)
    int64_t jitter_us = 0;       // Placeholder for now
    uint16_t sample_count = 192; // C64 Ultimate spec: 192 stereo samples per packet

    bool has_signal = false;
    if ((do_debug_pop_detect || do_av_sync_track) && packet_size > 2) {
        const uint8_t *samples = packet + 2;
        size_t samples_size = packet_size - 2;

        has_signal = c64_audio_pop_fast_i16_le(samples, samples_size);

        const bool was_marker = context->av_sync_last_audio_pop_marker;
        context->av_sync_last_audio_pop_marker = has_signal;
        if (!was_marker && has_signal) {
            c64_av_sync_on_audio_pop(context, C64_AV_SYNC_ORIGIN_NETWORK, timestamp_ns);
        }
    }

    if (do_csv_log) {
        c64_network_log_audio_packet(context, seq_num, packet_size, sample_count, jitter_us, has_signal, timestamp_ns);
    }
}
