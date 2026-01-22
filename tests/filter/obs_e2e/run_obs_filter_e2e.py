#!/usr/bin/env python3
"""
C64 Stream Effects - OBS integration test.

Runs a full OBS session with the C64 Stream source + C64 Stream Effects filter,
records output, and validates scanline contrast on the recorded video.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[2]
E2E_DIR = PROJECT_ROOT / "tests" / "e2e"

sys.path.insert(0, str(E2E_DIR))

from e2e import build_udp_replay
from framework.c64u_mock.replayer import PacketReplayer
from framework.c64u_mock.server import MockC64UServer
from framework.environment import Environment
from framework.obs.config import OBSConfigManager
from framework.obs.logs import OBSLogManager
from framework.obs.process import OBSProcessManager
from framework.validation.recording import RecordingValidator
from framework.xvfb import XvfbController

SKIP_RETURN_CODE = 77


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--format", default="NTSC", choices=["PAL", "NTSC"])
    parser.add_argument("--frames", type=int, default=120)
    return parser.parse_args()


def should_skip() -> tuple[bool, str]:
    if os.environ.get("CI"):
        return True, "CI environment"
    for tool in ("obs", "ffmpeg", "Xvfb"):
        if shutil.which(tool) is None:
            return True, f"Missing dependency: {tool}"
    return False, ""


def probe_video_info(path: Path) -> tuple[int, int, float]:
    cmd = [
        "ffprobe",
        "-v",
        "error",
        "-select_streams",
        "v:0",
        "-show_entries",
        "stream=width,height,duration",
        "-of",
        "json",
        str(path),
    ]
    data = json.loads(subprocess.check_output(cmd))
    stream = (data.get("streams") or [{}])[0]
    width = int(stream.get("width", 0) or 0)
    height = int(stream.get("height", 0) or 0)
    duration = float(stream.get("duration", 0.0) or 0.0)
    if width <= 0 or height <= 0:
        raise RuntimeError("ffprobe did not return a valid video size")
    return width, height, duration


def extract_frame(path: Path, time_offset: float, width: int, height: int) -> bytes:
    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-ss",
        f"{time_offset:.3f}",
        "-i",
        str(path),
        "-frames:v",
        "1",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgba",
        "-",
    ]
    data = subprocess.check_output(cmd)
    expected = width * height * 4
    if len(data) != expected:
        raise RuntimeError(f"Raw frame size mismatch: expected {expected}, got {len(data)}")
    return data


def scanline_scaling_info(scan_line_distance: float) -> tuple[int, int]:
    if scan_line_distance <= 0.25:
        return 5, 4
    if scan_line_distance <= 0.5:
        return 3, 2
    if scan_line_distance <= 1.0:
        return 4, 2
    return 3, 1


def compute_scanline_ratio(frame: bytes, width: int, height: int, total: int, scanline: int) -> float:
    row_bytes = width * 4
    bright_total = 0.0
    bright_count = 0
    dark_total = 0.0
    dark_count = 0

    for y in range(height):
        row = frame[y * row_bytes : (y + 1) * row_bytes]
        row_sum = 0.0
        for i in range(0, len(row), 4):
            row_sum += row[i] + row[i + 1] + row[i + 2]
        row_avg = row_sum / (width * 3)
        if (y % total) < scanline:
            bright_total += row_avg
            bright_count += 1
        else:
            dark_total += row_avg
            dark_count += 1

    if bright_count == 0 or dark_count == 0:
        raise RuntimeError("Scanline ratio calculation failed")

    bright_avg = bright_total / bright_count
    dark_avg = dark_total / dark_count
    if bright_avg <= 1.0:
        raise RuntimeError("Scanline ratio invalid: bright rows too dark")
    return dark_avg / bright_avg


def analyze_scanlines(recording: Path, scan_line_distance: float, scan_line_strength: float) -> None:
    width, height, duration = probe_video_info(recording)
    total, scanline = scanline_scaling_info(scan_line_distance)

    sample_times = [0.8, 1.6, 2.4]
    if duration > 0.0:
        sample_times = [t for t in sample_times if t < max(0.5, duration - 0.2)]

    ratios = []
    for t in sample_times:
        frame = extract_frame(recording, t, width, height)
        ratios.append(compute_scanline_ratio(frame, width, height, total, scanline))

    if not ratios:
        raise RuntimeError("No scanline samples collected")

    expected = max(0.0, 1.0 - scan_line_strength)
    for ratio in ratios:
        if abs(ratio - expected) > 0.25:
            raise AssertionError(f"Scanline ratio drift: {ratio:.3f} vs {expected:.3f}")


def generate_packets(test_dir: Path, frames: int, video_format: str) -> None:
    packet_dir = test_dir / "test_packets"
    if packet_dir.exists():
        shutil.rmtree(packet_dir)
    cmd = [
        sys.executable,
        str(test_dir / "util" / "generate_packets.py"),
        "--frames",
        str(frames),
        "--format",
        video_format,
        "--output",
        str(packet_dir),
        "--pattern",
        "solid",
    ]
    subprocess.check_call(cmd, cwd=str(test_dir))


def main() -> int:
    args = parse_args()
    skip, reason = should_skip()
    if skip:
        print(f"Skipping OBS filter E2E: {reason}")
        return SKIP_RETURN_CODE

    env = Environment(E2E_DIR, output_dir=args.output_dir)
    env.prepare()

    obs_config = OBSConfigManager(env)
    if not obs_config.copy_e2e_properties():
        raise RuntimeError("Failed to copy E2E properties")

    overrides_dir = SCRIPT_DIR / "overrides"
    profile = obs_config.create_obs_profile(args.format, overrides_dir)

    generate_packets(E2E_DIR, args.frames, args.format)

    udp_replay = build_udp_replay(E2E_DIR, env.is_ci)
    if not udp_replay:
        raise RuntimeError("Failed to build udp_replay")

    obs_logs = OBSLogManager(env)
    obs_process = OBSProcessManager(env, obs_logs)
    xvfb = XvfbController(env)
    mock_server = MockC64UServer(env, control_port=6400)
    replayer = PacketReplayer(env, args.format)

    try:
        if not xvfb.start():
            raise RuntimeError("Failed to start Xvfb")
        mock_server.start()
        obs_process.start(profile_name=profile.name, start_recording=True)

        if not mock_server.wait_for_trigger(timeout=30):
            raise RuntimeError("Timeout waiting for stream trigger")

        video_dest = mock_server.video_dest or ("127.0.0.1", 21000)
        audio_dest = mock_server.audio_dest or ("127.0.0.1", 21001)
        if not replayer.replay(Path(udp_replay), video_dest, audio_dest):
            raise RuntimeError("Packet replay failed")

        time.sleep(2.0)
    finally:
        obs_process.stop()
        mock_server.stop()
        xvfb.stop()
        obs_logs.collect_latest_log(obs_process._start_time)
        obs_config.restore_backup()

    recording = RecordingValidator(env).check_recording_output()
    if not recording:
        raise RuntimeError("No OBS recording produced")

    analyze_scanlines(recording, scan_line_distance=0.5, scan_line_strength=0.6)
    print("OBS filter E2E passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
