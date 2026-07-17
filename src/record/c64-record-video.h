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

/*
 * C64STR-034: the AVI header is refreshed periodically (about once per second)
 * for crash recovery, not on every frame -- a per-frame header rewrite + flush
 * stalled recording on slow mounts. Returns true only on the ~1 s boundary.
 * Header-only so the cadence is regression-testable without OBS/filesystem.
 */
static inline bool c64_avi_should_update_header(long frame_count, double fps)
{
    const uint32_t update_period = (uint32_t)(fps + 0.5);
    return update_period > 0 && (uint32_t)frame_count % update_period == 0;
}

/*
 * Patch an open legacy-AVI file's RIFF size (offset 4) and avih dwTotalFrames
 * (offset 48) in place from its current length, then restore the write
 * position to the end. Returns false if the file is not a valid AVI segment
 * (too short, or larger than RIFF's 32-bit size field), true otherwise.
 *
 * Header-only and OBS-free so the async record writer thread can maintain the
 * crash-recovery header without linking the whole video module; it is the
 * single implementation shared with c64_video_update_avi_header.
 */
static inline bool c64_avi_patch_header_inplace(FILE *file, uint32_t frame_count)
{
    if (!file) {
        return false;
    }
#ifdef _WIN32
    const int64_t end = _ftelli64(file);
#else
    const int64_t end = (int64_t)ftello(file);
#endif
    if (end < 8 || (uint64_t)end - 8 > UINT32_MAX) {
        return false;
    }
    const uint32_t file_size = (uint32_t)((uint64_t)end - 8); // total file size minus the RIFF(4)+size(4) prefix
#ifdef _WIN32
    if (_fseeki64(file, 4, SEEK_SET) == 0) {
        fwrite(&file_size, 4, 1, file);
    }
    if (_fseeki64(file, 48, SEEK_SET) == 0) {
        fwrite(&frame_count, 4, 1, file);
    }
    (void)_fseeki64(file, end, SEEK_SET);
#else
    if (fseeko(file, 4, SEEK_SET) == 0) {
        fwrite(&file_size, 4, 1, file);
    }
    if (fseeko(file, 48, SEEK_SET) == 0) {
        fwrite(&frame_count, 4, 1, file);
    }
    (void)fseeko(file, (off_t)end, SEEK_SET);
#endif
    return true;
}

/* Deterministic AVI segment filename: segment 0 is "<folder>/video.avi",
 * segment N (>0) is "<folder>/video.NNN.avi" (zero-padded to 3 digits). Kept
 * public so the continuation-name scheme can be regression-tested (C64STR-012). */
void c64_avi_segment_filename(char *buf, size_t size, const char *session_folder, uint32_t segment_index);

// Video recording functions (AVI format)
void c64_video_write_avi_header(FILE *file, uint32_t width, uint32_t height, double fps);
void c64_video_update_avi_header(FILE *file, uint32_t frame_count, uint32_t audio_samples_written);
void c64_video_convert_rgba_to_bgr24(uint32_t *rgba_buffer, uint8_t *bgr_buffer, uint32_t width, uint32_t height);
void c64_video_start_recording(struct c64_source *context);
void c64_video_record_frame(struct c64_source *context, uint32_t *frame_buffer);
void c64_video_stop_recording(struct c64_source *context);

#endif // C64_RECORD_VIDEO_H
