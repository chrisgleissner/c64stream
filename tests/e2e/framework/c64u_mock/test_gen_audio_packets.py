"""Unit tests for deterministic mock audio fixtures."""

import struct
import unittest

from framework.c64u_mock.gen_audio_packets import (
    SCALE500_NOTES,
    SAMPLE_RATES,
    SAMPLES_PER_PACKET,
    generate_scale500_packet,
    scale500_note_index,
    scale500_note_samples,
    scale500_sample,
)


class TestScale500AudioFixture(unittest.TestCase):
    """The listening fixture must have exact, click-free note boundaries."""

    def test_note_boundaries_are_exactly_500ms(self):
        sample_rate = SAMPLE_RATES["NTSC"]
        samples_per_note = scale500_note_samples(sample_rate)

        self.assertEqual(samples_per_note, 23970)
        for note_index in range(len(SCALE500_NOTES)):
            first = note_index * samples_per_note
            last = first + samples_per_note - 1
            self.assertEqual(scale500_note_index(first, sample_rate), note_index)
            self.assertEqual(scale500_note_index(last, sample_rate), note_index)
            self.assertEqual(scale500_note_index(last + 1, sample_rate),
                             (note_index + 1) % len(SCALE500_NOTES))

    def test_transitions_are_continuous_and_packet_is_stereo_pcm(self):
        sample_rate = SAMPLE_RATES["NTSC"]
        samples_per_note = scale500_note_samples(sample_rate)
        for note_index in range(1, len(SCALE500_NOTES)):
            boundary = note_index * samples_per_note
            step = abs(scale500_sample(boundary, sample_rate) -
                       scale500_sample(boundary - 1, sample_rate))
            self.assertLess(step, 6000)

        packet = generate_scale500_packet(0, sample_rate)
        self.assertEqual(len(packet), 770)
        self.assertEqual(struct.unpack_from("<H", packet)[0], 0)
        pcm = struct.unpack_from(f"<{SAMPLES_PER_PACKET * 2}h", packet, 2)
        self.assertTrue(all(left == right for left, right in zip(pcm[::2], pcm[1::2])))


if __name__ == "__main__":
    unittest.main()
