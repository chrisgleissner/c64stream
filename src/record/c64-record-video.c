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
#include <inttypes.h>
#include <limits.h>
#include "c64-logging.h"
#include "c64-record.h"
#include "c64-record-video.h"
#include "c64-record-obs.h"
#include "c64-types.h"

static int64_t c64_avi_tell(FILE *file)
{
#ifdef _WIN32
    return _ftelli64(file);
#else
    return (int64_t)ftello(file);
#endif
}

static int c64_avi_seek(FILE *file, int64_t offset, int whence)
{
#ifdef _WIN32
    return _fseeki64(file, offset, whence);
#else
    return fseeko(file, (off_t)offset, whence);
#endif
}

static bool c64_video_finalize_segment_locked(struct c64_source *context)
{
    if (!context->video_file) {
        return true;
    }

    c64_video_update_avi_header(context->video_file, context->avi_segment_frames, 0);
    if (fclose(context->video_file) != 0) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to finalize AVI segment %u", context->avi_segment_index);
        context->video_file = NULL;
        return false;
    }
    context->video_file = NULL;
    return true;
}

static bool c64_video_open_next_segment_locked(struct c64_source *context, uint32_t width, uint32_t height, double fps)
{
    char filename[950];
    if (context->avi_segment_index == 0) {
        snprintf(filename, sizeof(filename), "%s/video.avi", context->session_folder);
    } else {
        snprintf(filename, sizeof(filename), "%s/video.%03u.avi", context->session_folder, context->avi_segment_index);
    }

    context->video_file = fopen(filename, "wb");
    if (!context->video_file) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to open AVI segment: %s", filename);
        return false;
    }

    c64_video_write_avi_header(context->video_file, width, height, fps);
    const int64_t header_size = c64_avi_tell(context->video_file);
    if (header_size < 0) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to determine AVI header size: %s", filename);
        fclose(context->video_file);
        context->video_file = NULL;
        return false;
    }
    context->avi_segment_width = width;
    context->avi_segment_height = height;
    context->avi_segment_fps = fps;
    context->avi_segment_frames = 0;
    context->avi_segment_bytes = (uint64_t)header_size;
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Started AVI segment %u: %s (%ux%u @ %.3f fps)", context->avi_segment_index,
                 filename, width, height, fps);
    return true;
}

static bool c64_video_roll_segment_locked(struct c64_source *context, uint32_t width, uint32_t height, double fps,
                                          const char *reason)
{
    if (!c64_video_finalize_segment_locked(context)) {
        return false;
    }
    context->avi_segment_index++;
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Rolling AVI recording to segment %u (%s)", context->avi_segment_index, reason);
    return c64_video_open_next_segment_locked(context, width, height, fps);
}

