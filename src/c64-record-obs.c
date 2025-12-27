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
#include "c64-record-obs.h"

/**
 * Write OBS timing CSV header
 * Initializes the CSV file with column headers for timing analysis
 * @param context Source context with valid timing file handle
 */
void c64_obs_write_header(struct c64_source *context)
{
    if (!context || !context->timing_file) {
        C64_LOG_ERROR("Cannot write CSV header: context or timing file is NULL");
        return;
    }

    // Initialize shared timing base to 0 - will be set on first actual event (network or OBS)
    context->csv_timing_base_ns = 0;

    // Write CSV header with all timing columns
    fprintf(context->timing_file,
            "event_type,frame_num,elapsed_us,data_size_bytes,fps,audio_samples_total,video_packets_received,"
            "audio_packets_received,sequence_errors\n");
    fflush(context->timing_file);

    C64_LOG_INFO("OBS timing CSV header written successfully");
}

/**
 * Log video frame timing event to OBS CSV
 * @param context Source context
 * @param frame_num Logical stream-relative frame number
 * @param frame_size Size of frame data in bytes
 */
void c64_obs_log_video_event(struct c64_source *context, uint16_t frame_num, size_t frame_size)
{
    if (!context || !context->timing_file) {
        return; // Silently ignore if timing file not available
    }

    // Calculate elapsed microseconds since shared CSV timing started
    uint64_t current_ns = os_gettime_ns();

    // Set shared timing base on first event (network or OBS) to ensure elapsed_us starts at 0
    if (context->csv_timing_base_ns == 0) {
        context->csv_timing_base_ns = current_ns;
    }

    uint64_t elapsed_us = (current_ns - context->csv_timing_base_ns) / 1000;

    // Write video timing event to CSV with actual frame number
    uint64_t video_packets = (uint64_t)os_atomic_load_long(&context->video_packets_received);
    uint64_t audio_packets = (uint64_t)os_atomic_load_long(&context->audio_packets_received);
    uint64_t sequence_errors = (uint64_t)os_atomic_load_long(&context->video_sequence_errors);

    fprintf(context->timing_file, "video,%u,%llu,%zu,%.3f,%ld,%llu,%llu,%llu\n", frame_num,
            (unsigned long long)elapsed_us, frame_size, context->expected_fps,
            os_atomic_load_long(&context->recorded_audio_samples), (unsigned long long)video_packets,
            (unsigned long long)audio_packets, (unsigned long long)sequence_errors);

    // Flush immediately for real-time analysis
    fflush(context->timing_file);
}

/**
 * Log audio data timing event to OBS CSV
 * @param context Source context
 * @param data_size Size of audio data in bytes
 */
void c64_obs_log_audio_event(struct c64_source *context, size_t data_size)
{
    if (!context || !context->timing_file) {
        return; // Silently ignore if timing file not available
    }

    // Calculate elapsed microseconds since shared CSV timing started
    uint64_t current_ns = os_gettime_ns();

    // Set shared timing base on first event (network or OBS) to ensure elapsed_us starts at 0
    if (context->csv_timing_base_ns == 0) {
        context->csv_timing_base_ns = current_ns;
    }

    uint64_t elapsed_us = (current_ns - context->csv_timing_base_ns) / 1000;

    // Write audio timing event to CSV (frame_num = 0 since audio doesn't correspond to specific video frames)
    uint64_t video_packets = (uint64_t)os_atomic_load_long(&context->video_packets_received);
    uint64_t audio_packets = (uint64_t)os_atomic_load_long(&context->audio_packets_received);
    uint64_t sequence_errors = (uint64_t)os_atomic_load_long(&context->video_sequence_errors);

    fprintf(context->timing_file, "audio,0,%llu,%zu,%.3f,%ld,%llu,%llu,%llu\n", (unsigned long long)elapsed_us,
            data_size, context->expected_fps, os_atomic_load_long(&context->recorded_audio_samples),
            (unsigned long long)video_packets, (unsigned long long)audio_packets, (unsigned long long)sequence_errors);

    // Flush immediately for real-time analysis
    fflush(context->timing_file);
}
