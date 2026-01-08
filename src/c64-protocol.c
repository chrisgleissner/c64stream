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
#include "c64-types.h"
#include "c64-video.h"
#include "c64-record-network.h"

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

        // Create IP:PORT string for the destination
        char ip_port_str[128]; // Larger buffer to avoid truncation warnings
        uint32_t port = (stream_id == 0) ? context->video_port : context->audio_port;
        snprintf(ip_port_str, sizeof(ip_port_str), "%s:%u", client_ip, port);
        size_t ip_port_len = strlen(ip_port_str);

        // Enable stream command with destination IP:PORT
        // Command structure: <command word LE> <param length LE> <duration LE> <IP:PORT string>
        // According to docs: FF2n where n is stream ID (0=video, 1=audio)
        uint8_t cmd[140];          // Large enough buffer for IP:PORT string (128 + header bytes)
        cmd[0] = 0x20 + stream_id; // 0x20 for video (stream 0), 0x21 for audio (stream 1)
        cmd[1] = 0xFF;
        cmd[2] = (uint8_t)(2 + ip_port_len); // Parameter length: 2 bytes duration + IP:PORT string length
        cmd[3] = 0x00;
        cmd[4] = 0x00; // Duration: 0 = forever (little endian)
        cmd[5] = 0x00;
        memcpy(&cmd[6], ip_port_str, ip_port_len);

        int cmd_len = 6 + (int)ip_port_len;
        C64_LOG_INFO("" NETWORK_LOG_PREFIX " Sending start command for stream %u to %s with client destination: %s",
                     stream_id, context->ip_address, ip_port_str);

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

    // Avoid unused parameter warning (timestamp_ns reserved for future jitter calculation)
    (void)timestamp_ns;

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
    if (context->csv_debug_enabled && data_payload > 0) {
        const uint8_t *payload = packet + C64_VIDEO_HEADER_SIZE;

        // Sampling-based check (avoid scanning full payload; this code runs on the UDP receive path).
        // Full-frame pops are all-white frames, so even sparse sampling is very likely to detect non-white.
        static const size_t sample_points[] = {0, 1, 7, 15, 31, 63, 127, 255};
        const size_t n = sizeof(sample_points) / sizeof(sample_points[0]);
        is_all_white = true;
        for (size_t i = 0; i < n; i++) {
            const size_t idx = (sample_points[i] < data_payload) ? sample_points[i] : (data_payload - 1);
            if (payload[idx] != 0xFF) {
                is_all_white = false;
                break;
            }
        }
    }

    c64_network_log_video_packet(context, seq_num, frame_num, line_num, is_last_packet, packet_size, data_payload,
                                 jitter_us, is_all_white);
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

    // Avoid unused parameter warning (timestamp_ns reserved for future jitter calculation)
    (void)timestamp_ns;

    // Parse audio packet header (only when logging is enabled)
    uint16_t seq_num = *(uint16_t *)(packet + 0);

    // Calculate jitter (simplified - would need timing reference for real jitter)
    int64_t jitter_us = 0;       // Placeholder for now
    uint16_t sample_count = 192; // C64 Ultimate spec: 192 stereo samples per packet

    bool has_signal = false;
    if (context->csv_debug_enabled && packet_size > 2) {
        const uint8_t *samples = packet + 2;
        size_t samples_size = packet_size - 2;
        const int16_t *samples_i16 = (const int16_t *)samples;
        size_t count_i16 = samples_size / sizeof(int16_t);

        // Sampling-based check to keep receive path cheap.
        // A real pop should have many non-zero samples, so sampling is sufficient.
        const size_t samples_to_check = (count_i16 < 32) ? count_i16 : 32;
        for (size_t i = 0; i < samples_to_check; i++) {
            const size_t idx = (count_i16 <= samples_to_check) ? i : (i * (count_i16 - 1)) / (samples_to_check - 1);
            if (samples_i16[idx] != 0) {
                has_signal = true;
                break;
            }
        }
    }

    c64_network_log_audio_packet(context, seq_num, packet_size, sample_count, jitter_us, has_signal);
}
