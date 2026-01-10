/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "c64-logging.h"
#include "c64-types.h"
#include "c64-record.h"
#include "c64-record-network.h"

/**
 * Write network packet CSV header
 * Initializes the CSV file with column headers for network analysis
 * @param context Source context with valid network file handle
 */
// Reset static timing trackers (called when starting new recording session)
void c64_network_reset_timing(void)
{
    static uint64_t *last_video_ptr = NULL;
    static uint64_t *last_audio_ptr = NULL;

    // Store pointers to static variables on first call
    if (!last_video_ptr) {
        // Will be initialized by first call to log functions
        return;
    }

    *last_video_ptr = 0;
    *last_audio_ptr = 0;
}

void c64_network_write_header(struct c64_source *context)
{
    if (!context || !context->network_file) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Cannot write network CSV header: context or network file is NULL");
        return;
    }

    // csv_timing_base_ns is initialized when CSV recording starts so obs.csv and network.csv share one base.

    // Reset packet timing trackers for new recording session
    context->last_video_packet_us = 0;
    context->last_audio_packet_us = 0;

    // Write CSV header for network packet analysis
    fprintf(context->network_file,
            "packet_type,elapsed_us,sequence_num,frame_num,line_num,last_packet,packet_size,data_payload,jitter_us,"
            "packet_interval_us,total_video_packets,total_audio_packets,sequence_errors");
    if (context->csv_debug_enabled) {
        fprintf(context->network_file, ",is_all_white,has_signal");
    }
    fprintf(context->network_file, "\n");
    fflush(context->network_file);

    C64_LOG_INFO("" RECORD_LOG_PREFIX " Network packet CSV header written successfully");
}

/**
 * Log video packet reception event to network CSV
 * @param context Source context
 * @param sequence_num Video packet sequence number
 * @param frame_num Video frame number
 * @param line_num Video line number within frame
 * @param is_last_packet True if this is the last packet in the frame
 * @param packet_size Total size of received packet
 * @param data_payload Size of actual video data in packet
 * @param jitter_us Calculated jitter from expected timing (microseconds)
 */
void c64_network_log_video_packet(struct c64_source *context, uint16_t sequence_num, uint16_t frame_num,
                                  uint16_t line_num, bool is_last_packet, size_t packet_size, size_t data_payload,
                                  int64_t jitter_us, bool is_all_white, uint64_t packet_timestamp_ns)
{
    if (!context || !context->network_file) {
        return; // Silently ignore if network file not available
    }

    // Calculate elapsed microseconds since shared CSV timing started
    uint64_t current_ns = packet_timestamp_ns;

    uint64_t base_ns = context->csv_timing_base_ns;
    if (base_ns == 0) {
        base_ns = current_ns;
    }
    uint64_t elapsed_us = (current_ns - base_ns) / 1000;

    // Calculate packet interval from last video packet (stored in context, not static)
    uint64_t packet_interval_us = (context->last_video_packet_us > 0) ? (elapsed_us - context->last_video_packet_us)
                                                                      : 0;
    context->last_video_packet_us = elapsed_us;

    // Load atomic counters for network statistics
    uint64_t video_packets = (uint64_t)os_atomic_load_long(&context->video_packets_received);
    uint64_t audio_packets = (uint64_t)os_atomic_load_long(&context->audio_packets_received);
    uint64_t sequence_errors = (uint64_t)os_atomic_load_long(&context->video_sequence_errors);

    // Write video packet event to CSV - Optimized to use single fwrite to minimize locking overhead
    char log_buffer[512];
    int len = snprintf(log_buffer, sizeof(log_buffer), "video,%llu,%u,%u,%u,%d,%zu,%zu,%lld,%llu,%llu,%llu,%llu",
                       (unsigned long long)elapsed_us, sequence_num, frame_num, line_num, is_last_packet ? 1 : 0,
                       packet_size, data_payload, (long long)jitter_us, (unsigned long long)packet_interval_us,
                       (unsigned long long)video_packets, (unsigned long long)audio_packets,
                       (unsigned long long)sequence_errors);

    if (context->csv_debug_enabled && len < (int)sizeof(log_buffer)) {
        int ret = snprintf(log_buffer + len, sizeof(log_buffer) - len, ",%d,0", is_all_white ? 1 : 0);
        if (ret > 0)
            len += ret;
    }

    if (len < (int)sizeof(log_buffer)) {
        log_buffer[len++] = '\n';
    }

    // Single fwrite call is much faster than multiple fprintfs due to reduced locking/overhead
    fwrite(log_buffer, 1, len, context->network_file);

    // Flush trigger removed to improve performance - rely on OS buffering and fclose
    static int flush_counter = 0;
    if (++flush_counter >= 100000) { // Safety flush only, very rare
        // fflush(context->network_file); // Disabled completely for max speed
        flush_counter = 0;
    }
}

