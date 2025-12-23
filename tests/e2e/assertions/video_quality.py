#!/usr/bin/env python3
"""
C64 Stream - Video Quality Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import json
import subprocess
from contextlib import suppress
from pathlib import Path
from typing import Any, Optional

import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class VideoQualityAssertion(EffectAssertion):
    """Verify basic video quality: duration, resolution, non-black frames."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_duration_ratio": 0.8,  # Min ratio of expected duration
            "max_duration_ratio": 1.2,  # Max ratio of expected duration
            "min_nonblack_ratio": 0.5,  # Min ratio of non-black frames
            "black_threshold": 5.0,  # Luma threshold for black detection
        }
        super().__init__("VideoQuality", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not mp4_path.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Recording file not found: {mp4_path}",
            )

        try:
            # Get video info
            info = self._ffprobe_info(mp4_path)
            width = info.get("width", 0)
            height = info.get("height", 0)
            duration = info.get("duration", 0.0)

            self.log(f"Video: {width}x{height}, {duration:.2f}s", verbose)

            # Check resolution (expected: 1920x1080 for E2E)
            if width != 1920 or height != 1080:
                return AssertionResult(
                    status=AssertionStatus.WARNING,
                    name=self.name,
                    message=f"Unexpected resolution: {width}x{height} (expected 1920x1080)",
                    details={"width": width, "height": height},
                )

            # Check for non-black frames
            nonblack_ratio = self._check_nonblack_frames(mp4_path, verbose)
            min_ratio = self.thresholds["min_nonblack_ratio"]

            if nonblack_ratio < min_ratio:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Too many black frames: {nonblack_ratio:.1%} non-black (min: {min_ratio:.1%})",
                    metrics={"nonblack_ratio": nonblack_ratio},
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"Video quality OK: {width}x{height}, {duration:.2f}s, {nonblack_ratio:.1%} non-black",
                details={"width": width, "height": height, "duration": duration},
                metrics={"nonblack_ratio": nonblack_ratio},
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Video quality check failed: {e}",
            )

    def _ffprobe_info(self, mp4_path: Path) -> dict[str, Any]:
        cmd = [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height",
            "-show_entries",
            "format=duration",
            "-of",
            "json",
            str(mp4_path),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        data = json.loads(result.stdout)
        stream = data.get("streams", [{}])[0]
        fmt = data.get("format", {})
        return {
            "width": int(stream.get("width", 0)),
            "height": int(stream.get("height", 0)),
            "duration": float(fmt.get("duration", 0)),
        }

    def _check_nonblack_frames(self, mp4_path: Path, verbose: bool) -> float:
        """Sample frames and count non-black ones."""
        w, h = 1920, 1080
        frame_bytes = w * h * 3
        black_thresh = self.thresholds["black_threshold"]

        cmd = [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(mp4_path),
            "-vf",
            "fps=2",  # Sample at 2 fps
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ]

        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        total = 0
        nonblack = 0

        try:
            while True:
                buf = proc.stdout.read(frame_bytes)
                if len(buf) != frame_bytes:
                    break
                total += 1
                arr = np.frombuffer(buf, dtype=np.uint8).reshape((h, w, 3))
                luma = 0.2126 * arr[..., 0] + 0.7152 * arr[..., 1] + 0.0722 * arr[..., 2]
                if np.mean(luma) > black_thresh:
                    nonblack += 1
        finally:
            with suppress(Exception):
                proc.stdout.close()
            proc.kill()
            proc.wait(timeout=5)

        self.log(f"Non-black frames: {nonblack}/{total}", verbose)
        return nonblack / total if total > 0 else 0.0
