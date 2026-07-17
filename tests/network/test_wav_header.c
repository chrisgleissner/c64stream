/* C64 Stream - C64STR-010 / C64STR-011 regression.
 *
 * C64STR-010: the WAV header must carry the true detected sample rate, not a
 * hardcoded 48000 Hz.
 * C64STR-011: finalizing a recording that exceeds the 4 GiB RIFF limit must
 * clamp the 32-bit size fields (and warn) instead of wrapping. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-record-audio.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool c64_debug_logging = false;

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(void)
{
    char tmpl[] = "/tmp/c64wav_XXXXXX";
    int fd = mkstemp(tmpl);
    assert(fd >= 0);
    FILE *f = fdopen(fd, "wb+");
    assert(f != NULL);

    /* C64STR-010: write with the real PAL audio rate (not 48000). */
    const uint32_t pal_rate = 47983; /* llround(47982.887) */
    c64_audio_write_wav_header(f, pal_rate, 2, 16);

    /* Append a modest amount of data, then finalize normally. */
    const uint32_t data_bytes = 4096;
    uint8_t *data = calloc(1, data_bytes);
    assert(data != NULL);
    assert(fwrite(data, 1, data_bytes, f) == data_bytes);
    free(data);
    c64_audio_finalize_wav_header(f, data_bytes);
    fflush(f);

    /* Read back the 44-byte header. */
    uint8_t hdr[44];
    assert(fseek(f, 0, SEEK_SET) == 0);
    assert(fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr));

    assert(memcmp(hdr, "RIFF", 4) == 0);
    assert(memcmp(hdr + 8, "WAVE", 4) == 0);
    assert(rd_u32(hdr + 24) == pal_rate);              /* SampleRate (C64STR-010) */
    assert(rd_u32(hdr + 28) == pal_rate * 2 * 16 / 8); /* ByteRate follows the rate */
    assert(rd_u32(hdr + 40) == data_bytes);            /* Subchunk2Size */
    assert(rd_u32(hdr + 4) == data_bytes + 36);        /* ChunkSize = data + 36 */

    /* C64STR-011: finalize with a size beyond the 4 GiB RIFF limit. The 32-bit
     * fields must clamp to UINT32_MAX, never wrap to a small value. */
    const uint64_t huge = (uint64_t)5 * 1024 * 1024 * 1024; /* 5 GiB */
    c64_audio_finalize_wav_header(f, huge);
    fflush(f);
    assert(fseek(f, 0, SEEK_SET) == 0);
    assert(fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr));
    assert(rd_u32(hdr + 40) == UINT32_MAX); /* data size clamped, not wrapped */
    assert(rd_u32(hdr + 4) == UINT32_MAX);  /* chunk size clamped, not wrapped */

    fclose(f);
    remove(tmpl);

    printf("test_wav_header: PASS\n");
    return 0;
}
