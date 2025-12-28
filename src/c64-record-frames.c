/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include <obs-module.h>
#include <util/platform.h>
#include <stdio.h>
#include <stdint.h>
#include "c64-logging.h"
#include "c64-record.h"
#include "c64-record-frames.h"
#include "c64-record-obs.h"
#include "c64-file.h"
#include "c64-types.h"

void c64_frames_save_as_bmp(struct c64_source *context, uint32_t *frame_buffer)
{
    if (!context->record_frames || !frame_buffer) {
        return;
    }

    // Ensure we have a recording session and CSV logging
    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_WARNING("Failed to create recording session for frame saving");
        return;
    }

    // Start CSV and network recording if enabled and not already active
    if (context->record_csv) {
        c64_start_csv_recording(context);
        c64_start_network_recording(context);
    }

    // Create frames subfolder within session folder
    char frames_folder[900];
    snprintf(frames_folder, sizeof(frames_folder), "%s/frames", context->session_folder);

    if (!c64_create_directory_recursive(frames_folder)) {
        C64_LOG_WARNING("Failed to create frames subfolder: %s", frames_folder);
        return;
    }

    // Create timestamped filename in frames subfolder
    uint64_t timestamp_ms = os_gettime_ns() / 1000000;
    char filename[900];
    snprintf(filename, sizeof(filename), "%s/frames/frame_%llu_%05u.bmp", context->session_folder,
             (unsigned long long)timestamp_ms, context->saved_frame_count++);

    FILE *file = fopen(filename, "wb");
    if (!file) {
        C64_LOG_WARNING("Failed to create frame file: %s", filename);
        return;
    }

    uint32_t width = context->width;
    uint32_t height = context->height;
    uint32_t row_padded = (width * 3 + 3) & (~3); // 4-byte alignment for BMP
    uint32_t image_size = row_padded * height;
    uint32_t file_size = 54 + image_size; // BMP header + image data

    // BMP Header (54 bytes total)
    uint8_t header[54] = {
        'B',
        'M', // Signature
        file_size & 0xFF,
        (file_size >> 8) & 0xFF,
        (file_size >> 16) & 0xFF,
        (file_size >> 24) & 0xFF, // File size
        0,
        0,
        0,
        0, // Reserved
        54,
        0,
        0,
        0, // Data offset
        40,
        0,
        0,
        0, // Header size
        width & 0xFF,
        (width >> 8) & 0xFF,
        (width >> 16) & 0xFF,
        (width >> 24) & 0xFF, // Width
        height & 0xFF,
        (height >> 8) & 0xFF,
        (height >> 16) & 0xFF,
        (height >> 24) & 0xFF, // Height
        1,
        0, // Planes
        24,
        0, // Bits per pixel
        0,
        0,
        0,
        0, // Compression
        image_size & 0xFF,
        (image_size >> 8) & 0xFF,
        (image_size >> 16) & 0xFF,
        (image_size >> 24) & 0xFF, // Image size
        0,
        0,
        0,
        0, // X pixels per meter
        0,
        0,
        0,
        0, // Y pixels per meter
        0,
        0,
        0,
        0, // Colors used
        0,
        0,
        0,
        0 // Colors important
    };

    fwrite(header, 1, 54, file);

    // Write image data (BMP stores bottom-to-top, convert RGBA to BGR)
    if (context->bmp_row_buffer) {
        uint8_t *row_buffer = context->bmp_row_buffer;
        for (int y = height - 1; y >= 0; y--) { // Bottom-to-top
            uint32_t *src_row = frame_buffer + (y * width);
            uint8_t *dst = row_buffer;

            for (uint32_t x = 0; x < width; x++) {
                uint32_t pixel = src_row[x];
                *dst++ = (pixel >> 16) & 0xFF; // B
                *dst++ = (pixel >> 8) & 0xFF;  // G
                *dst++ = pixel & 0xFF;         // R
            }

            // Pad to 4-byte boundary
            while ((dst - row_buffer) % 4 != 0) {
                *dst++ = 0;
            }

            fwrite(row_buffer, 1, row_padded, file);
        }
        // No free() needed - using pre-allocated buffer
    }

    fclose(file);
}