/**
 * Log audio packet reception event to network CSV
 * @param context Source context
 * @param sequence_num Audio packet sequence number
 * @param packet_size Total size of received packet
 * @param sample_count Number of audio samples in packet
 * @param jitter_us Calculated jitter from expected timing (microseconds)
 */
void c64_network_log_audio_packet(struct c64_source *context, uint16_t sequence_num, size_t packet_size,
                                  uint16_t sample_count, int64_t jitter_us, bool has_signal,
                                  uint64_t packet_timestamp_ns)
{
    if (!context || !context->network_file) {
        return; // Silently ignore if network file not available
    }

    // Calculate elapsed microseconds since shared CSV timing started
    uint64_t current_ns = packet_timestamp_ns;

    uint64_t base_ns = context->csv_timing_base_ns;
    if (base_ns == 0) {
        base_ns = current_ns;
    }
    uint64_t elapsed_us = (current_ns - base_ns) / 1000;

    // Calculate packet interval from last audio packet (stored in context, not static)
    uint64_t packet_interval_us = (context->last_audio_packet_us > 0) ? (elapsed_us - context->last_audio_packet_us)
                                                                      : 0;
    context->last_audio_packet_us = elapsed_us;

    // Load atomic counters for network statistics
    uint64_t video_packets = (uint64_t)os_atomic_load_long(&context->video_packets_received);
    uint64_t audio_packets = (uint64_t)os_atomic_load_long(&context->audio_packets_received);
    uint64_t sequence_errors = (uint64_t)os_atomic_load_long(&context->video_sequence_errors);

    // Write audio packet event to CSV (use 0 for video-specific fields).
    // IMPORTANT: keep this as a single stdio call to avoid interleaving with concurrent writers.
    char log_buffer[512];
    int len = snprintf(log_buffer, sizeof(log_buffer), "audio,%llu,%u,0,0,0,%zu,%u,%lld,%llu,%llu,%llu,%llu",
                       (unsigned long long)elapsed_us, sequence_num, packet_size, sample_count, (long long)jitter_us,
                       (unsigned long long)packet_interval_us, (unsigned long long)video_packets,
                       (unsigned long long)audio_packets, (unsigned long long)sequence_errors);

    if (context->csv_debug_enabled && len > 0 && len < (int)sizeof(log_buffer)) {
        int ret = snprintf(log_buffer + len, sizeof(log_buffer) - len, ",0,%d", has_signal ? 1 : 0);
        if (ret > 0) {
            len += ret;
        }
    }

    if (len > 0 && len < (int)sizeof(log_buffer)) {
        log_buffer[len++] = '\n';
    } else if (len <= 0) {
        return;
    } else {
        // Truncated; ensure the line terminator is present.
        log_buffer[sizeof(log_buffer) - 1] = '\n';
        len = (int)sizeof(log_buffer);
    }

    fwrite(log_buffer, 1, (size_t)len, context->network_file);

    // Flush removed for performance; rely on OS buffering and fclose.
}
