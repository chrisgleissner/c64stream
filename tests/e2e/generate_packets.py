#!/usr/bin/env python3
"""
C64 Stream - Packet Generator for E2E Testing
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Generates test packets following the C64 Ultimate stream specification.
Creates deterministic PAL and NTSC video/audio packets with verification markers.
"""

import os
import struct
import numpy as np
import argparse
from pathlib import Path


# Video format specifications (from doc/c64-stream-spec.md)
VIDEO_FORMATS = {
    'PAL': {
        'width': 384,
        'height': 272,
        'packets_per_frame': 68,
        'frame_rate': 50.125,
        'audio_sample_rate': 47983,  # PAL: 47983 Hz per C64 Ultimate spec
    },
    'NTSC': {
        'width': 384,
        'height': 240,
        'packets_per_frame': 60,
        'frame_rate': 59.826,
        'audio_sample_rate': 47940,  # NTSC: 47940 Hz per C64 Ultimate spec
    }
}

# Protocol constants (from doc/c64-stream-spec.md)
VIDEO_PACKET_SIZE = 780  # 12 byte header + 768 byte payload
AUDIO_PACKET_SIZE = 770  # 2 byte header + 768 byte payload
VIDEO_HEADER_SIZE = 12
AUDIO_HEADER_SIZE = 2
LINES_PER_PACKET = 4
BITS_PER_PIXEL = 4
AUDIO_SAMPLES_PER_PACKET = 192  # Stereo samples


def get_sync_timing_info(format_name):
    """
    Get unified sync timing information for both audio and video.
    Returns (frame_rate, frame_duration_ms, sync_period_ms, sync_duration_ms)
    """
    if format_name == 'PAL':
        frame_rate = 50.125
    else:  # NTSC
        frame_rate = 59.826

    frame_duration_ms = 1000.0 / frame_rate
    sync_period_ms = 1000.0  # Sync marker every 1000ms
    sync_duration_ms = 4 * frame_duration_ms  # 4 frames duration

    return frame_rate, frame_duration_ms, sync_period_ms, sync_duration_ms


def is_sync_marker_active(frame_num, format_name):
    """
    Determine if the A/V sync marker (black rectangle + audio beep) should be active.
    Returns True if this frame should show the black rectangle.
    """
    _, frame_duration_ms, sync_period_ms, sync_duration_ms = get_sync_timing_info(format_name)

    # Calculate frame timing - use precise timing aligned to sync boundaries
    time_in_test_ms = frame_num * frame_duration_ms

    time_in_current_second = time_in_test_ms % sync_period_ms
    return time_in_current_second < sync_duration_ms

