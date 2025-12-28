#!/usr/bin/env python3
"""
C64 Stream - Tint Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import subprocess
from contextlib import suppress
from pathlib import Path
from typing import Any, Optional

import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class TintAssertion(EffectAssertion):
    """Verify color tint (amber or green) is present when expected."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_tint_ratio": 1.20,  # Dominant channel must be 20% higher than avg of others
            "min_nonblack_sum": 500_000,  # Ignore very dark frames
            "sample_fps": 2.0,
            "max_frames": 20,
        }
        super().__init__("Tint", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not preset.has_tint():
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Tint not enabled for this preset",
            )

        tint_type = preset.tint_type()
        self.log(f"Verifying {tint_type} tint (strength={preset.tint_strength})", verbose)

        try:
            frames = self._sample_frames(mp4_path, verbose)
            if not frames:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="No frames could be sampled for tint analysis",
                )

            ok, message, details = self._verify_tint(frames, tint_type, verbose)

            return AssertionResult(
                status=AssertionStatus.PASS if ok else AssertionStatus.FAIL,
                name=self.name,
                message=message,
                details=details,
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Tint verification failed: {e}",
            )

    def _sample_frames(self, mp4_path: Path, verbose: bool) -> list[tuple[int, int, int]]:
        """Sample frames and return RGB channel sums."""
        w, h = 1920, 1080
        frame_bytes = w * h * 3
        fps = self.thresholds["sample_fps"]
        max_frames = int(self.thresholds["max_frames"])

        cmd = [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(mp4_path),
            "-vf",
            f"fps={fps}",
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
                arr = np.frombuffer(buf, dtype=np.uint8)
                # Reshape to N x 3 and sum channels (uint64 to avoid overflow)
                rgb = arr.reshape((-1, 3)).astype(np.uint64)
                r_sum, g_sum, b_sum = (int(x) for x in rgb.sum(axis=0))
                frames.append((r_sum, g_sum, b_sum))
        finally:
            with suppress(Exception):
                proc.stdout.close()
            proc.kill()
            proc.wait(timeout=5)

        self.log(f"Sampled {len(frames)} frames for tint analysis", verbose)
        return frames

    def _verify_tint(
        self, frames: list[tuple[int, int, int]], tint_type: str, verbose: bool
    ) -> tuple[bool, str, dict[str, Any]]:
        min_ratio = self.thresholds["min_tint_ratio"]
        min_sum = int(self.thresholds["min_nonblack_sum"])

        checked = 0
        passed = 0
        ratios = []

        for r_sum, g_sum, b_sum in frames:
            total = r_sum + g_sum + b_sum
            if total < min_sum:
                continue  # Skip black frames

            checked += 1

            if tint_type == "green":
                # Green should dominate over avg(R, B)
                rb_avg = (r_sum + b_sum) / 2.0 if (r_sum + b_sum) > 0 else 1.0
                ratio = g_sum / rb_avg
                dominant = g_sum > r_sum and g_sum > b_sum
            else:  # amber
                # Amber: R > G > B, and R+G should dominate B
                gb_avg = (g_sum + b_sum) / 2.0 if (g_sum + b_sum) > 0 else 1.0
                ratio = r_sum / gb_avg
                dominant = r_sum > g_sum > b_sum

            ratios.append(ratio)
            if ratio >= min_ratio and dominant:
                passed += 1

        if checked == 0:
            return False, "No non-black frames found for tint analysis", {"checked_frames": 0}

        details = {
            "checked_frames": checked,
            "passed_frames": passed,
            "tint_type": tint_type,
            "min_ratio": min_ratio,
            "ratios": {
                "min": float(min(ratios)) if ratios else None,
                "median": float(sorted(ratios)[len(ratios) // 2]) if ratios else None,
                "max": float(max(ratios)) if ratios else None,
            },
        }

        if passed == checked:
            return True, f"{tint_type.capitalize()} tint verified on all {checked} frames", details
        return False, f"{tint_type.capitalize()} tint missing: {passed}/{checked} frames passed", details
