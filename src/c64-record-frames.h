/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_RECORD_FRAMES_H
#define C64_RECORD_FRAMES_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64_source;

// BMP frame saving functions
void c64_frames_save_as_bmp(struct c64_source *context, uint32_t *frame_buffer);

#endif // C64_RECORD_FRAMES_H
