#!/usr/bin/env python3
"""
Verify that an OBS recording is predominantly green-tinted.

This is meant as a simple "POC verifier" to prove our E2E pipeline can detect
visual filters in recorded output before we move on to afterglow validation.
"""

import argparse
import json
import subprocess
from contextlib import suppress
from pathlib import Path


def ffprobe_size(path: Path) -> tuple[int, int]:
    out = subprocess.check_output(
        [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height",
            "-of",
            "json",
            str(path),
        ]
    )
    info = json.loads(out)
    stream = info["streams"][0]
    return int(stream["width"]), int(stream["height"])


def iter_rgb_frames(path: Path, max_frames: int, fps: float) -> tuple[int, int, list[tuple[int, int, int]]]:
    w, h = ffprobe_size(path)
    frame_bytes = w * h * 3

    # Downsample in time to keep runtime bounded.
    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(path),
        "-vf",
        f"fps={fps}",
        "-frames:v",
        str(max_frames),
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb24",
        "-",
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    frames = []
    try:
        for _ in range(max_frames):
            buf = proc.stdout.read(frame_bytes)
            if len(buf) != frame_bytes:
                break
            # Fast-ish channel sums using slicing.
            r_sum = sum(buf[0::3])
            g_sum = sum(buf[1::3])
            b_sum = sum(buf[2::3])
            frames.append((r_sum, g_sum, b_sum))
    finally:
        # Best-effort cleanup: ffmpeg may exit early and close pipes.
        with suppress(Exception):
            proc.stdout.close()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

    return w, h, frames


def verify_green(frames: list[tuple[int, int, int]], min_green_ratio: float, min_nonblack_sum: int) -> tuple[bool, str, dict]:
    checked = 0
    passed = 0
    ratios = []

    for (r_sum, g_sum, b_sum) in frames:
        total = r_sum + g_sum + b_sum
        if total < min_nonblack_sum:
            # Likely a blank/black frame; ignore it for tint validation.
            continue

        checked += 1
        # Compare green to the average of red/blue to avoid false positives on bright whites.
        rb_avg = (r_sum + b_sum) / 2.0 if (r_sum + b_sum) > 0 else 1.0
        ratio = g_sum / rb_avg
        ratios.append(ratio)

        if ratio >= min_green_ratio and g_sum > r_sum and g_sum > b_sum:
            passed += 1

    if checked == 0:
        return False, "No non-black frames found for tint analysis", {"checked_frames": 0, "passed_frames": 0}

    ok = passed == checked
    details = {
        "checked_frames": checked,
        "passed_frames": passed,
        "min_green_ratio": min_green_ratio,
        "ratios": {
            "min": float(min(ratios)) if ratios else None,
            "p50": float(sorted(ratios)[len(ratios) // 2]) if ratios else None,
            "max": float(max(ratios)) if ratios else None,
        },
    }

    if ok:
        return True, f"Green tint detected on all checked frames (n={checked})", details
    return False, f"Green tint missing on some frames: {passed}/{checked} passed", details


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify green tint in an OBS recording")
    ap.add_argument("recording", help="Path to recording file (mp4/mkv)")
    ap.add_argument("--max-frames", type=int, default=20, help="Max frames to analyze (after fps downsample)")
    ap.add_argument("--fps", type=float, default=2.0, help="Temporal downsample FPS for analysis")
    ap.add_argument("--min-green-ratio", type=float, default=1.25, help="Require G >= ratio * avg(R,B)")
    ap.add_argument("--min-nonblack-sum", type=int, default=500_000, help="Ignore frames with very low total intensity")
    args = ap.parse_args()

    rec = Path(args.recording)
    if not rec.exists():
        raise SystemExit(f"Recording not found: {rec}")

    w, h, frames = iter_rgb_frames(rec, max_frames=args.max_frames, fps=args.fps)
    ok, msg, details = verify_green(frames, min_green_ratio=args.min_green_ratio, min_nonblack_sum=args.min_nonblack_sum)

    print(json.dumps({"ok": ok, "details": msg, "video": {"width": w, "height": h}, "stats": details}, indent=2))
    return 0 if ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
