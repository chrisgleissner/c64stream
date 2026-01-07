/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_RECORD_NETWORK_H
#define C64_RECORD_NETWORK_H

// Logging prefix for recording operations
#define RECORD_LOG_PREFIX "💾 RECORD:"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64_source;

// Network packet recording functions
void c64_network_write_header(struct c64_source *context);
void c64_network_log_video_packet(struct c64_source *context, uint16_t sequence_num, uint16_t frame_num,
                                  uint16_t line_num, bool is_last_packet, size_t packet_size, size_t data_payload,
                                  int64_t jitter_us, bool is_all_white, uint64_t packet_timestamp_ns);
void c64_network_log_audio_packet(struct c64_source *context, uint16_t sequence_num, size_t packet_size,
                                  uint16_t sample_count, int64_t jitter_us, bool has_signal,
                                  uint64_t packet_timestamp_ns);

#endif // C64_RECORD_NETWORK_H