def generate_video_packet(frame_num, packet_num, width, height, packets_per_frame, format_name):
    """
    Generate a single video packet following C64 Ultimate spec.

    Packet Header (12 bytes):
    1. Sequence number (16-bit LE)
    2. Frame number (16-bit LE)
    3. Line number (16-bit LE, bit 15 = last packet of frame)
    4. Pixels per line (16-bit LE) = 384
    5. Lines per packet (8-bit) = 4
    6. Bits per pixel (8-bit) = 4
    7. Encoding type (16-bit) = 0 (uncompressed)
    """
    line_num = packet_num * LINES_PER_PACKET
    is_last_packet = (packet_num == packets_per_frame - 1)

    # Set bit 15 if this is the last packet of the frame
    line_num_with_flag = line_num | (0x8000 if is_last_packet else 0)

    # Build header (12 bytes)
    header = struct.pack('<HHHHBBH',
                         packet_num,           # Sequence number
                         frame_num,            # Frame number
                         line_num_with_flag,   # Line number with last packet flag
                         width,                # Pixels per line
                         LINES_PER_PACKET,     # Lines per packet
                         BITS_PER_PIXEL,       # Bits per pixel
                         0)                    # Encoding type (0 = uncompressed)

    # Generate deterministic test pattern payload (768 bytes)
    # Use frame number as the marker pattern in the top-left block
    # This allows verification that frames are in correct order
    payload = bytearray(768)

    # Check if A/V sync marker should be active
    sync_active = is_sync_marker_active(frame_num, format_name)

    # Calculate center coordinates for 100x100 black rectangle
    # Center should be based on full frame dimensions, not current packet
    frame_center_x = width // 2
    frame_center_y = height // 2
    rect_size = 100
    rect_left = frame_center_x - rect_size // 2
    rect_right = frame_center_x + rect_size // 2
    rect_top = frame_center_y - rect_size // 2
    rect_bottom = frame_center_y + rect_size // 2

    for line in range(LINES_PER_PACKET):
        for byte_idx in range(width // 2):  # 2 pixels per byte (4-bit color)
            pixel_line = line_num + line
            pixel_x = byte_idx * 2  # Each byte contains 2 pixels

            # Check if we're in the sync marker rectangle (100x100 black square in center)
            in_sync_rect = (sync_active and
                          rect_top <= pixel_line < rect_bottom and
                          rect_left <= pixel_x < rect_right)

            if in_sync_rect:
                # Black rectangle for A/V sync (VIC color 0 = black)
                payload[line * (width // 2) + byte_idx] = 0x00  # Both pixels black
            elif pixel_line < 32 and byte_idx < 16:
                # Frame number marker pattern in top-left corner (first 32x32 pixels)
                # Show frame number modulo 16 as solid VIC color
                # This should create a solid block of color for the entire 32x32 area
                marker_color = frame_num % 16
                payload[line * (width // 2) + byte_idx] = (marker_color << 4) | marker_color
            else:
                # Rest of frame: simple pattern based on position
                color1 = (byte_idx + pixel_line + frame_num) % 16
                color2 = (byte_idx + pixel_line + frame_num + 1) % 16
                payload[line * (width // 2) + byte_idx] = (color2 << 4) | color1

    return header + bytes(payload)


def generate_audio_packet(audio_packet_num, sample_rate, total_test_duration_ms):
    """
    Generate a single audio packet following C64 Ultimate spec.

    Packet Structure:
    1. Header: Sequence number (16-bit LE)
    2. Payload: 192 stereo samples (16-bit signed LE, interleaved L/R)

    Audio Pattern: Continuous stream with 100Hz beep for 4 video frames every 1 second for A/V sync
    """
    # Build header (2 bytes)
    header = struct.pack('<H', audio_packet_num)

    # Calculate timing - each audio packet represents exactly 192 samples at the given sample rate
    packet_duration_ms = (AUDIO_SAMPLES_PER_PACKET / sample_rate) * 1000
    time_in_test_ms = audio_packet_num * packet_duration_ms

    # Use unified sync timing to ensure perfect A/V alignment
    format_name = 'PAL' if sample_rate > 47960 else 'NTSC'
    _, frame_duration_ms, sync_period_ms, sync_duration_ms = get_sync_timing_info(format_name)

    # Check if we're in a sync beep period using same calculation as video
    time_in_current_second = time_in_test_ms % sync_period_ms
    is_sync_beep = time_in_current_second < sync_duration_ms

    # Generate time array for this packet's 192 samples
    t = np.linspace(time_in_test_ms / 1000.0,
                    (time_in_test_ms + packet_duration_ms) / 1000.0,
                    AUDIO_SAMPLES_PER_PACKET, endpoint=False)

    if is_sync_beep:
        # Generate 200Hz sine wave during sync periods for clear distinction
        tone = np.sin(2 * np.pi * 200 * t)

        # Apply smooth ramp-up (1ms) and ramp-down (5ms) envelope
        envelope = np.ones_like(tone)

        # Ramp timing in milliseconds
        ramp_up_ms = 1.0
        ramp_down_ms = 5.0

        # Calculate precise position within the current sync beep period
        # Use exact same boundary calculation as video sync markers
        beep_start_time_ms = (time_in_test_ms // sync_period_ms) * sync_period_ms

        for i in range(len(envelope)):
            # Calculate precise sample time within the sync period
            sample_time_ms = time_in_test_ms + (i / sample_rate) * 1000
            sample_time_in_beep = sample_time_ms - beep_start_time_ms

            if sample_time_in_beep < 0:
                # Before beep starts
                envelope[i] = 0.0
            elif sample_time_in_beep < ramp_up_ms:
                # Ramp up phase
                envelope[i] = sample_time_in_beep / ramp_up_ms
            elif sample_time_in_beep >= (sync_duration_ms - ramp_down_ms):
                # Ramp down phase
                time_to_end = sync_duration_ms - sample_time_in_beep
                envelope[i] = max(0.0, time_to_end / ramp_down_ms)
            elif sample_time_in_beep < sync_duration_ms:
                # Full volume phase
                envelope[i] = 1.0
            else:
                # After beep ends
                envelope[i] = 0.0

        # Apply envelope and convert to 16-bit signed
        audio_signal = (tone * envelope * 1.0 * 32767).astype(np.int16)
    else:
        # Silence between sync beeps (realistic - Ultimate continues streaming)
        audio_signal = np.zeros(AUDIO_SAMPLES_PER_PACKET, dtype=np.int16)

    # Interleave left and right channels (same signal for both)
    payload = np.empty(AUDIO_SAMPLES_PER_PACKET * 2, dtype=np.int16)
    payload[0::2] = audio_signal  # Left channel
    payload[1::2] = audio_signal  # Right channel

    return header + payload.tobytes()


def generate_packets(output_dir, num_frames=30, formats=None):
    """
    Generate test packets for all specified formats.

    Args:
        output_dir: Base directory for output packets
        num_frames: Number of frames to generate per format
        formats: List of formats to generate ('PAL', 'NTSC'), or None for all
    """
    if formats is None:
        formats = ['PAL', 'NTSC']

    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    for format_name in formats:
        if format_name not in VIDEO_FORMATS:
            print(f"Warning: Unknown format '{format_name}', skipping")
            continue

        fmt = VIDEO_FORMATS[format_name]
        print(f"\nGenerating {format_name} packets ({num_frames} frames)...")

        # Create format-specific directories
        video_dir = output_path / 'video' / format_name
        audio_dir = output_path / 'audio' / format_name
        video_dir.mkdir(parents=True, exist_ok=True)
        audio_dir.mkdir(parents=True, exist_ok=True)

        # Generate video packets
        print(f"  Video: {fmt['packets_per_frame']} packets per frame")
        for frame_num in range(num_frames):
            for packet_num in range(fmt['packets_per_frame']):
                packet_data = generate_video_packet(
                    frame_num, packet_num, fmt['width'],
                    fmt['height'], fmt['packets_per_frame'], format_name
                )

                # Write packet to file
                packet_file = video_dir / f"frame_{frame_num:04d}_pkt_{packet_num:03d}.bin"
                packet_file.write_bytes(packet_data)

        total_video_packets = num_frames * fmt['packets_per_frame']
        print(f"    Generated {total_video_packets} video packets")

        # Generate audio packets with realistic timing per C64 Ultimate spec
        print(f"  Audio: {fmt['audio_sample_rate']} Hz sample rate")

        # Calculate total test duration and required audio packets
        frame_duration_ms = 1000.0 / fmt['frame_rate']
        total_test_duration_ms = num_frames * frame_duration_ms
        audio_packet_duration_ms = (AUDIO_SAMPLES_PER_PACKET / fmt['audio_sample_rate']) * 1000
        # Use exact calculation without extra packet - this ensures precise timing alignment
        total_audio_packets = int(total_test_duration_ms / audio_packet_duration_ms)

        for audio_packet_num in range(total_audio_packets):
            packet_data = generate_audio_packet(audio_packet_num, fmt['audio_sample_rate'], total_test_duration_ms)

            # Write packet to file with sequential numbering
            packet_file = audio_dir / f"audio_{audio_packet_num:04d}.bin"
            packet_file.write_bytes(packet_data)

        print(f"    Generated {total_audio_packets} audio packets ({audio_packet_duration_ms:.1f}ms/packet)")

        # Calculate disk space usage for this format
        format_size = 0
        for video_file in video_dir.glob("*.bin"):
            format_size += video_file.stat().st_size
        for audio_file in audio_dir.glob("*.bin"):
            format_size += audio_file.stat().st_size

        print(f"  💾 {format_name} disk usage: {format_size:,} bytes ({format_size / 1024 / 1024:.1f} MB)")

    # Calculate total disk space usage
    total_size = 0
    for format_dir in output_path.glob("*/*"):
        if format_dir.is_dir():
            for packet_file in format_dir.glob("*.bin"):
                total_size += packet_file.stat().st_size

    print(f"\n✅ Packet generation complete: {output_dir}")
    print(f"💾 Total disk usage: {total_size:,} bytes ({total_size / 1024 / 1024:.1f} MB)")


def main():
    parser = argparse.ArgumentParser(
        description='Generate C64 Ultimate test packets for e2e testing',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Generate 30 frames for both PAL and NTSC
  %(prog)s

  # Generate 60 frames for PAL only
  %(prog)s --frames 60 --format PAL

  # Generate to custom directory
  %(prog)s --output /tmp/test_packets
        """
    )
    parser.add_argument('--output', '-o', default='test_packets',
                        help='Output directory (default: test_packets)')
    parser.add_argument('--frames', '-f', type=int, default=30,
                        help='Number of frames to generate (default: 30)')
    parser.add_argument('--format', choices=['PAL', 'NTSC'], action='append',
                        dest='formats',
                        help='Format(s) to generate (can specify multiple times, default: both)')

    args = parser.parse_args()

    generate_packets(args.output, args.frames, args.formats)


if __name__ == '__main__':
    main()
