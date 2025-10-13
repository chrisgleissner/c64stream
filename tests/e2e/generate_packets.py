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
        'audio_sample_rate': 47983,
    },
    'NTSC': {
        'width': 384,
        'height': 240,
        'packets_per_frame': 60,
        'frame_rate': 59.826,
        'audio_sample_rate': 47940,
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


def generate_video_packet(frame_num, packet_num, width, height, packets_per_frame):
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
    
    for line in range(LINES_PER_PACKET):
        for byte_idx in range(width // 2):  # 2 pixels per byte (4-bit color)
            pixel_line = line_num + line
            
            # Create a visible marker pattern in top-left corner (first 32x32 pixels)
            if pixel_line < 32 and byte_idx < 16:
                # Use frame_num modulo 16 as the color in marker area
                marker_color = frame_num % 16
                payload[line * (width // 2) + byte_idx] = (marker_color << 4) | marker_color
            else:
                # Rest of frame: simple pattern based on position
                color1 = (byte_idx + pixel_line + frame_num) % 16
                color2 = (byte_idx + pixel_line + frame_num + 1) % 16
                payload[line * (width // 2) + byte_idx] = (color2 << 4) | color1
    
    return header + bytes(payload)


def generate_audio_packet(frame_num, sample_rate):
    """
    Generate a single audio packet following C64 Ultimate spec.
    
    Packet Structure:
    1. Header: Sequence number (16-bit LE)
    2. Payload: 192 stereo samples (16-bit signed LE, interleaved L/R)
    """
    # Build header (2 bytes)
    header = struct.pack('<H', frame_num)
    
    # Generate deterministic audio pattern (440Hz tone with frame marker)
    # Add a unique amplitude envelope at the start of each frame
    t = np.linspace(0, AUDIO_SAMPLES_PER_PACKET / sample_rate, 
                    AUDIO_SAMPLES_PER_PACKET, endpoint=False)
    
    # 440Hz sine wave
    tone = np.sin(2 * np.pi * 440 * t)
    
    # Add frame marker: amplitude modulation in first few samples
    # This allows verification of A/V sync
    envelope = np.ones(AUDIO_SAMPLES_PER_PACKET)
    marker_length = 10
    for i in range(marker_length):
        envelope[i] = 0.5 + 0.5 * (frame_num % 16) / 16.0
    
    # Apply envelope and convert to 16-bit signed
    audio_signal = (tone * envelope * 0.5 * 32767).astype(np.int16)
    
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
                    fmt['height'], fmt['packets_per_frame']
                )
                
                # Write packet to file
                packet_file = video_dir / f"frame_{frame_num:04d}_pkt_{packet_num:03d}.bin"
                packet_file.write_bytes(packet_data)
        
        total_video_packets = num_frames * fmt['packets_per_frame']
        print(f"    Generated {total_video_packets} video packets")
        
        # Generate audio packets (one per frame for simplicity)
        print(f"  Audio: {fmt['audio_sample_rate']} Hz sample rate")
        for frame_num in range(num_frames):
            packet_data = generate_audio_packet(frame_num, fmt['audio_sample_rate'])
            
            # Write packet to file
            packet_file = audio_dir / f"frame_{frame_num:04d}.bin"
            packet_file.write_bytes(packet_data)
        
        print(f"    Generated {num_frames} audio packets")
    
    print(f"\n✅ Packet generation complete: {output_dir}")


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