// Helper function to write minimal, standard-compliant AVI header
void c64_video_write_avi_header(FILE *file, uint32_t width, uint32_t height, double fps)
{
    uint32_t frame_size = width * height * 3; // BGR24 bytes per frame
    uint32_t zero = 0;

    // Calculate precise frame period in microseconds
    uint32_t frame_period = (uint32_t)(1000000.0 / fps + 0.5); // Round to nearest microsecond

    // RIFF header (will update file size later)
    fwrite("RIFF", 1, 4, file);
    uint32_t file_size_placeholder = 0;
    fwrite(&file_size_placeholder, 4, 1, file);
    fwrite("AVI ", 1, 4, file);

    // LIST hdrl chunk
    fwrite("LIST", 1, 4, file);
    uint32_t hdrl_size = 4 + 56 + (4 + 48 + 4 + 40); // hdrl + avih + video_strl (NO AUDIO)
    fwrite(&hdrl_size, 4, 1, file);
    fwrite("hdrl", 1, 4, file);

    // Main AVI header (avih)
    fwrite("avih", 1, 4, file);
    uint32_t avih_size = 56;
    fwrite(&avih_size, 4, 1, file);

    uint32_t max_bytes_per_sec = (uint32_t)(frame_size * fps);

    fwrite(&frame_period, 4, 1, file);      // dwMicroSecPerFrame
    fwrite(&max_bytes_per_sec, 4, 1, file); // dwMaxBytesPerSec
    fwrite(&zero, 4, 1, file);              // dwPaddingGranularity
    fwrite(&zero, 4, 1, file);              // dwFlags
    fwrite(&zero, 4, 1, file);              // dwTotalFrames (update later)
    fwrite(&zero, 4, 1, file);              // dwInitialFrames
    uint32_t one = 1;
    fwrite(&one, 4, 1, file);        // dwStreams (video only)
    fwrite(&frame_size, 4, 1, file); // dwSuggestedBufferSize
    fwrite(&width, 4, 1, file);      // dwWidth
    fwrite(&height, 4, 1, file);     // dwHeight
    fwrite(&zero, 4, 1, file);       // dwReserved[0]
    fwrite(&zero, 4, 1, file);       // dwReserved[1]
    fwrite(&zero, 4, 1, file);       // dwReserved[2]
    fwrite(&zero, 4, 1, file);       // dwReserved[3]

    // LIST strl chunk (stream list)
    fwrite("LIST", 1, 4, file);
    uint32_t strl_size = 4 + 48 + 4 + 40; // strl + strh + strf + BITMAPINFOHEADER
    fwrite(&strl_size, 4, 1, file);
    fwrite("strl", 1, 4, file);

    // Stream header (strh)
    fwrite("strh", 1, 4, file);
    uint32_t strh_size = 48;
    fwrite(&strh_size, 4, 1, file);
    fwrite("vids", 1, 4, file);     // fccType (video)
    fwrite("\0\0\0\0", 1, 4, file); // fccHandler (uncompressed)
    fwrite(&zero, 4, 1, file);      // dwFlags
    uint16_t priority = 0;
    fwrite(&priority, 2, 1, file); // wPriority
    uint16_t language = 0;
    fwrite(&language, 2, 1, file); // wLanguage
    fwrite(&zero, 4, 1, file);     // dwInitialFrames
    // Use detected refresh rate for scale/rate
    uint32_t scale = 1000000;
    fwrite(&scale, 4, 1, file);                        // dwScale
    uint32_t rate = (uint32_t)(fps * 1000000.0 + 0.5); // Rate in scale units, rounded
    fwrite(&rate, 4, 1, file);                         // dwRate
    fwrite(&zero, 4, 1, file);                         // dwStart
    fwrite(&zero, 4, 1, file);                         // dwLength (update later)
    fwrite(&frame_size, 4, 1, file);                   // dwSuggestedBufferSize
    uint32_t quality = 0xFFFFFFFF;
    fwrite(&quality, 4, 1, file); // dwQuality (-1 = default)
    fwrite(&zero, 4, 1, file);    // dwSampleSize (0 for video)

    // Stream format (strf) - BITMAPINFOHEADER
    fwrite("strf", 1, 4, file);
    uint32_t strf_size = 40; // BITMAPINFOHEADER size
    fwrite(&strf_size, 4, 1, file);

    // BITMAPINFOHEADER
    fwrite(&strf_size, 4, 1, file); // biSize
    fwrite(&width, 4, 1, file);     // biWidth
    int32_t negative_height = -(int32_t)height;
    fwrite(&negative_height, 4, 1, file); // biHeight (negative = top-down)
    uint16_t planes = 1;
    fwrite(&planes, 2, 1, file); // biPlanes
    uint16_t bit_count = 24;
    fwrite(&bit_count, 2, 1, file); // biBitCount
    fwrite(&zero, 4, 1, file);      // biCompression (0 = uncompressed)
    uint32_t image_size = frame_size;
    fwrite(&image_size, 4, 1, file); // biSizeImage
    fwrite(&zero, 4, 1, file);       // biXPelsPerMeter
    fwrite(&zero, 4, 1, file);       // biYPelsPerMeter
    fwrite(&zero, 4, 1, file);       // biClrUsed
    fwrite(&zero, 4, 1, file);       // biClrImportant

    // LIST movi chunk (where frame data goes - VIDEO ONLY)
    fwrite("LIST", 1, 4, file);
    fwrite(&zero, 4, 1, file); // movi size (update later)
    fwrite("movi", 1, 4, file);
}

