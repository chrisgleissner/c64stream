/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_RECORD_VIDEO_H
#define C64_RECORD_VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

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
