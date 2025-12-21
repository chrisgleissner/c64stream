#!/usr/bin/env python3
"""
C64 Stream - Packet Generator for E2E Testing
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Generates test packets following the C64 Ultimate stream specification.
Creates deterministic PAL and NTSC video/audio packets with verification pops
(A/V sync generator):
    - Video pop: pure white 50x50 square centered inside a permanently black
        80x80 area located at the lower-right corner of the C64 frame; instant on/off
    - Audio pop: pleasant, band-limited noise burst ("rushing water"-like), instant on/off,
        alternating speakers per pop (L, R, L, R, ...), starting with LEFT
    - Pop cadence: first pop at ~1000ms after start, then every 1000ms
    - Pop duration: 1 video frame
"""

import os
import struct
import numpy as np
import argparse
from pathlib import Path
import math


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
    Returns (frame_rate, frame_duration_ms, sync_period_ms, sync_duration_ms, sync_offset_ms)
    """
    if format_name == 'PAL':
        frame_rate = 50.125
    else:  # NTSC
        frame_rate = 59.826

    frame_duration_ms = 1000.0 / frame_rate
    sync_period_ms = 1000.0  # Pop every 1000ms
    sync_duration_ms = 1 * frame_duration_ms  # 1 frame duration (video pop)
    sync_offset_ms = 1000.0  # First pop at ~1000ms

    return frame_rate, frame_duration_ms, sync_period_ms, sync_duration_ms, sync_offset_ms


def is_sync_marker_active(frame_num, format_name):
    """
    Determine if the A/V sync marker (black rectangle + audio beep) should be active.
    Returns True if this frame should show the black rectangle.
    """
    _, frame_duration_ms, sync_period_ms, sync_duration_ms, sync_offset_ms = get_sync_timing_info(format_name)

    # Calculate frame timing - use precise timing aligned to sync boundaries
    time_in_test_ms = frame_num * frame_duration_ms
    time_since_offset = time_in_test_ms - sync_offset_ms
    if time_since_offset < 0:
        return False
    time_in_current_period = time_since_offset % sync_period_ms
    return time_in_current_period < sync_duration_ms

def generate_video_packet(frame_num, packet_num, width, height, packets_per_frame, format_name, total_test_duration_ms):
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

    # Sequence number should be a monotonically increasing 16-bit value across the whole stream,
    # not reset per frame. This helps consumers correctly detect ordering and frame boundaries.
    seq_num = (frame_num * packets_per_frame + packet_num) & 0xFFFF

    # Build header (12 bytes)
    header = struct.pack('<HHHHBBH',
                         seq_num,              # Sequence number (monotonic across frames)
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

    # Check if A/V sync pop should be active
    sync_active = is_sync_marker_active(frame_num, format_name)
    # Enforce schedule constraint: no pop in the last 1000ms of recording
    frame_rate, frame_duration_ms, *_ = get_sync_timing_info(format_name)
    frame_start_ms = frame_num * frame_duration_ms
    if frame_start_ms >= (total_test_duration_ms - 1000.0):
        sync_active = False

    # Define permanent black pop area: 80x80 at lower-right corner of C64 frame
    area_size = 80
    area_left = width - area_size
    area_right = width
    area_top = height - area_size
    area_bottom = height

    # Define white pop subregion: 50x50 centered within the black area
    pop_size = 50
    pop_left = area_left + (area_size - pop_size) // 2
    pop_right = pop_left + pop_size
    pop_top = area_top + (area_size - pop_size) // 2
    pop_bottom = pop_top + pop_size

    for line in range(LINES_PER_PACKET):
        for byte_idx in range(width // 2):  # 2 pixels per byte (4-bit color)
            pixel_line = line_num + line
            pixel_x = byte_idx * 2  # Each byte contains 2 pixels

            # Determine if current location is within the permanent black area
            in_black_area = (area_top <= pixel_line < area_bottom and
                             area_left <= pixel_x < area_right)

            # Determine if current location is within the white pop region
            in_white_pop = (sync_active and
                            pop_top <= pixel_line < pop_bottom and
                            pop_left <= pixel_x < pop_right)

            if in_white_pop:
                # White pop: set both pixels to VIC color 1 (white)
                payload[line * (width // 2) + byte_idx] = 0x11
            elif in_black_area:
                # Permanent black background for the pop area (VIC color 0)
                payload[line * (width // 2) + byte_idx] = 0x00
            elif pixel_line < 40 and byte_idx < 20:
                # Frame number marker pattern in top-left corner (first 40x40 pixels)
                # Show frame number modulo 16 as solid VIC color
                # This creates a solid block of color across the entire 40x40 area
                marker_color = frame_num % 16
                payload[line * (width // 2) + byte_idx] = (marker_color << 4) | marker_color
            else:
                # Rest of frame: diagonal, slowly moving lines
                # Create thin diagonal slanted lines by combining x and y, with a slow motion term.
                # Lines are 2 pixels wide every 32 diagonal steps; color cycles across 16 VIC colors.
                # Motion: shift by +1 pixel per frame along the diagonal direction.
                def diag_color(px):
                    # Thin diagonal stripes (in_stripe controlled by motion S),
                    # color tied to invariant diagonal index so color remains constant as it moves.
                    S = px + pixel_line + frame_num
                    stripe_period = 8
                    stripe_width = 2
                    in_stripe = (S % stripe_period) < stripe_width
                    if not in_stripe:
                        return 0
                    # Diagonal index invariant under motion along S: use (px - pixel_line)
                    diag_index = ( (px - pixel_line) // stripe_period ) % 16
                    return int(diag_index)

                c1 = diag_color(pixel_x)
                c2 = diag_color(pixel_x + 1)
                payload[line * (width // 2) + byte_idx] = (c2 << 4) | c1

    return header + bytes(payload)


def _one_pole_lowpass(x: np.ndarray, cutoff_hz: float, sample_rate: float) -> np.ndarray:
    """Simple one-pole low-pass filter."""
    if x.size == 0:
        return x
    rc = 1.0 / (2.0 * math.pi * cutoff_hz)
    dt = 1.0 / float(sample_rate)
    alpha = dt / (rc + dt)
    y = np.empty_like(x, dtype=np.float64)
    acc = float(x[0])
    y[0] = acc
    for n in range(1, x.size):
        acc = acc + alpha * (float(x[n]) - acc)
        y[n] = acc
    return y


def _one_pole_highpass(x: np.ndarray, cutoff_hz: float, sample_rate: float) -> np.ndarray:
    """Simple one-pole high-pass filter."""
    if x.size == 0:
        return x
    rc = 1.0 / (2.0 * math.pi * cutoff_hz)
    dt = 1.0 / float(sample_rate)
    alpha = rc / (rc + dt)
    y = np.empty_like(x, dtype=np.float64)
    prev_y = float(x[0])
    prev_x = float(x[0])
    y[0] = 0.0
    for n in range(1, x.size):
        yn = alpha * (prev_y + float(x[n]) - prev_x)
        y[n] = yn
        prev_y = yn
        prev_x = float(x[n])
    return y


def generate_audio_packet(audio_packet_num, sample_rate, total_test_duration_ms):
    """
    Generate a single audio packet following C64 Ultimate spec.

    Packet Structure:
    1. Header: Sequence number (16-bit LE)
    2. Payload: 192 stereo samples (16-bit signed LE, interleaved L/R)

    Audio Pattern: Continuous stream with 200Hz audio pop for 2 video frames every 1 second
    for A/V sync (A/V sync generator), with instant on/off (no ramp)
    """
    # Build header (2 bytes)
    header = struct.pack('<H', audio_packet_num)

    # Calculate timing - each audio packet represents exactly 192 samples at the given sample rate
    packet_duration_ms = (AUDIO_SAMPLES_PER_PACKET / sample_rate) * 1000
    time_in_test_ms = audio_packet_num * packet_duration_ms

    # Use unified sync timing to ensure perfect A/V alignment
    format_name = 'PAL' if sample_rate > 47960 else 'NTSC'
    _, frame_duration_ms, sync_period_ms, sync_duration_ms, sync_offset_ms = get_sync_timing_info(format_name)

    # Check if we're in a sync pop period using same calculation as video
    time_since_offset = time_in_test_ms - sync_offset_ms
    is_sync_pop = time_since_offset >= 0 and (time_since_offset % sync_period_ms) < sync_duration_ms
    # Enforce schedule constraint: no pop in the last 1000ms of recording
    if time_in_test_ms >= (total_test_duration_ms - 1000.0):
        is_sync_pop = False
    # Determine which pop index (0-based) we're in to alternate speakers L/R starting with LEFT
    pop_index = int(time_since_offset // sync_period_ms) if time_since_offset >= 0 else -1

    # Generate time array for this packet's 192 samples
    t = np.linspace(time_in_test_ms / 1000.0,
                    (time_in_test_ms + packet_duration_ms) / 1000.0,
                    AUDIO_SAMPLES_PER_PACKET, endpoint=False)

    if is_sync_pop:
        # Generate band-limited noise burst (pleasant, "rushing water"-like)
        # Deterministic seed per packet for reproducibility
        rng = np.random.RandomState(0xC64A ^ audio_packet_num)
        white = rng.randn(AUDIO_SAMPLES_PER_PACKET).astype(np.float64)
        # Band-limit: high-pass ~200 Hz to remove rumble; low-pass ~3 kHz to soften harshness
        noise = _one_pole_highpass(white, cutoff_hz=200.0, sample_rate=sample_rate)
        noise = _one_pole_lowpass(noise, cutoff_hz=3000.0, sample_rate=sample_rate)
        # Normalize and scale to comfortable level
        peak = np.max(np.abs(noise)) if np.max(np.abs(noise)) > 0 else 1.0
        noise = 0.55 * (noise / peak)
        audio_signal = (noise * 32767.0).astype(np.int16)
    else:
        # Silence between pops
        audio_signal = np.zeros(AUDIO_SAMPLES_PER_PACKET, dtype=np.int16)

    # Interleave left and right channels
    # During pops, alternate speakers per pop: even pop_index => LEFT, odd => RIGHT
    payload = np.empty(AUDIO_SAMPLES_PER_PACKET * 2, dtype=np.int16)
    if is_sync_pop and pop_index >= 0:
        if (pop_index % 2) == 0:
            # LEFT pop
            payload[0::2] = audio_signal   # Left
            payload[1::2] = 0              # Right
        else:
            # RIGHT pop
            payload[0::2] = 0              # Left
            payload[1::2] = audio_signal   # Right
    else:
        # Silence or non-pop portion of packet
        payload[0::2] = audio_signal
        payload[1::2] = audio_signal

    return header + payload.tobytes()


def generate_packets(output_dir, num_frames=30, formats=None):
    """
    Generate test packets for specified formats with A/V sync pops.
    """
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    target_formats = formats if formats else ['PAL', 'NTSC']

    for format_name in target_formats:
        fmt = VIDEO_FORMATS[format_name]
        video_dir = output_path / 'video' / format_name
        audio_dir = output_path / 'audio' / format_name
        video_dir.mkdir(parents=True, exist_ok=True)
        audio_dir.mkdir(parents=True, exist_ok=True)

        width = fmt['width']
        height = fmt['height']
        ppf = fmt['packets_per_frame']

        # Compute total test duration upfront (needed to suppress last-second pops)
        frame_duration_ms = 1000.0 / fmt['frame_rate']
        total_test_duration_ms = num_frames * frame_duration_ms

        # Generate video packets
        for frame_num in range(num_frames):
            for packet_num in range(ppf):
                packet_data = generate_video_packet(
                    frame_num, packet_num, width, height, ppf, format_name, total_test_duration_ms
                )
                packet_file = video_dir / f"video_{frame_num:04d}_{packet_num:04d}.bin"
                packet_file.write_bytes(packet_data)

        # Generate audio packets for total test duration
        audio_packet_duration_ms = (AUDIO_SAMPLES_PER_PACKET / fmt['audio_sample_rate']) * 1000.0
        total_audio_packets = int(total_test_duration_ms / audio_packet_duration_ms)

        for audio_packet_num in range(total_audio_packets):
            packet_data = generate_audio_packet(audio_packet_num, fmt['audio_sample_rate'], total_test_duration_ms)
            packet_file = audio_dir / f"audio_{audio_packet_num:04d}.bin"
            packet_file.write_bytes(packet_data)

        print(f"  ✅ {format_name}: Generated {num_frames*ppf} video packets and {total_audio_packets} audio packets")

    # Report disk usage
    total_size = 0
    for p in output_path.rglob('*.bin'):
        total_size += p.stat().st_size
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
