#!/usr/bin/env python3
"""
C64 Stream - Deterministic Audio Fixture Generator
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Generates a full replacement audio packet set for click-detection E2E scenarios
(C64CLK-006). The default captured/generated audio content contains natural
steps (pops, noise bursts) that would confound a click detector, so these
scenarios replace it with a phase-continuous 1 kHz sine riding on a constant
DC offset:

- 770-byte packets: little-endian uint16 sequence number + 192 stereo frames
  of 16-bit signed little-endian PCM (interleaved L/R).
- Sine amplitude ~12000, phase continuous across packet boundaries: the
  largest legitimate sample-to-sample step is ~2*pi*1000/rate*12000 ~= 1573,
  so any |delta| above the click threshold (default 6000) is a genuine splice.
- Constant DC offset (+2000) emulates the SID's output bias so that zero-fill
  concealment bugs produce a detectable step.

Selected per scenario via the `audio_packet_set: sine1k` network_simulation
key; shell_lib/packets.sh invokes this generator during packet preparation.
"""

import argparse
import math
import struct
import sys
from pathlib import Path

SAMPLES_PER_PACKET = 192
SINE_FREQ_HZ = 1000.0
SINE_AMPLITUDE = 12000
DC_OFFSET = 2000

SAMPLE_RATES = {
    'PAL': 47982.8869047619,
    'NTSC': 47940.3408482143,
}


def generate_sine_packet(packet_num: int, sample_rate: float) -> bytes:
    """Generate one 770-byte packet of phase-continuous 1 kHz sine + DC offset."""
    header = struct.pack('<H', packet_num & 0xFFFF)
    base_index = packet_num * SAMPLES_PER_PACKET

    samples = []
    for i in range(SAMPLES_PER_PACKET):
        t = (base_index + i) / sample_rate
        value = int(round(DC_OFFSET + SINE_AMPLITUDE * math.sin(2.0 * math.pi * SINE_FREQ_HZ * t)))
        samples.append(value)  # Left
        samples.append(value)  # Right

    return header + struct.pack(f'<{len(samples)}h', *samples)


def main() -> int:
    ap = argparse.ArgumentParser(description='Generate sine1k audio packet fixture')
    ap.add_argument('--format', choices=['PAL', 'NTSC'], required=True)
    ap.add_argument('--count', type=int, required=True,
                    help='Number of packets to generate (match the default audio set)')
    ap.add_argument('--output', type=Path, required=True,
                    help='Packet base directory (e.g. test_packets); packets go to '
                         '<output>/audio_sine1k/<FORMAT>/')
    args = ap.parse_args()

    if args.count <= 0:
        print('❌ --count must be positive')
        return 1

    out_dir = args.output / 'audio_sine1k' / args.format
    out_dir.mkdir(parents=True, exist_ok=True)

    sample_rate = SAMPLE_RATES[args.format]
    for packet_num in range(args.count):
        packet = generate_sine_packet(packet_num, sample_rate)
        assert len(packet) == 770, f'packet size {len(packet)} != 770'
        (out_dir / f'audio_{packet_num:04d}.bin').write_bytes(packet)

    print(f'✅ Generated {args.count} sine1k audio packets ({args.format}) in {out_dir}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
