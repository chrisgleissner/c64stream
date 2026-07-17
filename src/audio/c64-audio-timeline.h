/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_AUDIO_TIMELINE_H
#define C64_AUDIO_TIMELINE_H

/*
 * C64CLK-001/002: pure (OBS-free) audio sequence timeline.
 *
 * Maps the 16-bit wrap-around packet sequence numbers onto a monotonic
 * packet index (synthetic timestamp = stream_start_ns + index * interval_ns)
 * and classifies every incoming packet:
 *
 *   PLAY     in-order packet: play at the next index.
 *   DROP     late/duplicate (seq delta <= 0 within the resync threshold):
 *            discard, do NOT advance the index. If the "missing" packet was
 *            already concealed and then arrives late, it is dropped too -
 *            4 ms of concealed audio instead of real audio is the correct
 *            trade for a splice-free, sync-exact timeline.
 *   CONCEAL  forward gap: synthesize the missing packets (hold-last-sample
 *            with fade, never zeros - real SID output carries a DC offset and
 *            a zero-fill against it is itself a click), then play the real
 *            packet at its seq-exact index. A/V sync is preserved by
 *            construction: the index still advances by the true seq delta.
 *   RESYNC   gap beyond the fill cap or a backward jump beyond the resync
 *            threshold (device restart, not reordering): re-anchor the index
 *            to max(previous_index + 1, wall-clock slot) so the timeline
 *            stays monotonic and lands where real time says it should.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Max packets synthesized per gap for the OBS playback path (~100 ms). */
#define C64_AUDIO_CONCEAL_MAX_PACKETS 25
/* Max packets synthesized per gap for the WAV recording path (~5 s), keeping
 * the WAV duration-matched to the AVI across longer outages. */
#define C64_AUDIO_WAV_FILL_MAX_PACKETS 1250
/* Backward seq jump (packets, ~1 s) treated as a stream restart. */
#define C64_AUDIO_RESYNC_THRESHOLD 250

/* Fill shape: the held value fades linearly to 0 over this many samples
 * (~100 ms), so long concealments decay to silence instead of freezing on a
 * DC plateau... */
#define C64_AUDIO_CONCEAL_FADE_SAMPLES (C64_AUDIO_CONCEAL_MAX_PACKETS * 192)
/* ...and the final samples of the fill ramp linearly to the first sample of
 * the real packet after the gap (~2.7 ms).  128 samples keeps even a full
 * int16 endpoint transition below the 600-count click-detector limit. */
#define C64_AUDIO_CONCEAL_RAMP_SAMPLES 128

enum c64_audio_seq_action {
    C64_AUDIO_SEQ_PLAY = 0,
    C64_AUDIO_SEQ_DROP,
    C64_AUDIO_SEQ_CONCEAL,
    C64_AUDIO_SEQ_RESYNC,
};

struct c64_audio_timeline {
    bool seq_set;      /* last_seq/packet_index are valid */
    uint16_t last_seq; /* seq of the last played packet */
    uint64_t packet_index;

    /* C64CLK-003 counters; reset with the timeline on stream restart. */
    uint64_t packets_lost; /* gap packets, whether concealed or resynced over */
    uint64_t concealed;    /* gap packets covered by concealment fill */
    uint64_t late_dropped; /* delta < 0 within the resync threshold */
    uint64_t duplicates;   /* delta == 0 */
    uint64_t resyncs;      /* timeline re-anchors */
};

void c64_audio_timeline_reset(struct c64_audio_timeline *tl);

/*
 * Advance the timeline with packet `seq`.
 *
 * `now_slot` is the wall-clock-derived packet slot,
 * (now_ns - stream_start_ns) / interval_ns, used only to re-anchor on RESYNC.
 *
 * On PLAY/CONCEAL/RESYNC, *out_index is the synthetic index at which the REAL
 * packet must be played. On CONCEAL, *out_gap is the number of packets to
 * synthesize first; concealed packet k (0-based) plays at index
 * (*out_index - *out_gap + k). On DROP, outputs are untouched and the packet
 * must not be played or recorded.
 */
enum c64_audio_seq_action c64_audio_timeline_advance(struct c64_audio_timeline *tl, uint16_t seq, uint64_t now_slot,
                                                     uint64_t *out_index, uint32_t *out_gap);

/* Endpoint samples for one concealment run. */
struct c64_audio_conceal_fill {
    int16_t last_left; /* final L/R samples of the packet before the gap */
    int16_t last_right;
    int16_t next_left; /* first L/R samples of the packet after the gap */
    int16_t next_right;
};

/*
 * Generate fill packet k of n (0-based) as 192 interleaved stereo 16-bit LE
 * frames (768 bytes, no seq header). Both splices are step-free: the fill
 * starts at the held last sample and its final samples ramp to the next real
 * sample; in between the held value fades toward 0 (see the FADE/RAMP
 * constants above).
 */
void c64_audio_conceal_fill_packet(const struct c64_audio_conceal_fill *fill, uint32_t k, uint32_t n, uint8_t out[768]);

#ifdef __cplusplus
}
#endif

#endif /* C64_AUDIO_TIMELINE_H */
