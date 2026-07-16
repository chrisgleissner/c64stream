/* C64 Stream - C64STR-016 regression.
 *
 * Two sources added with default settings must not both bind 21000/21001 (which
 * silently starves one source of video/audio). Each instance draws a distinct
 * default UDP port pair from a per-instance counter. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-protocol.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* Instance 0 keeps the historical defaults. */
    uint32_t v = 0, a = 0;
    c64_default_ports_for_pair(0, &v, &a);
    assert(v == C64_DEFAULT_VIDEO_PORT); /* 21000 */
    assert(a == C64_DEFAULT_VIDEO_PORT + 1);

    /* Consecutive instances get distinct, non-overlapping pairs. */
    uint32_t prev_v = 0, prev_a = 0;
    c64_default_ports_for_pair(0, &prev_v, &prev_a);
    for (long i = 1; i < 64; i++) {
        uint32_t vi = 0, ai = 0;
        c64_default_ports_for_pair(i, &vi, &ai);
        assert(ai == vi + 1);                 /* audio follows video */
        assert(vi != prev_v && vi != prev_a); /* no collision with previous pair */
        assert(ai != prev_v && ai != prev_a);
        assert(vi > prev_v); /* strictly increasing */
        prev_v = vi;
        prev_a = ai;
    }

    /* Two-source scene: distinct pairs => both receive their own stream. */
    uint32_t v0, a0, v1, a1;
    c64_default_ports_for_pair(0, &v0, &a0);
    c64_default_ports_for_pair(1, &v1, &a1);
    assert(v0 != v1 && v0 != a1 && a0 != v1 && a0 != a1);

    printf("test_default_ports: PASS\n");
    return 0;
}
