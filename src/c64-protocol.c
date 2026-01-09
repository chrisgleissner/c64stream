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
#include "c64-network.h" // Include network header first to avoid Windows header conflicts

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

    // Pixel index 768 -> byte 384, high nibble (VIC index 0 == black)
    return (payload_768[384] & 0xF0u) != 0u;
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

    const size_t n = samples_size / 2;
    if (n <= 10) {
        return false;
    }

    const size_t mid = n >> 1;

    const int16_t s0 = c64_load_le_i16_unaligned(samples + mid * 2);
    const int16_t s1 = c64_load_le_i16_unaligned(samples + (mid + 10) * 2);

    if (s0 >= 20000 || s0 <= -20000) {
        return true;
    }

    const int32_t d = (int32_t)s1 - (int32_t)s0;
    if (d >= 15000 || d <= -15000) {
        return true;
    }

    return false;
}

void c64_send_control_command(struct c64_source *context, bool enable, uint8_t stream_id)
{
    if (strcmp(context->ip_address, "0.0.0.0") == 0) {
        C64_LOG_DEBUG("" NETWORK_LOG_PREFIX " Skipping control command - no IP configured (0.0.0.0)");
        return;
    }

    socket_t sock = c64_create_tcp_socket(context->ip_address, context->control_port);
    if (sock == INVALID_SOCKET_VALUE) {
        return; // Error already logged in c64_create_tcp_socket
    }

    if (enable) {
        // Get the OBS IP to send as destination
        const char *client_ip = context->obs_ip_address;

        // Ensure we have a valid OBS IP address
        if (!client_ip || strlen(client_ip) == 0) {
            C64_LOG_WARNING("" NETWORK_LOG_PREFIX " No OBS IP address configured, cannot send stream start command");
            close(sock);
            return;
        }

        // Destination string for the control protocol.
        // Per spec/examples, this is an IP address string (port is implied by the stream).
        // Some firmwares may tolerate "IP:PORT", but sticking to IP-only is the most compatible.
        char dest_str[64];
        snprintf(dest_str, sizeof(dest_str), "%s", client_ip);
        size_t dest_len = strlen(dest_str);

        // The C64U may keep its streaming state/destination unless the stream is
        // explicitly stopped first. Make the start command idempotent by sending a stop command
        // for the same stream ID right before enabling it.
        {
            uint8_t stop_cmd[4];
            stop_cmd[0] = 0x30 + stream_id; // 0x30 for video (stream 0), 0x31 for audio (stream 1)
            stop_cmd[1] = 0xFF;
            stop_cmd[2] = 0x00; // No parameters
            stop_cmd[3] = 0x00;

            (void)send(sock, (const char *)stop_cmd, (int)sizeof(stop_cmd), 0);
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
    } else {
        // Disable stream command: FF3n where n is stream ID
        uint8_t cmd[4];
        cmd[0] = 0x30 + stream_id; // 0x30 for video (stream 0), 0x31 for audio (stream 1)
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
    }

    close(sock);
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
    // Early return if network logging is disabled (no performance impact)
    if (!context->network_file) {
        return;
    }

    // Parse video packet header (only when logging is enabled)
    uint16_t seq_num = *(uint16_t *)(packet + 0);
    uint16_t frame_num = *(uint16_t *)(packet + 2);
    uint16_t line_num = *(uint16_t *)(packet + 4);

    bool is_last_packet = (line_num & 0x8000) != 0;
    line_num &= 0x7FFF; // Remove last packet flag

    // Calculate jitter (simplified - would need timing reference for real jitter)
    int64_t jitter_us = 0; // Placeholder for now
    size_t data_payload = packet_size - C64_VIDEO_HEADER_SIZE;

    bool is_all_white = false;
    if (c64_debug_logging) {
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

    c64_network_log_video_packet(context, seq_num, frame_num, line_num, is_last_packet, packet_size, data_payload,
                                 jitter_us, is_all_white, timestamp_ns);
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
    // Early return if network logging is disabled (no performance impact)
    if (!context->network_file) {
        return;
    }

    // Parse audio packet header (only when logging is enabled)
    uint16_t seq_num = *(uint16_t *)(packet + 0);

    // Calculate jitter (simplified - would need timing reference for real jitter)
    int64_t jitter_us = 0;       // Placeholder for now
    uint16_t sample_count = 192; // C64 Ultimate spec: 192 stereo samples per packet

    bool has_signal = false;
    if (c64_debug_logging && packet_size > 2) {
        const uint8_t *samples = packet + 2;
        size_t samples_size = packet_size - 2;

        has_signal = c64_audio_pop_fast_i16_le(samples, samples_size);

        const bool was_marker = context->av_sync_last_audio_pop_marker;
        context->av_sync_last_audio_pop_marker = has_signal;
        if (!was_marker && has_signal) {
            // Mirror c64_process_audio_packet behavior: ignore the very first audio marker before any video pop.
            const struct c64_av_sync_state *state = &context->av_sync[C64_AV_SYNC_ORIGIN_NETWORK];
            if (state->video_pop_count != 0 || state->audio_pop_count != 0) {
                c64_av_sync_on_audio_pop(context, C64_AV_SYNC_ORIGIN_NETWORK, timestamp_ns);
            }
        }
    }

    c64_network_log_audio_packet(context, seq_num, packet_size, sample_count, jitter_us, has_signal, timestamp_ns);
}
