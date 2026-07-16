/* C64STR-012/013: AVI files roll before the legacy size limit and on format changes. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-record-video.h"

#include <assert.h>

int main(void)
{
    const uint64_t lowered_limit = 1024;
    assert(!c64_avi_segment_needs_rollover(384, 272, 50.125, 0, 216, 384, 272, 50.125, 800, lowered_limit));
    assert(c64_avi_segment_needs_rollover(384, 272, 50.125, 1, 216, 384, 272, 50.125, 809, lowered_limit));
    assert(c64_avi_segment_needs_rollover(384, 272, 50.125, 1, 500, 384, 234, 59.826, 1, UINT64_MAX));
    assert(c64_avi_segment_needs_rollover(384, 234, 59.826, 1, 500, 384, 272, 50.125, 1, UINT64_MAX));
    return 0;
}
