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

The ``scale500`` fixture is a phase-continuous C-major note ladder suitable
for manual listening as well as automated checks: C3, D3, E3, F3, G3, A3, B3,
C4, then back down, each held for exactly 500 ms. (``B3`` is the conventional
English spelling of German ``H3``.)

Selected per scenario via the `audio_packet_set` network_simulation key;
shell_lib/packets.sh invokes this generator during packet preparation.
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

SCALE500_NOTE_DURATION_S = 0.5
SCALE500_NOTES = (
    ("C3", 130.8127826503),
    ("D3", 146.8323839587),
    ("E3", 164.8137784564),
    ("F3", 174.6141157165),
    ("G3", 195.9977179909),
    ("A3", 220.0000000000),
    ("B3", 246.9416506281),
    ("C4", 261.6255653006),
    ("B3", 246.9416506281),
    ("A3", 220.0000000000),
    ("G3", 195.9977179909),
    ("F3", 174.6141157165),
    ("E3", 164.8137784564),
    ("D3", 146.8323839587),
    ("C3", 130.8127826503),
)

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


def scale500_note_samples(sample_rate: float) -> int:
    """Return the exact sample length of every 500 ms note."""
    return round(sample_rate * SCALE500_NOTE_DURATION_S)


def scale500_note_index(sample_index: int, sample_rate: float) -> int:
    """Return the ladder position for a zero-based PCM sample index."""
    return (sample_index // scale500_note_samples(sample_rate)) % len(SCALE500_NOTES)


def scale500_sample(sample_index: int, sample_rate: float) -> int:
    """Generate a phase-continuous sample of the 500 ms note ladder."""
    note_samples = scale500_note_samples(sample_rate)
    note_index = scale500_note_index(sample_index, sample_rate)
    cycle_samples = note_samples * len(SCALE500_NOTES)
    cycle_index, sample_in_cycle = divmod(sample_index, cycle_samples)

    cycles_before = cycle_index * sum(frequency * note_samples for _, frequency in SCALE500_NOTES)
    completed_notes = sample_in_cycle // note_samples
    cycles_before += sum(
        frequency * note_samples for _, frequency in SCALE500_NOTES[:completed_notes]
    )
    sample_in_note = sample_in_cycle % note_samples
    frequency = SCALE500_NOTES[note_index][1]
    phase = 2.0 * math.pi * (cycles_before + frequency * sample_in_note) / sample_rate
    return int(round(DC_OFFSET + SINE_AMPLITUDE * math.sin(phase)))


def generate_scale500_packet(packet_num: int, sample_rate: float) -> bytes:
    """Generate one packet of the deterministic 500 ms C-major note ladder."""
    header = struct.pack('<H', packet_num & 0xFFFF)
    base_index = packet_num * SAMPLES_PER_PACKET
    samples = []
    for i in range(SAMPLES_PER_PACKET):
        value = scale500_sample(base_index + i, sample_rate)
        samples.extend((value, value))
    return header + struct.pack(f'<{len(samples)}h', *samples)


def generate_packet(packet_set: str, packet_num: int, sample_rate: float) -> bytes:
    """Generate a packet from one of the supported deterministic fixtures."""
    if packet_set == "sine1k":
        return generate_sine_packet(packet_num, sample_rate)
    if packet_set == "scale500":
        return generate_scale500_packet(packet_num, sample_rate)
    raise ValueError(f"unsupported packet set: {packet_set}")


def main() -> int:
    ap = argparse.ArgumentParser(description='Generate deterministic audio packet fixtures')
    ap.add_argument('--format', choices=['PAL', 'NTSC'], required=True)
    ap.add_argument('--count', type=int, required=True,
                    help='Number of packets to generate (match the default audio set)')
    ap.add_argument('--output', type=Path, required=True,
                    help='Packet base directory (e.g. test_packets); packets go to '
                         '<output>/audio_<packet-set>/<FORMAT>/')
    ap.add_argument('--packet-set', choices=['sine1k', 'scale500'], default='sine1k')
    args = ap.parse_args()

    if args.count <= 0:
        print('❌ --count must be positive')
        return 1

    out_dir = args.output / f'audio_{args.packet_set}' / args.format
    out_dir.mkdir(parents=True, exist_ok=True)

    sample_rate = SAMPLE_RATES[args.format]
    for packet_num in range(args.count):
        packet = generate_packet(args.packet_set, packet_num, sample_rate)
        assert len(packet) == 770, f'packet size {len(packet)} != 770'
        (out_dir / f'audio_{packet_num:04d}.bin').write_bytes(packet)

    print(f'✅ Generated {args.count} {args.packet_set} audio packets ({args.format}) in {out_dir}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
