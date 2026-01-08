/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_AV_SYNC_H
#define C64_AV_SYNC_H

#include <stdint.h>
#include <stdbool.h>

struct c64_source;

void c64_av_sync_init(struct c64_source *context);
void c64_av_sync_cleanup(struct c64_source *context);

void c64_av_sync_on_video_pop(struct c64_source *context, uint16_t frame_num, uint64_t timestamp_ns);
void c64_av_sync_on_audio_pop(struct c64_source *context, uint64_t timestamp_ns);

#endif // C64_AV_SYNC_H
