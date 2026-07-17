/* C64 Stream - C64CLK-001 regression: concealment fill generator.
 *
 * The fill must be splice-free at BOTH ends and never zero-fill against the
 * SID DC offset:
 * - packet 0 starts at the held last sample of the packet before the gap;
 * - the final ~1 ms ramps to the first sample of the packet after the gap;
 * - in between the held value fades toward 0 (long outages decay to silence
 *   instead of freezing on a DC plateau);
 * - every packet is exactly 192 stereo frames (768 bytes). */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-audio-timeline.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Any step above this is a click for the deterministic sine fixture; the fill
 * generator must stay far below it at every sample boundary. */
#define MAX_STEP 600

static int16_t sample_at(const uint8_t *pcm, size_t frame, int right)
{
    const size_t off = frame * 4 + (right ? 2 : 0);
    return (int16_t)((uint16_t)pcm[off] | ((uint16_t)pcm[off + 1] << 8));
}

/* Generate the full fill for a gap of n and validate shape + splices. */
static void check_gap(uint32_t n, int16_t last_l, int16_t last_r, int16_t next_l, int16_t next_r)
{
    struct c64_audio_conceal_fill fill = {
        .last_left = last_l,
        .last_right = last_r,
        .next_left = next_l,
        .next_right = next_r,
    };

    uint8_t *pcm = malloc((size_t)n * 768);
    assert(pcm != NULL);
    for (uint32_t k = 0; k < n; k++) {
        c64_audio_conceal_fill_packet(&fill, k, n, pcm + (size_t)k * 768);
    }

    const size_t frames = (size_t)n * 192;

    /* Entry splice: first fill sample continues the held value. */
    assert(abs(sample_at(pcm, 0, 0) - last_l) <= MAX_STEP);
    assert(abs(sample_at(pcm, 0, 1) - last_r) <= MAX_STEP);

    /* Exit splice: last fill sample lands on the next real sample. */
    assert(abs(sample_at(pcm, frames - 1, 0) - next_l) <= MAX_STEP);
    assert(abs(sample_at(pcm, frames - 1, 1) - next_r) <= MAX_STEP);

    /* No step anywhere inside the fill. */
    for (size_t f = 1; f < frames; f++) {
        assert(abs(sample_at(pcm, f, 0) - sample_at(pcm, f - 1, 0)) <= MAX_STEP);
        assert(abs(sample_at(pcm, f, 1) - sample_at(pcm, f - 1, 1)) <= MAX_STEP);
    }

    /* Long outages decay to silence before the exit ramp. */
    if (frames > (size_t)C64_AUDIO_CONCEAL_FADE_SAMPLES + C64_AUDIO_CONCEAL_RAMP_SAMPLES) {
        const size_t quiet = C64_AUDIO_CONCEAL_FADE_SAMPLES;
        assert(sample_at(pcm, quiet, 0) == 0);
        assert(sample_at(pcm, quiet, 1) == 0);
    }

    free(pcm);
}

int main(void)
{
    /* DC-offset input (the SID case): both splices must be step-free even
     * though the signal never crosses zero. */
    check_gap(1, 14000, 14000, 13500, 13500);

    /* Asymmetric stereo endpoints. */
    check_gap(1, -12000, 9000, 5000, -3000);

    /* Multi-packet gap. */
    check_gap(4, 14000, -14000, -14000, 14000);

    /* OBS cap boundary (~100 ms). */
    check_gap(C64_AUDIO_CONCEAL_MAX_PACKETS, 14000, 14000, -14000, -14000);

    /* Long outage (1 s): decays to silence, then ramps to the next sample. */
    check_gap(250, 14000, 14000, 2000, 2000);

    /* Extremes must not overflow int16 anywhere. */
    check_gap(2, INT16_MAX, INT16_MIN, INT16_MIN, INT16_MAX);

    /* Degenerate inputs must not crash and must zero the buffer. */
    uint8_t buf[768];
    c64_audio_conceal_fill_packet(NULL, 0, 1, buf);
    for (size_t i = 0; i < sizeof(buf); i++) {
        assert(buf[i] == 0);
    }
    struct c64_audio_conceal_fill fill = {0};
    c64_audio_conceal_fill_packet(&fill, 5, 5, buf); /* k >= n */
    c64_audio_conceal_fill_packet(&fill, 0, 0, buf); /* n == 0 */

    printf("test_audio_conceal_fill: all tests passed\n");
    return 0;
}
