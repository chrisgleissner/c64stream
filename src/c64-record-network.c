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
void c64_network_write_header(struct c64_source *context)
{
    if (!context || !context->network_file) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Cannot write network CSV header: context or network file is NULL");
        return;
    }

    // Initialize shared timing base to 0 - will be set on first actual event (network or OBS)
    context->csv_timing_base_ns = 0;

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

    // Set shared timing base on first event (network or OBS) to ensure elapsed_us starts at 0
    if (context->csv_timing_base_ns == 0) {
        context->csv_timing_base_ns = current_ns;
    }

    uint64_t elapsed_us = (current_ns - context->csv_timing_base_ns) / 1000;

    // Calculate packet interval from last video packet
    static uint64_t last_video_packet_us = 0;
    uint64_t packet_interval_us = (last_video_packet_us > 0) ? (elapsed_us - last_video_packet_us) : 0;
    last_video_packet_us = elapsed_us;

    // Load atomic counters for network statistics
    uint64_t video_packets = (uint64_t)os_atomic_load_long(&context->video_packets_received);
    uint64_t audio_packets = (uint64_t)os_atomic_load_long(&context->audio_packets_received);
    uint64_t sequence_errors = (uint64_t)os_atomic_load_long(&context->video_sequence_errors);

    // Write video packet event to CSV
    fprintf(context->network_file, "video,%llu,%u,%u,%u,%d,%zu,%zu,%lld,%llu,%llu,%llu,%llu",
            (unsigned long long)elapsed_us, sequence_num, frame_num, line_num, is_last_packet ? 1 : 0, packet_size,
            data_payload, (long long)jitter_us, (unsigned long long)packet_interval_us,
            (unsigned long long)video_packets, (unsigned long long)audio_packets, (unsigned long long)sequence_errors);
    if (context->csv_debug_enabled) {
        fprintf(context->network_file, ",%d,0", is_all_white ? 1 : 0);
    }
    fprintf(context->network_file, "\n");

    // Flush every 50 packets to balance performance vs real-time analysis
    static int flush_counter = 0;
    if (++flush_counter >= 50) {
        fflush(context->network_file);
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

    // Set shared timing base on first event (network or OBS) to ensure elapsed_us starts at 0
    if (context->csv_timing_base_ns == 0) {
        context->csv_timing_base_ns = current_ns;
    }

    uint64_t elapsed_us = (current_ns - context->csv_timing_base_ns) / 1000;

    // Calculate packet interval from last audio packet
    static uint64_t last_audio_packet_us = 0;
    uint64_t packet_interval_us = (last_audio_packet_us > 0) ? (elapsed_us - last_audio_packet_us) : 0;
    last_audio_packet_us = elapsed_us;

    // Load atomic counters for network statistics
    uint64_t video_packets = (uint64_t)os_atomic_load_long(&context->video_packets_received);
    uint64_t audio_packets = (uint64_t)os_atomic_load_long(&context->audio_packets_received);
    uint64_t sequence_errors = (uint64_t)os_atomic_load_long(&context->video_sequence_errors);

    // Write audio packet event to CSV (use 0 for video-specific fields)
    fprintf(context->network_file, "audio,%llu,%u,0,0,0,%zu,%u,%lld,%llu,%llu,%llu,%llu",
            (unsigned long long)elapsed_us, sequence_num, packet_size, sample_count, (long long)jitter_us,
            (unsigned long long)packet_interval_us, (unsigned long long)video_packets,
            (unsigned long long)audio_packets, (unsigned long long)sequence_errors);
    if (context->csv_debug_enabled) {
        fprintf(context->network_file, ",0,%d", has_signal ? 1 : 0);
    }
    fprintf(context->network_file, "\n");

    // Flush every 25 packets for audio (lower frequency than video)
    static int flush_counter = 0;
    if (++flush_counter >= 25) {
        fflush(context->network_file);
        flush_counter = 0;
    }
}
