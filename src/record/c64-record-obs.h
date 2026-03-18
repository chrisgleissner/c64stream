/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_RECORD_OBS_H
#define C64_RECORD_OBS_H

// Logging prefix for recording operations
#define RECORD_LOG_PREFIX "💾 RECORD:"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64_source;

// OBS timing management functions
void c64_obs_write_header(struct c64_source *context);
void c64_obs_log_video_event(struct c64_source *context, uint16_t frame_num, size_t frame_size, bool is_all_white);
void c64_obs_log_audio_event(struct c64_source *context, size_t data_size, bool has_signal);

#endif // C64_RECORD_OBS_H
