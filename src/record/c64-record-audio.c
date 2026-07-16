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

    size_t wav_written = fwrite(audio_data, 1, data_size, context->audio_file);

    if (wav_written == data_size) {
        context->recorded_audio_bytes += wav_written;
        // Calculate samples correctly: data_size is in bytes, each stereo sample is 4 bytes (16-bit L + 16-bit R)
        long new_samples = (long)(data_size / 4);
        // Add samples atomically - need to manually implement atomic add since OBS doesn't have it
        long old_samples, new_total_samples;
        do {
            old_samples = os_atomic_load_long(&context->recorded_audio_samples);
            new_total_samples = old_samples + new_samples;
        } while (!os_atomic_compare_swap_long(&context->recorded_audio_samples, old_samples, new_total_samples));

        // Note: CSV logging for audio events is now handled independently in the video processor thread
    } else {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to write audio data to WAV recording");
    }

    pthread_mutex_unlock(&context->recording_mutex);
}
