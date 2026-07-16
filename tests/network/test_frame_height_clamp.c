/* C64 Stream - C64STR-021 (and supporting C64STR-023) regression.
 *
 * A malformed video packet could report a frame height above the PAL maximum
 * (e.g. line_num 271 + 4 lines = 275). All frame/recording buffers are
 * PAL-sized (272 rows), so an unclamped height drove heap OOB writes/reads in
 * black-screen render, frame output, and BGR recording conversion. Every
 * packet-derived height must be clamped to [NTSC_HEIGHT, PAL_HEIGHT] before it
 * reaches those buffers. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-protocol.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* Above PAL max -> clamped down to PAL_HEIGHT (the buffer size). */
    assert(c64_clamp_frame_height(275) == C64_PAL_HEIGHT); /* the malformed-packet case */
    assert(c64_clamp_frame_height(273) == C64_PAL_HEIGHT);
    assert(c64_clamp_frame_height(C64_PAL_HEIGHT + 1) == C64_PAL_HEIGHT);
    assert(c64_clamp_frame_height(0xFFFFFFFFu) == C64_PAL_HEIGHT);

    /* Exact standard formats pass through unchanged. */
    assert(c64_clamp_frame_height(C64_PAL_HEIGHT) == C64_PAL_HEIGHT);
    assert(c64_clamp_frame_height(C64_NTSC_HEIGHT) == C64_NTSC_HEIGHT);

    /* "Unknown format" band (241-250) stays within [NTSC, PAL] and never
     * exceeds the PAL buffer size. */
    assert(c64_clamp_frame_height(241) == 241);
    assert(c64_clamp_frame_height(250) == 250);
    assert(c64_clamp_frame_height(271) == 271);

    /* Below NTSC min -> clamped up so downstream never underflows a format. */
    assert(c64_clamp_frame_height(200) == C64_NTSC_HEIGHT);
    assert(c64_clamp_frame_height(0) == C64_NTSC_HEIGHT);

    /* The invariant that makes the buffers safe: clamp never exceeds PAL and is
     * never below NTSC, for any 32-bit input. */
    for (uint32_t h = 0; h < 2048; h++) {
        uint32_t c = c64_clamp_frame_height(h);
        assert(c >= C64_NTSC_HEIGHT && c <= C64_PAL_HEIGHT);
    }

    printf("test_frame_height_clamp: PASS\n");
    return 0;
}
