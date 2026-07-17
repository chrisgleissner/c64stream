/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-audio-timeline.h"

#include <string.h>

void c64_audio_timeline_reset(struct c64_audio_timeline *tl)
{
    if (!tl) {
        return;
    }
    memset(tl, 0, sizeof(*tl));
}

enum c64_audio_seq_action c64_audio_timeline_advance(struct c64_audio_timeline *tl, uint16_t seq, uint64_t now_slot,
                                                     uint64_t *out_index, uint32_t *out_gap)
{
    if (!tl || !out_index || !out_gap) {
        return C64_AUDIO_SEQ_DROP;
    }

    if (!tl->seq_set) {
        tl->seq_set = true;
        tl->last_seq = seq;
        tl->packet_index = 0;
        *out_index = 0;
        *out_gap = 0;
        return C64_AUDIO_SEQ_PLAY;
    }

    /* int16_t cast handles 16-bit wraparound (e.g. 65530 -> 3 is +9). */
    const int16_t delta = (int16_t)(seq - tl->last_seq);

    if (delta == 1) {
        tl->packet_index += 1;
        tl->last_seq = seq;
        *out_index = tl->packet_index;
        *out_gap = 0;
        return C64_AUDIO_SEQ_PLAY;
    }

    if (delta <= 0 && delta > -C64_AUDIO_RESYNC_THRESHOLD) {
        /* Duplicate or too-late packet: never play stale samples, never
         * advance the timeline (the old +1 advance shifted A/V sync by 4 ms
         * per occurrence). */
        if (delta == 0) {
            tl->duplicates += 1;
        } else {
            tl->late_dropped += 1;
        }
        return C64_AUDIO_SEQ_DROP;
    }

    if (delta > 1 && (uint32_t)(delta - 1) <= C64_AUDIO_WAV_FILL_MAX_PACKETS) {
        const uint32_t gap = (uint32_t)delta - 1;
        tl->packets_lost += gap;
        tl->concealed += gap;
        tl->packet_index += (uint64_t)delta;
        tl->last_seq = seq;
        *out_index = tl->packet_index;
        *out_gap = gap;
        return C64_AUDIO_SEQ_CONCEAL;
    }

    /* Stream discontinuity: a huge forward jump (device paused/counter jump)
     * or a big backward jump (device restart). Keep stream_start_ns; re-anchor
     * the index so the next timestamp is monotonic and lands where wall time
     * says the stream is now. */
    if (delta > 1) {
        tl->packets_lost += (uint32_t)delta - 1;
    }
    tl->resyncs += 1;
    uint64_t next_index = tl->packet_index + 1;
    if (now_slot > next_index) {
        next_index = now_slot;
    }
    tl->packet_index = next_index;
    tl->last_seq = seq;
    *out_index = tl->packet_index;
    *out_gap = 0;
    return C64_AUDIO_SEQ_RESYNC;
}

/* Held value at global fill-sample position s: linear fade from the last real
 * sample toward 0 across FADE_SAMPLES, then silence. */
static inline int32_t conceal_held_value(int32_t last, uint64_t s)
{
    if (s >= C64_AUDIO_CONCEAL_FADE_SAMPLES) {
        return 0;
    }
    return (int32_t)((int64_t)last * (int64_t)(C64_AUDIO_CONCEAL_FADE_SAMPLES - s) / C64_AUDIO_CONCEAL_FADE_SAMPLES);
}

void c64_audio_conceal_fill_packet(const struct c64_audio_conceal_fill *fill, uint32_t k, uint32_t n, uint8_t out[768])
{
    if (!fill || !out || n == 0 || k >= n) {
        if (out) {
            memset(out, 0, 768);
        }
        return;
    }

    const uint64_t total = (uint64_t)n * 192;
    const uint64_t ramp_len = (total < C64_AUDIO_CONCEAL_RAMP_SAMPLES) ? total : C64_AUDIO_CONCEAL_RAMP_SAMPLES;
    const uint64_t ramp_start = total - ramp_len;

    for (uint32_t i = 0; i < 192; i++) {
        const uint64_t s = (uint64_t)k * 192 + i;
        int32_t left = conceal_held_value(fill->last_left, s);
        int32_t right = conceal_held_value(fill->last_right, s);

        if (s >= ramp_start) {
            /* Crossfade the held value into the first sample of the real
             * packet after the gap, so the exit splice is step-free. */
            const int64_t pos = (int64_t)(s - ramp_start) + 1;
            left += (int32_t)(((int64_t)fill->next_left - left) * pos / (int64_t)ramp_len);
            right += (int32_t)(((int64_t)fill->next_right - right) * pos / (int64_t)ramp_len);
        }

        out[i * 4 + 0] = (uint8_t)((uint16_t)(int16_t)left & 0xFF);
        out[i * 4 + 1] = (uint8_t)(((uint16_t)(int16_t)left >> 8) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((uint16_t)(int16_t)right & 0xFF);
        out[i * 4 + 3] = (uint8_t)(((uint16_t)(int16_t)right >> 8) & 0xFF);
    }
}
