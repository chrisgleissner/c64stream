/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_RECORD_VIDEO_H
#define C64_RECORD_VIDEO_H

// Logging prefix for recording operations
#define RECORD_LOG_PREFIX "💾 RECORD:"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* Keep well below the legacy AVI 2 GiB interoperability boundary. */
#define C64_AVI_SEGMENT_LIMIT_BYTES (UINT64_C(2) * 1024 * 1024 * 1024 - 16 * 1024 * 1024)

/* Kept header-only so the rollover decision can be regression-tested without
 * filesystem or OBS dependencies. */
static inline bool c64_avi_segment_needs_rollover(uint32_t segment_width, uint32_t segment_height, double segment_fps,
                                                  uint32_t segment_frames, uint64_t segment_bytes, uint32_t width,
                                                  uint32_t height, double fps, uint64_t next_chunk_bytes,
                                                  uint64_t limit_bytes)
{
    return segment_width != width || segment_height != height || segment_fps != fps ||
           (segment_frames > 0 && segment_bytes + next_chunk_bytes > limit_bytes);
}

// Forward declarations
struct c64_source;

// Video recording functions (AVI format)
void c64_video_write_avi_header(FILE *file, uint32_t width, uint32_t height, double fps);
void c64_video_update_avi_header(FILE *file, uint32_t frame_count, uint32_t audio_samples_written);
void c64_video_convert_rgba_to_bgr24(uint32_t *rgba_buffer, uint8_t *bgr_buffer, uint32_t width, uint32_t height);
void c64_video_start_recording(struct c64_source *context);
void c64_video_record_frame(struct c64_source *context, uint32_t *frame_buffer);
void c64_video_stop_recording(struct c64_source *context);

#endif // C64_RECORD_VIDEO_H
