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
            "min_contrast_ratio": 0.0,  # Optional: require a minimum dark-vs-bright contrast (0 disables)
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
            # Extract a representative frame for analysis.
            # Recordings often include OBS startup/shutdown padding where content is not yet stable.
            frame, chosen_t = self._extract_best_frame(mp4_path)
            if frame is None:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="Could not extract frame for scanline analysis",
                )

            # Analyze scanline pattern
            ok, variance, details = self._analyze_scanlines(frame, verbose)
            details["frame_time_offset_s"] = float(chosen_t)
            if not ok:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=details.get("error", "Scanline analysis failed"),
                    details=details,
                )

            max_variance = self.thresholds["max_variance_percent"]
            if variance > max_variance:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Scanline variance too high: {variance:.2f}% (max: {max_variance}%)",
                    details=details,
                    metrics={"variance_percent": variance},
                )

            min_contrast_ratio = float(self.thresholds.get("min_contrast_ratio", 0.0) or 0.0)
            contrast_ratio = float(details.get("contrast_ratio", 0.0) or 0.0)
            if min_contrast_ratio > 0.0 and contrast_ratio < min_contrast_ratio:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Scanline contrast too low: {contrast_ratio:.3f} (min: {min_contrast_ratio:.3f})",
                    details=details,
                    metrics={"contrast_ratio": contrast_ratio, "variance_percent": variance},
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

    def _extract_best_frame(self, mp4_path: Path) -> tuple[Optional[np.ndarray], float]:
        """Extract a stable frame by sampling multiple offsets and picking the one with the largest
        detected content area.

        Returns (frame, chosen_time_offset_s).
        """

        def content_area(frame: np.ndarray) -> int:
            gray = 0.2126 * frame[..., 0] + 0.7152 * frame[..., 1] + 0.0722 * frame[..., 2]
            row_max = np.max(gray, axis=1)
            col_max = np.max(gray, axis=0)
            threshold = 10.0
            content_rows = np.where(row_max > threshold)[0]
            content_cols = np.where(col_max > threshold)[0]
            if len(content_rows) == 0 or len(content_cols) == 0:
                return 0
            y0, y1 = int(content_rows[0]), int(content_rows[-1])
            x0, x1 = int(content_cols[0]), int(content_cols[-1])
            if x1 <= x0 + 10 or y1 <= y0 + 10:
                return 0
            return max(0, (x1 - x0) * (y1 - y0))

        # Empirically: 8-14s tends to be inside the stable content window for these recordings,
        # but keep earlier offsets as fallback for shorter recordings.
        offsets = [10.0, 8.0, 12.0, 6.0, 4.0, 2.0]
        best_frame = None
        best_t = float(offsets[0])
        best_area = -1

        for t in offsets:
            frame = self._extract_frame(mp4_path, time_offset=float(t))
            if frame is None:
                continue
            area = int(content_area(frame))
            if area > best_area:
                best_area = area
                best_frame = frame
                best_t = float(t)

        return best_frame, best_t

    def _analyze_scanlines(
        self, frame: np.ndarray, verbose: bool
    ) -> tuple[bool, float, dict[str, Any]]:
        """Analyze scanline pattern in a frame."""
        # Convert to grayscale (0..255)
        gray = 0.2126 * frame[..., 0] + 0.7152 * frame[..., 1] + 0.0722 * frame[..., 2]

        # Find content bounds (non-black area), matching scanline_all_modes_test.py
        row_max = np.max(gray, axis=1)
        col_max = np.max(gray, axis=0)

        threshold = 10.0
        content_rows = np.where(row_max > threshold)[0]
        content_cols = np.where(col_max > threshold)[0]

        if len(content_rows) == 0 or len(content_cols) == 0:
            return False, 100.0, {"error": "No content detected"}

        y_start, y_end = int(content_rows[0]), int(content_rows[-1])
        x_start, x_end = int(content_cols[0]), int(content_cols[-1])
        if x_end <= x_start + 10 or y_end <= y_start + 10:
            return False, 100.0, {"error": "Could not find content region"}

        # Analyze a vertical band around the horizontal center.
        # Use a slightly wider band + median aggregation to reduce single-pixel noise.
        center_x = (x_start + x_end) // 2
        half_w = 8
        x0 = max(x_start, center_x - half_w)
        x1 = min(x_end, center_x + half_w)
        band = np.median(gray[y_start : y_end + 1, x0 : x1 + 1], axis=1)

        # Small 1D median filter to suppress 1-row glitches.
        if band.size >= 3:
            band = band.astype(np.float64, copy=False)
            band = np.concatenate([[band[0]], np.median(np.stack([band[:-2], band[1:-1], band[2:]]), axis=0), [band[-1]]])

        p10 = float(np.percentile(band, 10))
        p90 = float(np.percentile(band, 90))
        thr = (p10 + p90) / 2.0
        is_bright = band >= thr

        transitions = np.diff(is_bright.astype(np.int32))
        bright_to_dark = np.where(transitions == -1)[0]

        scanline_count = int(len(bright_to_dark))
        if scanline_count < int(self.thresholds["min_scanline_count"]):
            return False, 100.0, {"error": f"Too few scanlines detected: {scanline_count}"}

        if scanline_count >= 2:
            spacings = np.diff(bright_to_dark).astype(np.float64)
            mean_spacing = float(np.mean(spacings))
            std_spacing = float(np.std(spacings))
            variance_percent = (std_spacing / mean_spacing * 100.0) if mean_spacing > 0 else 100.0
        else:
            mean_spacing = 0.0
            std_spacing = 0.0
            variance_percent = 100.0

        bright_vals = band[is_bright]
        dark_vals = band[~is_bright]
        med_bright = float(np.median(bright_vals)) if bright_vals.size else 0.0
        med_dark = float(np.median(dark_vals)) if dark_vals.size else 0.0
        contrast_ratio = float((med_bright - med_dark) / max(med_bright, 1.0))

        details = {
            "scanline_count": scanline_count,
            "mean_spacing": mean_spacing,
            "std_spacing": std_spacing,
            "threshold": float(thr),
            "median_dark": med_dark,
            "median_bright": med_bright,
            "contrast_ratio": contrast_ratio,
            "content_region": {"x": (int(x_start), int(x_end)), "y": (int(y_start), int(y_end))},
            "roi": {"x": (int(x0), int(x1))},
        }

        self.log(
            f"Found {scanline_count} scanlines, mean spacing={mean_spacing:.2f}, variance={variance_percent:.2f}%, contrast={contrast_ratio:.3f}",
            verbose,
        )
        return True, float(variance_percent), details
