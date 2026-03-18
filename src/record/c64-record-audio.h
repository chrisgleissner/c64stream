/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_RECORD_AUDIO_H
#define C64_RECORD_AUDIO_H

// Logging prefix for recording operations
#define RECORD_LOG_PREFIX "💾 RECORD:"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Forward declarations
struct c64_source;

// Audio recording functions (WAV format)
void c64_audio_write_wav_header(FILE *file, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);
void c64_audio_finalize_wav_header(FILE *file, uint32_t data_size);
void c64_audio_record_data(struct c64_source *context, const uint8_t *audio_data, size_t data_size);

#endif // C64_RECORD_AUDIO_H
