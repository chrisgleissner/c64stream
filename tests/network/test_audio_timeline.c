/* C64 Stream - C64CLK-001 / C64CLK-002 regression.
 *
 * The audio timeline state machine must map wrap-around sequence numbers onto
 * a monotonic packet index and classify packets as PLAY / DROP / CONCEAL /
 * RESYNC:
 * - in-order packets advance the index by exactly 1;
 * - gaps are concealed with the true seq delta (A/V sync preserved);
 * - duplicates and late packets are dropped WITHOUT advancing the index
 *   (the old behavior advanced by 1 and played them: +4 ms drift per event);
 * - gaps beyond the WAV fill cap and big backward jumps re-anchor (RESYNC)
 *   monotonically using the wall-clock slot. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-audio-timeline.h"

#include <assert.h>
#include <stdio.h>

static struct c64_audio_timeline tl;

static enum c64_audio_seq_action advance(uint16_t seq, uint64_t now_slot, uint64_t *index, uint32_t *gap)
{
    uint64_t idx = 0;
    uint32_t g = 0;
    enum c64_audio_seq_action action = c64_audio_timeline_advance(&tl, seq, now_slot, &idx, &g);
    if (index) {
        *index = idx;
    }
    if (gap) {
        *gap = g;
    }
    return action;
}

static void test_in_order(void)
{
    c64_audio_timeline_reset(&tl);
    uint64_t idx;
    uint32_t gap;

    assert(advance(100, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY); /* first packet inits */
    assert(idx == 0);

    for (uint16_t s = 101; s < 110; s++) {
        assert(advance(s, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
        assert(gap == 0);
    }
    assert(idx == 9);
    assert(tl.packets_lost == 0 && tl.duplicates == 0 && tl.late_dropped == 0 && tl.resyncs == 0);
}

static void test_single_and_multi_gap(void)
{
    c64_audio_timeline_reset(&tl);
    uint64_t idx;
    uint32_t gap;

    assert(advance(0, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);

    /* seq 1 lost: seq 2 arrives -> conceal 1, play at index 2. */
    assert(advance(2, 0, &idx, &gap) == C64_AUDIO_SEQ_CONCEAL);
    assert(gap == 1);
    assert(idx == 2); /* concealed packet plays at idx - gap + 0 == 1 */
    assert(tl.packets_lost == 1 && tl.concealed == 1);

    /* seq 3..6 lost: seq 7 arrives -> conceal 4, play at index 7. */
    assert(advance(7, 0, &idx, &gap) == C64_AUDIO_SEQ_CONCEAL);
    assert(gap == 4);
    assert(idx == 7);
    assert(tl.packets_lost == 5 && tl.concealed == 5);

    /* Timeline continues seq-exact afterwards. */
    assert(advance(8, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(idx == 8);
}

static void test_gap_at_wraparound(void)
{
    c64_audio_timeline_reset(&tl);
    uint64_t idx;
    uint32_t gap;

    assert(advance(65530, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(idx == 0);

    /* 65531..65535,0..2 lost (8 packets): seq 3 arrives -> delta 9. */
    assert(advance(3, 0, &idx, &gap) == C64_AUDIO_SEQ_CONCEAL);
    assert(gap == 8);
    assert(idx == 9);

    assert(advance(4, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(idx == 10);
}

static void test_duplicate_and_late(void)
{
    c64_audio_timeline_reset(&tl);
    uint64_t idx;
    uint32_t gap;

    assert(advance(10, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(advance(11, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(idx == 1);

    /* Duplicate of 11: DROP, index untouched. */
    assert(advance(11, 0, NULL, NULL) == C64_AUDIO_SEQ_DROP);
    assert(tl.duplicates == 1);

    /* Late packet 9 (before the anchor): DROP. */
    assert(advance(9, 0, NULL, NULL) == C64_AUDIO_SEQ_DROP);
    assert(tl.late_dropped == 1);

    /* The next in-order packet still lands at index 2 (no +1 drift). */
    assert(advance(12, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(idx == 2);
}

static void test_late_after_conceal(void)
{
    c64_audio_timeline_reset(&tl);
    uint64_t idx;
    uint32_t gap;

    assert(advance(20, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(advance(23, 0, &idx, &gap) == C64_AUDIO_SEQ_CONCEAL); /* 21,22 concealed */
    assert(gap == 2 && idx == 3);

    /* 21 arrives late after being concealed: dropped, timeline unchanged. */
    assert(advance(21, 0, NULL, NULL) == C64_AUDIO_SEQ_DROP);
    assert(tl.late_dropped == 1);
    assert(advance(24, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(idx == 4);
}

static void test_conceal_cap_and_forward_resync(void)
{
    c64_audio_timeline_reset(&tl);
    uint64_t idx;
    uint32_t gap;

    assert(advance(0, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);

    /* Exactly at the WAV cap: still concealed. */
    assert(advance((uint16_t)(1 + C64_AUDIO_WAV_FILL_MAX_PACKETS), 0, &idx, &gap) == C64_AUDIO_SEQ_CONCEAL);
    assert(gap == C64_AUDIO_WAV_FILL_MAX_PACKETS);
    assert(idx == 1 + C64_AUDIO_WAV_FILL_MAX_PACKETS);

    /* One past the cap: RESYNC. Wall clock says we are at slot 5000. */
    c64_audio_timeline_reset(&tl);
    assert(advance(0, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(advance((uint16_t)(2 + C64_AUDIO_WAV_FILL_MAX_PACKETS), 5000, &idx, &gap) == C64_AUDIO_SEQ_RESYNC);
    assert(gap == 0);
    assert(idx == 5000); /* re-anchored to the wall-clock slot */
    assert(tl.resyncs == 1);

    /* RESYNC is always monotonic even when the wall-clock slot is behind. */
    c64_audio_timeline_reset(&tl);
    assert(advance(0, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(advance((uint16_t)(2 + C64_AUDIO_WAV_FILL_MAX_PACKETS), 0, &idx, &gap) == C64_AUDIO_SEQ_RESYNC);
    assert(idx == 1); /* previous index + 1 */
}

static void test_backward_resync(void)
{
    c64_audio_timeline_reset(&tl);
    uint64_t idx;
    uint32_t gap;

    assert(advance(1000, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    for (uint16_t s = 1001; s <= 1010; s++) {
        assert(advance(s, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    }
    assert(idx == 10);

    /* Device restart: seq jumps back beyond the resync threshold. */
    assert(advance(5, 42, &idx, &gap) == C64_AUDIO_SEQ_RESYNC);
    assert(idx == 42); /* wall-clock slot (> previous_index + 1) */
    assert(tl.resyncs == 1);

    /* Stream continues in-order from the new anchor. */
    assert(advance(6, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(idx == 43);
}

static void test_index_matches_expected_timestamps(void)
{
    /* Timestamps are stream_start + index * interval; verify the index
     * arithmetic across a mix of events matches the seq-derived timeline. */
    c64_audio_timeline_reset(&tl);
    uint64_t idx;
    uint32_t gap;

    assert(advance(100, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);    /* idx 0 */
    assert(advance(101, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);    /* idx 1 */
    assert(advance(104, 0, &idx, &gap) == C64_AUDIO_SEQ_CONCEAL); /* 102,103 lost */
    assert(idx == 4 && gap == 2);
    assert(advance(104, 0, NULL, NULL) == C64_AUDIO_SEQ_DROP); /* duplicate */
    assert(advance(105, 0, &idx, &gap) == C64_AUDIO_SEQ_PLAY);
    assert(idx == 5); /* == seq delta from the first packet: 105 - 100 */
}

int main(void)
{
    test_in_order();
    test_single_and_multi_gap();
    test_gap_at_wraparound();
    test_duplicate_and_late();
    test_late_after_conceal();
    test_conceal_cap_and_forward_resync();
    test_backward_resync();
    test_index_matches_expected_timestamps();

    printf("test_audio_timeline: all tests passed\n");
    return 0;
}