void c64_video_update_avi_header(FILE *file, uint32_t frame_count, uint32_t audio_samples_written)
{
    UNUSED_PARAMETER(audio_samples_written);
    if (!file)
        return;

    const int64_t current_pos = c64_avi_tell(file);
    if (current_pos < 8 || (uint64_t)current_pos - 8 > UINT32_MAX) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " AVI header update rejected invalid segment size");
        return;
    }
    uint32_t file_size = (uint32_t)((uint64_t)current_pos - 8); // Total file size minus RIFF header

    // Update RIFF file size
    if (c64_avi_seek(file, 4, SEEK_SET) != 0) {
        return;
    }
    fwrite(&file_size, 4, 1, file);

    // Update total frames in main AVI header (avih)
    // RIFF(4) + size(4) + AVI(4) + LIST(4) + size(4) + hdrl(4) + avih(4) + size(4) + period(4) + maxbytes(4) + pad(4) + flags(4) = 48
    if (c64_avi_seek(file, 48, SEEK_SET) != 0) {
        return;
    }
    fwrite(&frame_count, 4, 1, file);

    // Seek back to end. The caller writes headers periodically and at stop;
    // flushing every video frame stalls recording on slow mounts.
    (void)c64_avi_seek(file, current_pos, SEEK_SET);
}

// Helper function to convert RGBA to BGR24
void c64_video_convert_rgba_to_bgr24(uint32_t *rgba_buffer, uint8_t *bgr_buffer, uint32_t width, uint32_t height)
{
    for (uint32_t i = 0; i < width * height; i++) {
        uint32_t pixel = rgba_buffer[i];
        // RGBA is typically: 0xAABBGGRR (little endian)
        // We need BGR24: B, G, R
        uint8_t r = pixel & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = (pixel >> 16) & 0xFF;

        bgr_buffer[i * 3 + 0] = b; // Blue
        bgr_buffer[i * 3 + 1] = g; // Green
        bgr_buffer[i * 3 + 2] = r; // Red
    }
}

// Video recording functions (raw uncompressed format for minimal CPU overhead)
void c64_video_start_recording(struct c64_source *context)
{
    if (!context->record_video || context->video_file) {
        return; // Already recording or not enabled
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Ensure we have a recording session (creates if needed, joins if exists)
    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create recording session for video recording");
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    // Create video filename in the session folder
    char video_filename[950];
    snprintf(video_filename, sizeof(video_filename), "%s/video.avi", context->session_folder);

    // Open file for recording
    context->video_file = fopen(video_filename, "wb");

    if (!context->video_file) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create video recording file");
    } else {
        uint64_t timestamp_ms = os_gettime_ns() / 1000000;
        context->recording_start_time = timestamp_ms;
        os_atomic_store_long(&context->recorded_frames, 0);

        // Write AVI header with detected frame rate
        c64_video_write_avi_header(context->video_file, context->width, context->height, context->expected_fps);

        C64_LOG_INFO("" RECORD_LOG_PREFIX " Started video recording: %s", video_filename);
    }

    pthread_mutex_unlock(&context->recording_mutex);
}

