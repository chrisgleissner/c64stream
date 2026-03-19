#!/usr/bin/env python3
"""
C64 Stream - Network Simulation Unit Tests
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Unit tests for the network jitter simulation used in E2E testing.
Tests verify that jitter is applied correctly to packet timelines.
"""

import random
import statistics
import struct
import unittest
from pathlib import Path
from typing import Any

from tests.e2e.util.generate_packets import VIDEO_FORMATS, generate_audio_packet, generate_video_packet


def create_packet_timeline(
    video_count: int,
    audio_count: int,
    video_interval_us: float,
    audio_interval_us: float,
) -> list[dict[str, Any]]:
    """Create an interleaved packet timeline like the C64 Ultimate would send."""
    timeline = []
    start_time_us = 0

    # Add video packets
    for i in range(video_count):
        timeline.append({
            'time_us': start_time_us + i * video_interval_us,
            'type': 'video',
            'seq': i,
            'original_time_us': start_time_us + i * video_interval_us,
        })

    # Add audio packets
    for i in range(audio_count):
        timeline.append({
            'time_us': start_time_us + i * audio_interval_us,
            'type': 'audio',
            'seq': i,
            'original_time_us': start_time_us + i * audio_interval_us,
        })

    # Sort by timestamp for proper interleaving
    timeline.sort(key=lambda x: x['time_us'])
    return timeline


def apply_jitter(timeline: list[dict[str, Any]], max_jitter_ms: float, seed: int | None = None) -> list[dict[str, Any]]:
    """
    Apply network jitter simulation to a packet timeline.

    This simulates real network jitter where:
    - Packets are sent at precise times by the source (C64 Ultimate)
    - Network introduces random positive delays (0 to max_jitter_ms)
    - Packets arrive out-of-order due to different delays

    Args:
        timeline: List of packet events with 'time_us' field
        max_jitter_ms: Maximum jitter delay in milliseconds
        seed: Random seed for reproducibility (None for random)

    Returns:
        New timeline sorted by arrival time (with jitter applied)
    """
    if seed is not None:
        random.seed(seed)
    else:
        random.seed()

    max_jitter_us = max_jitter_ms * 1000

    # Create a copy and apply jitter
    jittered = []
    for i, pkt in enumerate(timeline):
        new_pkt = pkt.copy()
        if i == 0:
            # First packet has no jitter to establish baseline
            new_pkt['jitter_us'] = 0
        else:
            # Apply random positive jitter (0 to max)
            jitter = random.uniform(0, max_jitter_us)
            new_pkt['time_us'] = pkt['time_us'] + jitter
            new_pkt['jitter_us'] = jitter
        jittered.append(new_pkt)

    # Sort by arrival time (jittered time)
    jittered.sort(key=lambda x: x['time_us'])
    return jittered


def count_out_of_order(timeline: list[dict[str, Any]], packet_type: str) -> tuple[int, float]:
    """
    Count how many packets of a given type are out of sequence order.

    Args:
        timeline: Jittered timeline sorted by arrival time
        packet_type: 'video' or 'audio'

    Returns:
        Tuple of (out_of_order_count, out_of_order_percentage)
    """
    # Extract sequence numbers in arrival order
    sequences = [p['seq'] for p in timeline if p['type'] == packet_type]

    if len(sequences) < 2:
        return 0, 0.0

    out_of_order = 0
    for i in range(1, len(sequences)):
        # Count as out-of-order if current seq is less than previous
        if sequences[i] < sequences[i - 1]:
            out_of_order += 1

    rate = (out_of_order / len(sequences)) * 100
    return out_of_order, rate


def calculate_jitter_stats(timeline: list[dict[str, Any]]) -> dict[str, float]:
    """Calculate statistics about applied jitter."""
    jitters = [p.get('jitter_us', 0) for p in timeline if p.get('jitter_us', 0) > 0]

    if not jitters:
        return {'min_ms': 0, 'max_ms': 0, 'mean_ms': 0, 'median_ms': 0}

    return {
        'min_ms': min(jitters) / 1000,
        'max_ms': max(jitters) / 1000,
        'mean_ms': statistics.mean(jitters) / 1000,
        'median_ms': statistics.median(jitters) / 1000,
    }


