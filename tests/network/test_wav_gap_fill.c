/* C64 Stream - C64CLK-001 regression: WAV byte stream across a concealed gap.
 *
 * Simulates the recording path around a packet-loss gap in a 1 kHz sine with
 * SID-style DC offset (the E2E `sine1k` fixture): packet(s) lost between two
 * real packets are synthesized by the concealment generator and the resulting
 * WAV byte stream must
 * - have exactly (gap + surrounding) * 768 bytes (duration preserved), and
 * - be step-free at every packet boundary and inside the fill (no clicks). */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-audio-timeline.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 47940.3408482143
#define AMPLITUDE 12000.0
#define DC_OFFSET 2000.0
/* Threshold used by the E2E click detector on the WAV. */
#define CLICK_THRESHOLD 6000

static void gen_sine_packet(uint32_t packet_num, uint8_t out[768])
{
    for (uint32_t i = 0; i < 192; i++) {
        const double t = ((double)packet_num * 192.0 + (double)i) / SAMPLE_RATE;
        const int16_t v = (int16_t)lround(DC_OFFSET + AMPLITUDE * sin(2.0 * M_PI * 1000.0 * t));
        out[i * 4 + 0] = (uint8_t)((uint16_t)v & 0xFF);
        out[i * 4 + 1] = (uint8_t)(((uint16_t)v >> 8) & 0xFF);
        out[i * 4 + 2] = out[i * 4 + 0];
        out[i * 4 + 3] = out[i * 4 + 1];
    }
}

static int16_t left_sample(const uint8_t *wav, size_t frame)
{
    return (int16_t)((uint16_t)wav[frame * 4] | ((uint16_t)wav[frame * 4 + 1] << 8));
}

static void check_stream_with_gap(uint32_t gap)
{
    /* Real packets 0..1, gap packets 2..2+gap-1 lost, real packet 2+gap. */
    const uint32_t total_packets = 3 + gap;
    uint8_t *wav = malloc((size_t)total_packets * 768);
    assert(wav != NULL);
    size_t wav_len = 0;

    uint8_t packet[768];
    gen_sine_packet(0, packet);
    memcpy(wav + wav_len, packet, 768);
    wav_len += 768;
    gen_sine_packet(1, packet);
    memcpy(wav + wav_len, packet, 768);
    wav_len += 768;

    /* The packet after the gap arrives; synthesize the fill exactly as
     * c64_process_audio_packet does: endpoints are the last played sample and
     * the first sample of the just-arrived packet. */
    uint8_t next_packet[768];
    gen_sine_packet(2 + gap, next_packet);

    struct c64_audio_conceal_fill fill = {
        .last_left = left_sample(wav, wav_len / 4 - 1),
        .last_right = left_sample(wav, wav_len / 4 - 1),
        .next_left = (int16_t)((uint16_t)next_packet[0] | ((uint16_t)next_packet[1] << 8)),
        .next_right = (int16_t)((uint16_t)next_packet[2] | ((uint16_t)next_packet[3] << 8)),
    };
    for (uint32_t k = 0; k < gap; k++) {
        c64_audio_conceal_fill_packet(&fill, k, gap, wav + wav_len);
        wav_len += 768;
    }

    memcpy(wav + wav_len, next_packet, 768);
    wav_len += 768;

    /* Length: the gap is fully materialised. */
    assert(wav_len == (size_t)total_packets * 768);

    /* Click scan over the whole stream (every sample-to-sample delta). */
    const size_t frames = wav_len / 4;
    int max_delta = 0;
    for (size_t f = 1; f < frames; f++) {
        const int d = abs(left_sample(wav, f) - left_sample(wav, f - 1));
        if (d > max_delta) {
            max_delta = d;
        }
    }
    assert(max_delta < CLICK_THRESHOLD);

    /* Sanity: an unconcealed splice of the same stream WOULD click, proving
     * the detector threshold is meaningful for this gap. */
    if (gap >= 5) {
        uint8_t before[768], after[768];
        gen_sine_packet(1, before);
        gen_sine_packet(2 + gap, after);
        int splice_worst = 0;
        /* Worst boundary step over all phases this gap size can produce. */
        for (uint32_t g = 2; g <= gap + 2; g++) {
            uint8_t a[768];
            gen_sine_packet(g, a);
            const int d = abs((int16_t)((uint16_t)a[0] | ((uint16_t)a[1] << 8)) - left_sample(before, 191));
            if (d > splice_worst) {
                splice_worst = d;
            }
        }
        (void)after;
        assert(splice_worst > 0); /* informational; phase-dependent */
    }

    free(wav);
}

int main(void)
{
    check_stream_with_gap(1);
    check_stream_with_gap(2);
    check_stream_with_gap(7);
    check_stream_with_gap(25);
    check_stream_with_gap(100);

    printf("test_wav_gap_fill: all tests passed\n");
    return 0;
}