void c64_video_record_frame(struct c64_source *context, uint32_t *frame_buffer)
{
    if (!context->record_video || !context->video_file || !frame_buffer) {
        return;
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    const uint32_t width = context->width;
    const uint32_t height = context->height;
    const double fps = context->expected_fps;
    const uint64_t frame_bytes = (uint64_t)width * height * 3;
    const uint64_t chunk_bytes = 8 + frame_bytes + (frame_bytes & 1);

    if (width == 0 || height == 0 || frame_bytes > UINT32_MAX) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Invalid AVI frame geometry: %ux%u", width, height);
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    const bool format_changed = context->avi_segment_width != width || context->avi_segment_height != height ||
                                context->avi_segment_fps != fps;
    const bool roll_segment = c64_avi_segment_needs_rollover(context->avi_segment_width, context->avi_segment_height,
                                                             context->avi_segment_fps, context->avi_segment_frames,
                                                             context->avi_segment_bytes, width, height, fps,
                                                             chunk_bytes, C64_AVI_SEGMENT_LIMIT_BYTES);
    if (roll_segment) {
        if (!c64_video_roll_segment_locked(context, width, height, fps,
                                           format_changed ? "video format changed" : "legacy RIFF size limit")) {
            context->record_video = false;
            pthread_mutex_unlock(&context->recording_mutex);
            return;
        }
    }

    // Write AVI frame chunk with proper header
    size_t frame_size = (size_t)frame_bytes; // BGR24
    // Use pre-allocated buffer to eliminate malloc/free in hot path
    if (context->bgr_frame_buffer) {
        uint8_t *bgr_buffer = context->bgr_frame_buffer;

        // Check if frame_buffer has non-zero data
        uint32_t non_zero_pixels = 0;
        for (uint32_t i = 0; i < width * height && i < 100; i++) {
            if (frame_buffer[i] != 0)
                non_zero_pixels++;
        }

        // Very rare spot checks for recording stats (every 10 minutes)
        static int recording_debug_count = 0;
        static uint64_t last_recording_log_time = 0;
        uint64_t now = os_gettime_ns();
        if ((++recording_debug_count % 10000) == 0 ||
            (now - last_recording_log_time >= 600000000000ULL)) { // Every 10k frames OR 10 minutes
            C64_LOG_DEBUG("" RECORD_LOG_PREFIX
                          " RECORDING SPOT CHECK: frame %ld, %ux%u, non_zero=%u/100, fps=%.3f (total count: %d)",
                          os_atomic_load_long(&context->recorded_frames), width, height, non_zero_pixels, fps,
                          recording_debug_count);
            last_recording_log_time = now;
        }

        c64_video_convert_rgba_to_bgr24(frame_buffer, bgr_buffer, width, height);

        // Very rare spot checks for BGR debug data (60Hz only, every 15 minutes)
        if ((int)(fps + 0.5) == 60) {
            static int bgr_debug_count = 0;
            static uint64_t last_bgr_log_time = 0;
            uint64_t now = os_gettime_ns();
            if ((++bgr_debug_count % 20000) == 0 ||
                (now - last_bgr_log_time >= 900000000000ULL)) { // Every 20k frames OR 15 minutes
                char hexbuf[49];                                // 16 bytes * 3 chars + null
                for (int i = 0; i < 16; i++) {
                    sprintf(hexbuf + i * 3, "%02X ", bgr_buffer[i]);
                }
                hexbuf[48] = '\0';
                C64_LOG_DEBUG("" RECORD_LOG_PREFIX " BGR SPOT CHECK: frame %ld [0..15]: %s (total count: %d)",
                              os_atomic_load_long(&context->recorded_frames), hexbuf, bgr_debug_count);
                last_bgr_log_time = now;
            }
        }

        // Write AVI frame chunk header ("00db" = stream 0, uncompressed DIB)
        fwrite("00db", 1, 4, context->video_file);
        uint32_t chunk_size = (uint32_t)frame_size;
        fwrite(&chunk_size, 4, 1, context->video_file);

        // Write frame data
        size_t written = fwrite(bgr_buffer, 1, frame_size, context->video_file);

        // Ensure word alignment (AVI requirement)
        if (frame_size % 2) {
            uint8_t pad = 0;
            fwrite(&pad, 1, 1, context->video_file);
        }

        // No free() needed - using pre-allocated buffer

        if (written == frame_size) {
            long new_frame_count = os_atomic_inc_long(&context->recorded_frames);
            context->avi_segment_frames++;
            context->avi_segment_bytes += chunk_bytes;

            // Keep crash recovery useful without forcing synchronous I/O per frame.
            const uint32_t update_period = (uint32_t)(fps + 0.5);
            if (update_period > 0 && (uint32_t)new_frame_count % update_period == 0) {
                c64_video_update_avi_header(context->video_file, context->avi_segment_frames, 0);
            }

            // Log video recording timing information to CSV (frame_num = 0 for recording events)
            c64_obs_log_video_event(context, 0, frame_size, context->av_sync_last_video_all_white);
        } else {
            C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to write video frame to recording");
        }
    } else {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to allocate BGR conversion buffer");
    }

    pthread_mutex_unlock(&context->recording_mutex);
}

void c64_video_stop_recording(struct c64_source *context)
{
    if (!context->video_file) {
        return; // Not recording
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Close recording file and finalize format
    if (context->video_file) {
        (void)c64_video_finalize_segment_locked(context);
    }

    pthread_mutex_unlock(&context->recording_mutex);
}