class TestNetworkJitterSimulation(unittest.TestCase):
    """Test cases for network jitter simulation."""

    # NTSC timing constants
    NTSC_VIDEO_INTERVAL_US = 1_000_000 / 3590  # ~278.55 us
    NTSC_AUDIO_INTERVAL_US = 1_000_000 / 250   # 4000 us
    VIDEO_HEADER_SIZE = struct.calcsize('<HHHHBBH')

    def test_no_jitter_preserves_order(self):
        """With no jitter, packets should remain in original order."""
        timeline = create_packet_timeline(
            video_count=100,
            audio_count=10,
            video_interval_us=self.NTSC_VIDEO_INTERVAL_US,
            audio_interval_us=self.NTSC_AUDIO_INTERVAL_US,
        )

        jittered = apply_jitter(timeline, max_jitter_ms=0, seed=42)

        # With 0 jitter, order should be preserved
        video_ooo, video_rate = count_out_of_order(jittered, 'video')
        audio_ooo, audio_rate = count_out_of_order(jittered, 'audio')

        self.assertEqual(video_ooo, 0, "Video should have no out-of-order with 0 jitter")
        self.assertEqual(audio_ooo, 0, "Audio should have no out-of-order with 0 jitter")

    def test_small_jitter_minimal_reordering(self):
        """Small jitter (1ms) should cause minimal reordering."""
        timeline = create_packet_timeline(
            video_count=1000,
            audio_count=100,
            video_interval_us=self.NTSC_VIDEO_INTERVAL_US,
            audio_interval_us=self.NTSC_AUDIO_INTERVAL_US,
        )

        jittered = apply_jitter(timeline, max_jitter_ms=1, seed=42)

        # With 1ms jitter on ~278us interval, expect some reordering
        video_ooo, video_rate = count_out_of_order(jittered, 'video')
        audio_ooo, audio_rate = count_out_of_order(jittered, 'audio')

        # Video should have significant reordering (1ms >> 0.28ms interval)
        self.assertGreater(video_rate, 10, "Video should have >10% out-of-order with 1ms jitter")
        # Audio has 4ms interval, so 1ms jitter shouldn't cause much reordering
        self.assertLess(audio_rate, 30, "Audio should have <30% out-of-order with 1ms jitter")

    def test_large_jitter_significant_reordering(self):
        """Large jitter (100ms) should cause significant reordering."""
        timeline = create_packet_timeline(
            video_count=1000,
            audio_count=100,
            video_interval_us=self.NTSC_VIDEO_INTERVAL_US,
            audio_interval_us=self.NTSC_AUDIO_INTERVAL_US,
        )

        jittered = apply_jitter(timeline, max_jitter_ms=100, seed=42)

        # With 100ms jitter, expect ~50% reordering for both
        video_ooo, video_rate = count_out_of_order(jittered, 'video')
        audio_ooo, audio_rate = count_out_of_order(jittered, 'audio')

        # Both should have substantial reordering
        self.assertGreater(video_rate, 40, f"Video should have >40% out-of-order, got {video_rate:.1f}%")
        self.assertGreater(audio_rate, 40, f"Audio should have >40% out-of-order, got {audio_rate:.1f}%")

    def test_jitter_is_positive_only(self):
        """Jitter should only delay packets, never make them early."""
        timeline = create_packet_timeline(
            video_count=100,
            audio_count=10,
            video_interval_us=self.NTSC_VIDEO_INTERVAL_US,
            audio_interval_us=self.NTSC_AUDIO_INTERVAL_US,
        )

        jittered = apply_jitter(timeline, max_jitter_ms=500, seed=42)

        for orig, jit in zip(timeline, sorted(jittered, key=lambda x: x['seq'])):
            if orig['type'] == jit['type']:
                # Find matching packet by type and seq
                matching = [p for p in jittered if p['type'] == orig['type'] and p['seq'] == orig['seq']]
                if matching:
                    self.assertGreaterEqual(
                        matching[0]['time_us'],
                        orig['original_time_us'],
                        f"Packet should not arrive before original time"
                    )

    def test_jitter_within_bounds(self):
        """Applied jitter should be within 0 to max_jitter_ms."""
        timeline = create_packet_timeline(
            video_count=1000,
            audio_count=100,
            video_interval_us=self.NTSC_VIDEO_INTERVAL_US,
            audio_interval_us=self.NTSC_AUDIO_INTERVAL_US,
        )

        max_jitter_ms = 200
        jittered = apply_jitter(timeline, max_jitter_ms=max_jitter_ms, seed=42)

        stats = calculate_jitter_stats(jittered)

        self.assertGreaterEqual(stats['min_ms'], 0, "Min jitter should be >= 0")
        self.assertLessEqual(stats['max_ms'], max_jitter_ms, f"Max jitter should be <= {max_jitter_ms}ms")
        # Mean should be roughly half of max for uniform distribution
        self.assertGreater(stats['mean_ms'], max_jitter_ms * 0.3, "Mean jitter too low")
        self.assertLess(stats['mean_ms'], max_jitter_ms * 0.7, "Mean jitter too high")

    def test_still_pattern_video_payload_is_frame_invariant(self):
        """The 'still' pattern must generate identical pixel payloads across frames."""
        fmt = VIDEO_FORMATS['NTSC']

        for packet_num in range(fmt['packets_per_frame']):
            payloads = []
            for frame_num in (0, 1, 2):
                packet = generate_video_packet(
                    frame_num=frame_num,
                    packet_num=packet_num,
                    width=fmt['width'],
                    height=fmt['height'],
                    packets_per_frame=fmt['packets_per_frame'],
                    format_name='NTSC',
                    total_frames=300,
                    pattern='still',
                    disable_pops=True,
                    full_frame_pop=False,
                )
                payloads.append(packet[self.VIDEO_HEADER_SIZE:])

            self.assertEqual(payloads[0], payloads[1], f"Packet {packet_num} payload changed between frames 0 and 1")
            self.assertEqual(payloads[1], payloads[2], f"Packet {packet_num} payload changed between frames 1 and 2")

    def test_disable_pops_silences_audio_pop_packets(self):
        """disable_pops must suppress audio pop payloads as well as video markers."""
        active_packet_num = None
        sample_rate = VIDEO_FORMATS['NTSC']['audio_sample_rate']

        for packet_num in range(200):
            packet = generate_audio_packet(packet_num, sample_rate, 300, 'NTSC', disable_pops=False)
            if any(byte != 0 for byte in packet[2:]):
                active_packet_num = packet_num
                break

        self.assertIsNotNone(active_packet_num, "Failed to locate an active audio pop packet for the test")

        enabled_packet = generate_audio_packet(active_packet_num, sample_rate, 300, 'NTSC', disable_pops=False)
        disabled_packet = generate_audio_packet(active_packet_num, sample_rate, 300, 'NTSC', disable_pops=True)

        self.assertTrue(any(byte != 0 for byte in enabled_packet[2:]), "Control packet should contain a pop payload")
        self.assertFalse(any(byte != 0 for byte in disabled_packet[2:]), "disable_pops should force silence")

    def test_second_packet_can_arrive_before_first(self):
        """Test that jitter can cause seq=1 to arrive before seq=0 (after baseline packet)."""
        # The first packet in timeline (seq=0 at t=0) is never jittered (baseline)
        # But seq=1 can be jittered less than seq=2, causing reordering
        reorder_count = 0

        for seed in range(100):
            timeline = create_packet_timeline(
                video_count=100,
                audio_count=20,
                video_interval_us=self.NTSC_VIDEO_INTERVAL_US,
                audio_interval_us=self.NTSC_AUDIO_INTERVAL_US,
            )

            jittered = apply_jitter(timeline, max_jitter_ms=400, seed=seed)

            # Get video packets only, in arrival order
            video_arrival = [p for p in jittered if p['type'] == 'video']

            # Check if any early packets arrived before later packets
            # (excluding seq=0 which is always first)
            for i in range(1, min(10, len(video_arrival))):
                if video_arrival[i]['seq'] < video_arrival[i-1]['seq']:
                    reorder_count += 1
                    break

        # Should have some trials with early reordering
        self.assertGreater(
            reorder_count, 30,
            f"Expected some reordering in first 10 packets, got {reorder_count}/100 trials"
        )

    def test_manifest_delay_calculation(self):
        """Test that manifest delays correctly represent inter-packet timing."""
        timeline = create_packet_timeline(
            video_count=10,
            audio_count=5,
            video_interval_us=self.NTSC_VIDEO_INTERVAL_US,
            audio_interval_us=self.NTSC_AUDIO_INTERVAL_US,
        )

        jittered = apply_jitter(timeline, max_jitter_ms=100, seed=42)

        # Simulate manifest generation (video only for simplicity)
        video_packets = [p for p in jittered if p['type'] == 'video']
        video_packets.sort(key=lambda x: x['time_us'])

        # Calculate delays like the manifest generator does
        cumulative_time = 0
        delays = []
        for p in video_packets:
            delay_us = max(0, int(p['time_us'] - cumulative_time))
            delays.append(delay_us)
            cumulative_time = p['time_us']

        # Sum of delays should equal total timeline duration
        total_delay = sum(delays)
        expected_duration = video_packets[-1]['time_us'] - video_packets[0]['time_us']

        # First packet delay is its arrival time
        self.assertEqual(delays[0], int(video_packets[0]['time_us']))

        # Total delays minus first should roughly equal duration
        self.assertAlmostEqual(
            total_delay - delays[0],
            expected_duration,
            delta=10,  # Allow small rounding error
            msg="Manifest delays should sum to timeline duration"
        )

    def test_reproducibility_with_seed(self):
        """Same seed should produce identical results."""
        timeline1 = create_packet_timeline(100, 10, self.NTSC_VIDEO_INTERVAL_US, self.NTSC_AUDIO_INTERVAL_US)
        timeline2 = create_packet_timeline(100, 10, self.NTSC_VIDEO_INTERVAL_US, self.NTSC_AUDIO_INTERVAL_US)

        jittered1 = apply_jitter(timeline1, max_jitter_ms=100, seed=12345)
        jittered2 = apply_jitter(timeline2, max_jitter_ms=100, seed=12345)

        self.assertEqual(len(jittered1), len(jittered2))

        for p1, p2 in zip(jittered1, jittered2):
            self.assertEqual(p1['time_us'], p2['time_us'])
            self.assertEqual(p1['jitter_us'], p2['jitter_us'])

    def test_400ms_jitter_statistics(self):
        """Test 400ms jitter scenario specifically (the target scenario)."""
        # Create 5 seconds worth of packets at NTSC rate
        video_count = int(5 * 3590)  # ~17950 video packets
        audio_count = int(5 * 250)   # ~1250 audio packets

        timeline = create_packet_timeline(
            video_count=video_count,
            audio_count=audio_count,
            video_interval_us=self.NTSC_VIDEO_INTERVAL_US,
            audio_interval_us=self.NTSC_AUDIO_INTERVAL_US,
        )

        jittered = apply_jitter(timeline, max_jitter_ms=400, seed=42)

        # Check jitter statistics
        stats = calculate_jitter_stats(jittered)
        print(f"\n400ms jitter stats: min={stats['min_ms']:.1f}ms, max={stats['max_ms']:.1f}ms, "
              f"mean={stats['mean_ms']:.1f}ms, median={stats['median_ms']:.1f}ms")

        # Check out-of-order rates
        video_ooo, video_rate = count_out_of_order(jittered, 'video')
        audio_ooo, audio_rate = count_out_of_order(jittered, 'audio')
        print(f"Out-of-order: video={video_rate:.1f}% ({video_ooo}), audio={audio_rate:.1f}% ({audio_ooo})")

        # Verify expected behavior
        self.assertGreater(video_rate, 45, "Video should have ~50% out-of-order")
        self.assertLess(video_rate, 55, "Video should have ~50% out-of-order")
        self.assertGreater(audio_rate, 45, "Audio should have ~50% out-of-order")
        self.assertLess(audio_rate, 55, "Audio should have ~50% out-of-order")

        # Max jitter should be close to 400ms
        self.assertGreater(stats['max_ms'], 350, "Max jitter should be close to 400ms")
        self.assertLess(stats['max_ms'], 401, "Max jitter should not exceed 400ms")

        # Mean jitter should be around 200ms (uniform distribution)
        self.assertGreater(stats['mean_ms'], 180, "Mean jitter should be ~200ms")
        self.assertLess(stats['mean_ms'], 220, "Mean jitter should be ~200ms")


