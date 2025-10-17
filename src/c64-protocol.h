/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_PROTOCOL_H
#define C64_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

// C64 Stream constants
#define C64_VIDEO_PACKET_SIZE 780
#define C64_AUDIO_PACKET_SIZE 770
#define C64_VIDEO_HEADER_SIZE 12
#define C64_AUDIO_HEADER_SIZE 2
#define C64_CONTROL_PORT 64
#define C64_DEFAULT_VIDEO_PORT 21000
#define C64_DEFAULT_AUDIO_PORT 21001
#define C64_DEFAULT_HOST "c64u"

// Video format constants
#define C64_PAL_WIDTH 384
#define C64_PAL_HEIGHT 272
#define C64_NTSC_WIDTH 384
#define C64_NTSC_HEIGHT 240
#define C64_PIXELS_PER_LINE 384
#define C64_BYTES_PER_LINE 192 // 384 pixels / 2 (4-bit per pixel) - keeping original
#define C64_LINES_PER_PACKET 4

// Frame assembly constants
#define C64_MAX_PACKETS_PER_FRAME 68           // PAL: 272 lines ÷ 4 lines/packet = 68 packets
#define C64_FRAME_TIMEOUT_MS 100               // Timeout for incomplete frames
#define C64_PAL_FRAME_INTERVAL_NS 19950124ULL  // 19.95ms for 50.125Hz PAL (actual C64 timing)
#define C64_NTSC_FRAME_INTERVAL_NS 16710875ULL // 16.71ms for 59.826Hz NTSC (actual C64 timing)

// Forward declaration
struct c64_source;

// Protocol operations
void c64_send_control_command(struct c64_source *context, bool enable, uint8_t stream_id);

// Network packet logging utilities (conditional execution for performance)
void c64_log_video_packet_if_enabled(struct c64_source *context, const uint8_t *packet, size_t packet_size,
                                     uint64_t timestamp_ns);
void c64_log_audio_packet_if_enabled(struct c64_source *context, const uint8_t *packet, size_t packet_size,
                                     uint64_t timestamp_ns);

#endif  // C64_PROTOCOL_H
