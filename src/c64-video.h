/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_VIDEO_H
#define C64_VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include "c64-color.h"

// Rendering defaults
// Legacy defines kept for compatibility during transition
#define C64_DEFAULT_RENDER_DELAY_FRAMES 3  // DEPRECATED - use network buffer instead
#define C64_MAX_RENDER_DELAY_FRAMES 100    // DEPRECATED - use network buffer instead
#define C64_RENDER_BUFFER_SAFETY_MARGIN 10 // Extra buffer frames for queue safety

// Timing constants (nanoseconds)
#define C64_FRAME_TIMEOUT_NS 100000000ULL       // 100ms - timeout for frame freshness (5 frames @ 50Hz)
#define C64_DEBUG_LOG_INTERVAL_NS 2000000000ULL // 2 seconds - interval for debug logging

// Forward declarations
struct c64_source;
struct frame_assembly;

// Helper functions for frame assembly
void c64_init_frame_assembly(struct frame_assembly *frame, uint16_t frame_num);
bool c64_is_frame_complete(struct frame_assembly *frame);
bool c64_is_frame_timeout(struct frame_assembly *frame);
void c64_render_frame_direct(struct c64_source *context, struct frame_assembly *frame, uint64_t timestamp_ns);
void c64_assemble_frame_with_interpolation(struct c64_source *context, struct frame_assembly *frame);

// Performance optimization functions
void c64_process_video_statistics_batch(struct c64_source *context, uint64_t current_time);
void c64_process_audio_statistics_batch(struct c64_source *context, uint64_t current_time);

// Lock-free frame assembly functions
bool c64_try_add_packet_lockfree(struct frame_assembly *frame, uint16_t packet_index);

// Video thread functions
void *c64_video_thread_func(void *data);
void *c64_video_processor_thread_func(void *data);

// Packet processing functions
void c64_process_video_packet_direct(struct c64_source *context, const uint8_t *packet, size_t packet_size,
                                     uint64_t timestamp_ns);

// Logo rendering function
void c64_render_logo_frame(struct c64_source *context, uint64_t timestamp_ns);

#endif // C64_VIDEO_H