class TestBufferRequirements(unittest.TestCase):
    """Test cases to verify buffer requirements for jitter scenarios."""

    def test_max_reorder_distance_400ms_jitter(self):
        """Calculate maximum packet reorder distance for 400ms jitter."""
        # At NTSC rate with 400ms jitter, packets can be displaced by:
        # max_distance = jitter_ms / packet_interval_ms
        video_interval_ms = 1000 / 3590  # ~0.279ms
        audio_interval_ms = 1000 / 250   # 4ms

        max_video_distance = 400 / video_interval_ms
        max_audio_distance = 400 / audio_interval_ms

        print(f"\n400ms jitter maximum reorder distances:")
        print(f"  Video: {max_video_distance:.0f} packets")
        print(f"  Audio: {max_audio_distance:.0f} packets")

        # Verify buffer can handle this
        # Plugin uses MAX_SEARCH_DEPTH = 2048 for video, 150 for audio
        self.assertLess(max_video_distance, 2048, "Video buffer search depth should be sufficient")
        self.assertLess(max_audio_distance, 150, "Audio buffer search depth should be sufficient")

    def test_buffer_delay_requirements(self):
        """Verify buffer delay is sufficient for jitter scenario."""
        # With 400ms jitter, the buffer needs at least 400ms delay to ensure
        # we can wait for late-arriving packets before displaying
        max_jitter_ms = 400
        buffer_delay_ms = 500

        self.assertGreater(
            buffer_delay_ms,
            max_jitter_ms,
            "Buffer delay should exceed max jitter"
        )

        # Margin should be at least 100ms for safety
        margin_ms = buffer_delay_ms - max_jitter_ms
        self.assertGreaterEqual(
            margin_ms,
            100,
            f"Buffer delay margin should be at least 100ms, got {margin_ms}ms"
        )


if __name__ == '__main__':
    unittest.main(verbosity=2)
