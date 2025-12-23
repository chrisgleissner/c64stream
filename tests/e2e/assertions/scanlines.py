#!/usr/bin/env python3
"""
C64 Stream - Scanlines Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import subprocess
from pathlib import Path
from typing import Any, Optional

import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class ScanlineAssertion(EffectAssertion):
    """Verify scanline uniformity in the recording."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "max_variance_percent": 0.5,  # Max acceptable scanline height variance
            "min_scanline_count": 50,  # Minimum expected scanlines
        }
        super().__init__("Scanlines", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not preset.has_scanlines():
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Scanlines not enabled for this preset",
            )

        self.log(
            f"Verifying scanlines (distance={preset.scan_line_distance}, strength={preset.scan_line_strength})",
            verbose,
        )

        try:
            # Extract a single frame for analysis
            frame = self._extract_frame(mp4_path, time_offset=2.0)
            if frame is None:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="Could not extract frame for scanline analysis",
                )

            # Analyze scanline pattern
            ok, variance, details = self._analyze_scanlines(frame, verbose)

            max_variance = self.thresholds["max_variance_percent"]
            if variance > max_variance:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Scanline variance too high: {variance:.2f}% (max: {max_variance}%)",
                    details=details,
                    metrics={"variance_percent": variance},
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"Scanlines uniform: {variance:.2f}% variance",
                details=details,
                metrics={"variance_percent": variance},
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Scanline verification failed: {e}",
            )

    def _extract_frame(self, mp4_path: Path, time_offset: float) -> Optional[np.ndarray]:
        """Extract a single frame at the given time offset."""
        cmd = [
            "ffmpeg",
            "-v",
            "error",
            "-ss",
            str(time_offset),
            "-i",
            str(mp4_path),
            "-frames:v",
            "1",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ]
        try:
            result = subprocess.run(cmd, capture_output=True, check=True)
            if len(result.stdout) == 1920 * 1080 * 3:
                return np.frombuffer(result.stdout, dtype=np.uint8).reshape((1080, 1920, 3))
        except subprocess.CalledProcessError:
            pass
        return None

    def _analyze_scanlines(
        self, frame: np.ndarray, verbose: bool
    ) -> tuple[bool, float, dict[str, Any]]:
        """Analyze scanline pattern in a frame."""
        # Convert to grayscale
        gray = 0.2126 * frame[..., 0] + 0.7152 * frame[..., 1] + 0.0722 * frame[..., 2]

        # Find content region (non-black area)
        col_means = gray.mean(axis=0)
        row_means = gray.mean(axis=1)

        x_start = np.argmax(col_means > 10)
        x_end = len(col_means) - np.argmax(col_means[::-1] > 10)
        y_start = np.argmax(row_means > 10)
        y_end = len(row_means) - np.argmax(row_means[::-1] > 10)

        if x_end <= x_start or y_end <= y_start:
            return False, 100.0, {"error": "Could not find content region"}

        # Analyze vertical center column for scanline pattern
        center_x = (x_start + x_end) // 2
        column = gray[y_start:y_end, center_x]

        # Detect scanline gaps (dark rows)
        threshold = np.percentile(column, 30)
        dark_rows = column < threshold

        # Find scanline positions
        scanlines = []
        in_gap = False
        gap_start = 0

        for i, is_dark in enumerate(dark_rows):
            if is_dark and not in_gap:
                in_gap = True
                gap_start = i
            elif not is_dark and in_gap:
                in_gap = False
                gap_height = i - gap_start
                if gap_height >= 1:  # Minimum gap height
                    scanlines.append(gap_height)

        if len(scanlines) < self.thresholds["min_scanline_count"]:
            return False, 100.0, {"error": f"Too few scanlines detected: {len(scanlines)}"}

        # Calculate variance
        mean_height = np.mean(scanlines)
        std_height = np.std(scanlines)
        variance_percent = (std_height / mean_height * 100) if mean_height > 0 else 100.0

        details = {
            "scanline_count": len(scanlines),
            "mean_height": float(mean_height),
            "std_height": float(std_height),
            "content_region": {"x": (int(x_start), int(x_end)), "y": (int(y_start), int(y_end))},
        }

        self.log(f"Found {len(scanlines)} scanlines, mean height={mean_height:.2f}, variance={variance_percent:.2f}%", verbose)
        return True, float(variance_percent), details
