/* C64STR-012 / C64STR-013 regression.
 *
 * Covers the AVI segment rollover contract end to end:
 *   - the rollover decision (size limit + PAL<->NTSC geometry change),
 *   - deterministic continuation filenames (video.avi, video.001.avi, ...),
 *   - each segment is an independently valid AVI whose 32-bit RIFF size fields
 *     are correct and whose header geometry matches the segment's format.
 *
 * Links the real src/record/c64-record-video.c (header writer/updater +
 * filename helper) and stubs the few OBS/source hooks it references. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-record-video.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- stubs for symbols referenced by c64-record-video.c but unused here --- */
bool c64_debug_logging = false;
struct c64_source;
void c64_obs_log_video_event(struct c64_source *context, uint16_t frame_num, size_t frame_size, bool is_all_white)
{
    (void)context;
    (void)frame_num;
    (void)frame_size;
    (void)is_all_white;
}
void c64_session_ensure_exists(struct c64_source *context)
{
    (void)context;
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Write one AVI segment: header for (w,h,fps), `frames` frames of payload, then
 * finalize the header. Reads it back and validates it as an independent AVI. */
static void write_and_validate_segment(const char *path, uint32_t w, uint32_t h, double fps, uint32_t frames)
{
    const uint32_t frame_size = w * h * 3; /* BGR24 */

    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    c64_video_write_avi_header(f, w, h, fps);
    /* Simulate the movi payload (frame chunks) as raw bytes. */
    uint8_t *chunk = calloc(1, frame_size);
    assert(chunk != NULL);
    for (uint32_t i = 0; i < frames; i++) {
        assert(fwrite(chunk, 1, frame_size, f) == frame_size);
    }
    free(chunk);
    c64_video_update_avi_header(f, frames, 0);
    fclose(f);

    /* Read the whole file back. */
    f = fopen(path, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    assert(fsize > 88);
    uint8_t *buf = malloc((size_t)fsize);
    assert(buf != NULL);
    assert(fread(buf, 1, (size_t)fsize, f) == (size_t)fsize);
    fclose(f);

    /* Independently valid: RIFF/AVI magic and coherent top-level size. */
    assert(memcmp(buf, "RIFF", 4) == 0);
    assert(memcmp(buf + 8, "AVI ", 4) == 0);
    assert(rd_u32(buf + 4) == (uint32_t)(fsize - 8)); /* RIFF size == filesize - 8 */

    /* avih geometry + frame count must match this segment's format. */
    assert(rd_u32(buf + 32) == (uint32_t)(1000000.0 / fps + 0.5)); /* dwMicroSecPerFrame */
    assert(rd_u32(buf + 48) == frames);                            /* dwTotalFrames */
    assert(rd_u32(buf + 64) == w);                                 /* dwWidth */
    assert(rd_u32(buf + 68) == h);                                 /* dwHeight */

    free(buf);
}

int main(void)
{
    /* 1. Rollover decision: no roll below the (lowered) limit, roll once the
     * next chunk would cross it; roll on any PAL<->NTSC geometry/fps change. */
    const uint64_t lowered_limit = 1024;
    assert(!c64_avi_segment_needs_rollover(384, 272, 50.125, 0, 216, 384, 272, 50.125, 800, lowered_limit));
    assert(c64_avi_segment_needs_rollover(384, 272, 50.125, 1, 216, 384, 272, 50.125, 809, lowered_limit));
    assert(c64_avi_segment_needs_rollover(384, 272, 50.125, 1, 500, 384, 234, 59.826, 1, UINT64_MAX));
    assert(c64_avi_segment_needs_rollover(384, 234, 59.826, 1, 500, 384, 272, 50.125, 1, UINT64_MAX));

    /* Near the real 2 GiB legacy boundary: the 64-bit accumulation must decide
     * correctly without wrapping a 32-bit counter. */
    const uint64_t near_limit = C64_AVI_SEGMENT_LIMIT_BYTES;
    assert(
        !c64_avi_segment_needs_rollover(384, 272, 50.125, 100, near_limit - 5000, 384, 272, 50.125, 4000, near_limit));
    assert(c64_avi_segment_needs_rollover(384, 272, 50.125, 100, near_limit - 100, 384, 272, 50.125, 4000, near_limit));

    /* 2. Deterministic continuation filenames. */
    char name[64];
    c64_avi_segment_filename(name, sizeof(name), "/rec", 0);
    assert(strcmp(name, "/rec/video.avi") == 0);
    c64_avi_segment_filename(name, sizeof(name), "/rec", 1);
    assert(strcmp(name, "/rec/video.001.avi") == 0);
    c64_avi_segment_filename(name, sizeof(name), "/rec", 42);
    assert(strcmp(name, "/rec/video.042.avi") == 0);
    c64_avi_segment_filename(name, sizeof(name), "/rec", 1234);
    assert(strcmp(name, "/rec/video.1234.avi") == 0);

    /* 3. Each produced segment is an independently valid AVI with correct size
     * fields and geometry -- including a PAL->NTSC transition across segments. */
    char tmpl[] = "/tmp/c64avi_XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir != NULL);

    char seg0[128], seg1[128];
    c64_avi_segment_filename(seg0, sizeof(seg0), dir, 0);
    c64_avi_segment_filename(seg1, sizeof(seg1), dir, 1);

    write_and_validate_segment(seg0, 384, 272, 50.125, 8); /* PAL segment */
    write_and_validate_segment(seg1, 384, 234, 59.826, 5); /* NTSC continuation */

    remove(seg0);
    remove(seg1);
    remove(dir);

    printf("test_c64_avi_segments: PASS\n");
    return 0;
}
