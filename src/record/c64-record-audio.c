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
#include <pthread.h>
#include "c64-logging.h"
#include "c64-record-audio.h"
#include "c64-record-obs.h"
#include "c64-record-writer.h"
#include "c64-types.h"

// Helper function to write WAV file header
void c64_audio_write_wav_header(FILE *file, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample)
{
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;

    // WAV header (44 bytes) - we'll update sizes later
    fwrite("RIFF", 1, 4, file); // ChunkID
    uint32_t chunk_size = 36;   // ChunkSize
    fwrite(&chunk_size, 4, 1, file);
    fwrite("WAVE", 1, 4, file);   // Format
    fwrite("fmt ", 1, 4, file);   // Subchunk1ID
    uint32_t subchunk1_size = 16; // Subchunk1Size (PCM)
    fwrite(&subchunk1_size, 4, 1, file);
    uint16_t audio_format = 1; // AudioFormat (PCM)
    fwrite(&audio_format, 2, 1, file);
    fwrite(&channels, 2, 1, file);        // NumChannels
    fwrite(&sample_rate, 4, 1, file);     // SampleRate
    fwrite(&byte_rate, 4, 1, file);       // ByteRate
    fwrite(&block_align, 2, 1, file);     // BlockAlign
    fwrite(&bits_per_sample, 2, 1, file); // BitsPerSample
    fwrite("data", 1, 4, file);           // Subchunk2ID
    uint32_t subchunk2_size = 0;          // Subchunk2Size
    fwrite(&subchunk2_size, 4, 1, file);
}

// Helper function to finalize WAV header with correct file sizes
void c64_audio_finalize_wav_header(FILE *file, uint64_t data_size)
{
    if (!file) {
        return;
    }

    if (data_size > UINT32_MAX - 36U) {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " WAV exceeds the RIFF 4 GiB limit; header sizes are clamped");
    }
    const uint32_t data_size_32 = data_size > UINT32_MAX ? UINT32_MAX : (uint32_t)data_size;
    const uint32_t chunk_size = data_size > UINT32_MAX - 36U ? UINT32_MAX : data_size_32 + 36U;

    // Update ChunkSize (file size - 8)
    if (fseek(file, 4, SEEK_SET) != 0 || fwrite(&chunk_size, sizeof(chunk_size), 1, file) != 1) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to finalize WAV RIFF chunk size");
        return;
    }

    // Update Subchunk2Size (data size)
    if (fseek(file, 40, SEEK_SET) != 0 || fwrite(&data_size_32, sizeof(data_size_32), 1, file) != 1) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to finalize WAV data chunk size");
    }
}

void c64_audio_record_data(struct c64_source *context, const uint8_t *audio_data, size_t data_size)
{
    if (!context->record_video || !context->audio_file || !audio_data) {
        return;
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    /* C64CLK-005: never let a filesystem write stall the packet-processing
     * thread. The bounded writer queue copies this tiny fixed packet and the
     * background thread owns fwrite and the byte/sample accounting. A full
     * queue drops the packet (counted in record_write_drops by the enqueue);
     * surface it like the video path so lost audio is never silent. */
    if (!c64_record_writer_enqueue(context, C64_RECORD_WRITE_AUDIO, audio_data, data_size)) {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Recording queue full; dropped audio packet");
    }

    pthread_mutex_unlock(&context->recording_mutex);
}
