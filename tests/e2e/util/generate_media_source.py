#!/usr/bin/env python3
"""
C64 Stream - Media Source Generator for E2E Testing
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Generates deterministic media files from the existing packet generator output.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable

import numpy as np

from generate_packets import (
    AUDIO_HEADER_SIZE,
    BITS_PER_PIXEL,
    LINES_PER_PACKET,
    VIDEO_FORMATS,
    VIDEO_HEADER_SIZE,
    generate_packets,
    set_scenario_name,
)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _load_vpl_palette(path: Path) -> list[tuple[int, int, int]]:
    if not path.exists():
        raise FileNotFoundError(f"Palette file not found: {path}")

    colors: list[tuple[int, int, int]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "#" in line:
                line = line[: line.index("#")].strip()
            parts = line.split()
            if len(parts) >= 3:
                try:
                    r = int(parts[0], 16)
                    g = int(parts[1], 16)
                    b = int(parts[2], 16)
                    colors.append((r, g, b))
                except ValueError:
                    continue
            elif len(line) >= 6:
                try:
                    r = int(line[0:2], 16)
                    g = int(line[2:4], 16)
                    b = int(line[4:6], 16)
                    colors.append((r, g, b))
                except ValueError:
                    continue
            if len(colors) >= 16:
                break

    if len(colors) != 16:
        raise RuntimeError(f"Palette {path} did not yield 16 colors")
    return colors


def _sorted_packet_files(path: Path, prefix: str) -> list[Path]:
    return sorted(path.glob(f"{prefix}_*.bin"))


def _decode_video_packets(video_dir: Path, format_name: str) -> np.ndarray:
    files = _sorted_packet_files(video_dir, "video")
    if not files:
        raise RuntimeError(f"No video packets found in {video_dir}")

    fmt = VIDEO_FORMATS[format_name]
    width = int(fmt["width"])
    height = int(fmt["height"])

    max_frame = -1
    for packet_path in files:
        data = packet_path.read_bytes()
        header = data[:VIDEO_HEADER_SIZE]
        _, frame_num, _, width_pkt, _, _, _ = struct.unpack("<HHHHBBH", header)
        if width_pkt != width:
            raise RuntimeError(f"Unexpected width {width_pkt} in {packet_path}")
        if frame_num > max_frame:
            max_frame = frame_num

    frame_count = max_frame + 1
    if frame_count <= 0:
        raise RuntimeError("No frames decoded from video packets")

    frames = np.zeros((frame_count, height, width), dtype=np.uint8)
    row_bytes = width // 2

    for packet_path in files:
        data = packet_path.read_bytes()
        header = data[:VIDEO_HEADER_SIZE]
        payload = data[VIDEO_HEADER_SIZE:]
        _, frame_num, line_num_with_flag, width_pkt, lines_per_packet, bpp, _ = struct.unpack("<HHHHBBH", header)

        if width_pkt != width or lines_per_packet != LINES_PER_PACKET or bpp != BITS_PER_PIXEL:
            raise RuntimeError(f"Unexpected packet metadata in {packet_path}")

        line_num = line_num_with_flag & 0x7FFF
        for line in range(lines_per_packet):
            start = line * row_bytes
            end = start + row_bytes
            packed = np.frombuffer(payload[start:end], dtype=np.uint8)
            low = packed & 0x0F
            high = packed >> 4
            row = frames[frame_num, line_num + line]
            row[0::2] = low
            row[1::2] = high

    return frames


def _decode_audio_packets(audio_dir: Path) -> np.ndarray:
    files = _sorted_packet_files(audio_dir, "audio")
    if not files:
        return np.zeros((0,), dtype=np.int16)

    chunks: list[np.ndarray] = []
    for packet_path in files:
        data = packet_path.read_bytes()
        payload = data[AUDIO_HEADER_SIZE:]
        if len(payload) % 2 != 0:
            raise RuntimeError(f"Odd audio payload size in {packet_path}")
        chunks.append(np.frombuffer(payload, dtype="<i2"))

    if not chunks:
        return np.zeros((0,), dtype=np.int16)
    return np.concatenate(chunks)


def _add_preamble(
    frames_rgb: np.ndarray,
    audio_samples: np.ndarray,
    fps: float,
    sample_rate: int,
    preamble_duration_s: float = 3.0,
) -> tuple[np.ndarray, np.ndarray]:
    """Add black video frames and silent audio to the beginning to match UDP preamble.

    In UDP mode, there's a ~9-10s preamble showing the C64 logo while waiting for packets.
    For media mode, OBS starts recording ~3-4s after playback starts (natural delay).
    Use 3s preamble so the natural recording delay skips most black frames.
    """
    preamble_frames = int(preamble_duration_s * fps)
    height, width, channels = frames_rgb.shape[1], frames_rgb.shape[2], frames_rgb.shape[3]
    black_frames = np.zeros((preamble_frames, height, width, channels), dtype=np.uint8)
    frames_with_preamble = np.concatenate([black_frames, frames_rgb], axis=0)

    # Add silent audio (2 channels, interleaved)
    preamble_samples = int(preamble_duration_s * sample_rate) * 2
    silence = np.zeros((preamble_samples,), dtype=np.int16)
    audio_with_preamble = np.concatenate([silence, audio_samples])

    return frames_with_preamble, audio_with_preamble


def _write_media_file(
    output_path: Path,
    frames_indexed: np.ndarray,
    palette: Iterable[tuple[int, int, int]],
    fps: float,
    audio_samples: np.ndarray,
    sample_rate: int,
) -> None:
    palette_np = np.array(list(palette), dtype=np.uint8)
    frames_rgb = palette_np[frames_indexed]

    duration_s = frames_rgb.shape[0] / fps if fps > 0 else 0.0
    if audio_samples.size == 0 and duration_s > 0:
        total_samples = int(duration_s * sample_rate)
        audio_samples = np.zeros((total_samples * 2,), dtype=np.int16)

    # Add 3-second preamble (natural recording delay skips most of it)
    frames_rgb, audio_samples = _add_preamble(frames_rgb, audio_samples, fps, sample_rate)

    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg not available in PATH")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        video_raw = tmp_dir / "video.rgb"
        audio_raw = tmp_dir / "audio.s16le"

        frames_rgb.tofile(video_raw)
        audio_samples.tofile(audio_raw)

        width = frames_rgb.shape[2]
        height = frames_rgb.shape[1]

        cmd = [
            ffmpeg,
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-s",
            f"{width}x{height}",
            "-r",
            f"{fps:.6f}",
            "-i",
            str(video_raw),
            "-f",
            "s16le",
            "-ar",
            str(sample_rate),
            "-ac",
            "2",
            "-i",
            str(audio_raw),
            "-c:v",
            "libx264rgb",
            "-pix_fmt",
            "rgb24",
            "-preset",
            "ultrafast",
            "-crf",
            "0",
            "-vsync",
            "cfr",
            "-r",
            f"{fps:.6f}",
            "-c:a",
            "aac",
            "-b:a",
            "192k",
            "-shortest",
            str(output_path),
        ]

        subprocess.run(cmd, check=True)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a deterministic MP4 from C64 packets")
    parser.add_argument("--format", choices=["PAL", "NTSC"], default="NTSC")
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--output", type=Path, required=True, help="Output MP4 file")
    parser.add_argument("--packet-dir", type=Path, default=Path("test_packets"))
    parser.add_argument("--scenario", type=str, default="DEFAULT")
    parser.add_argument("--pattern", type=str, default="diagonal")
    parser.add_argument("--disable-pops", action="store_true")
    parser.add_argument("--full-frame-pop", action="store_true")
    parser.add_argument("--palette", type=Path, default=None)
    return parser.parse_args()


def main() -> int:
    args = _parse_args()

    if args.frames <= 0:
        raise RuntimeError("Frame count must be positive")

    set_scenario_name(args.scenario)

    packet_root = args.packet_dir.resolve()
    if packet_root.exists():
        shutil.rmtree(packet_root)
    packet_root.mkdir(parents=True, exist_ok=True)

    generate_packets(
        output_dir=str(packet_root),
        num_frames=args.frames,
        formats=[args.format],
        pattern=args.pattern,
        parallel=True,
        disable_pops=args.disable_pops,
        full_frame_pop=args.full_frame_pop,
    )

    video_dir = packet_root / "video" / args.format
    audio_dir = packet_root / "audio" / args.format

    frames = _decode_video_packets(video_dir, args.format)
    audio = _decode_audio_packets(audio_dir)

    palette_path = args.palette
    if palette_path is None:
        palette_path = _repo_root() / "data" / "palettes" / "default.vpl"

    palette = _load_vpl_palette(palette_path)

    fmt = VIDEO_FORMATS[args.format]
    fps = float(fmt["frame_rate"])
    sample_rate = int(fmt["audio_sample_rate"])

    _write_media_file(args.output, frames, palette, fps, audio, sample_rate)
    return 0


if __name__ == "__main__":
    sys.exit(main())
