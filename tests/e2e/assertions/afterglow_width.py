#!/usr/bin/env python3
"""
C64 Stream - Afterglow Width Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Measures and compares afterglow trail widths behind moving diagonal lines.
This is a quantitative assertion that verifies GPU-based afterglow produces
equivalent output to the historical CPU-based implementation.
"""

import json
import subprocess
from contextlib import suppress
from pathlib import Path
from typing import Any, Optional

import numpy as np
from scipy.ndimage import uniform_filter1d

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class AfterglowWidthAssertion(EffectAssertion):
    """
    Verify afterglow trail width matches reference PNG.

    This assertion:
    1. Extracts a frame from the recorded MP4
    2. Measures the afterglow trail width behind moving diagonal lines
    3. Compares against the reference PNG from the E2E results folder
    4. Fails if the widths differ by more than the tolerance
    """

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "width_tolerance_px": 4.0,  # Max acceptable difference in trail width
            "min_peaks": 3,  # Minimum number of peaks to analyze
            "peak_threshold": 80.0,  # Minimum brightness for peak detection
            "trail_end_threshold_pct": 5.0,  # Percentage of peak where trail ends
            "max_trail_width_px": 80.0,  # Cap unreasonable trail widths (> ~2 frames at 60fps)
        }
        super().__init__("AfterglowWidth", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not preset.has_afterglow():
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Afterglow not enabled for this preset",
            )

        # Find the reference PNG in the same results folder
        results_dir = mp4_path.parent.resolve()
        reference_png = results_dir / "c64_recording_still.png"

        # Check if the reference PNG exists and is from git (not modified)
        # If modified locally, we can't use it as a reference
        import subprocess
        try:
            # Get the git root first
            git_root = Path(subprocess.check_output(
                ["git", "rev-parse", "--show-toplevel"],
                cwd=results_dir,
                text=True,
            ).strip())

            # Check if file is unmodified in git
            result = subprocess.run(
                ["git", "diff", "--quiet", str(reference_png)],
                cwd=git_root,
                capture_output=True,
            )
            reference_is_modified = result.returncode != 0
        except Exception:
            reference_is_modified = True
            git_root = None

        if reference_is_modified:
            # Reference PNG was modified - try to get it from git main branch
            try:
                if git_root is None:
                    git_root = Path(subprocess.check_output(
                        ["git", "rev-parse", "--show-toplevel"],
                        cwd=results_dir,
                        text=True,
                    ).strip())

                # Get relative path from git root
                rel_path = reference_png.resolve().relative_to(git_root)

                # Extract reference from main branch
                result = subprocess.run(
                    ["git", "show", f"main:{rel_path}"],
                    cwd=git_root,
                    capture_output=True,
                )
                if result.returncode != 0:
                    return AssertionResult(
                        status=AssertionStatus.SKIP,
                        name=self.name,
                        message=f"Reference PNG modified and not available from main branch",
                    )

                # Load reference from git
                import io
                from PIL import Image
                reference_frame = np.array(Image.open(io.BytesIO(result.stdout)))
                self.log(f"Using reference PNG from main branch", verbose)
            except Exception as e:
                return AssertionResult(
                    status=AssertionStatus.SKIP,
                    name=self.name,
                    message=f"Could not load reference from git: {e}",
                )
        else:
            if not reference_png.exists():
                return AssertionResult(
                    status=AssertionStatus.SKIP,
                    name=self.name,
                    message=f"Reference PNG not found: {reference_png}",
                )
            from PIL import Image
            reference_frame = np.array(Image.open(reference_png))

        self.log(f"Comparing afterglow width against reference", verbose)

        try:
            # Extract a frame from the recorded MP4 (at 10 seconds - stable period)
            current_frame = self._extract_frame_from_mp4(mp4_path, time_sec=10.0)

            if current_frame.shape != reference_frame.shape:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Frame dimensions mismatch: current={current_frame.shape}, reference={reference_frame.shape}",
                )

            # Measure trail widths in both frames
            current_widths = self._measure_trail_widths(current_frame, verbose)
            reference_widths = self._measure_trail_widths(reference_frame, verbose)

            if len(current_widths) < self.thresholds["min_peaks"]:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Not enough peaks found in current frame: {len(current_widths)} < {self.thresholds['min_peaks']}",
                )

            if len(reference_widths) < self.thresholds["min_peaks"]:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Not enough peaks found in reference frame: {len(reference_widths)} < {self.thresholds['min_peaks']}",
                )

            # Compare average trail widths
            current_avg = float(np.mean(current_widths))
            reference_avg = float(np.mean(reference_widths))
            diff = abs(current_avg - reference_avg)
            tolerance = self.thresholds["width_tolerance_px"]

            self.log(f"Current avg trail width: {current_avg:.1f}px", verbose)
            self.log(f"Reference avg trail width: {reference_avg:.1f}px", verbose)
            self.log(f"Difference: {diff:.1f}px (tolerance: {tolerance:.1f}px)", verbose)

            if diff > tolerance:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Afterglow trail width mismatch: current={current_avg:.1f}px, reference={reference_avg:.1f}px, diff={diff:.1f}px > {tolerance:.1f}px",
                    details={
                        "current_avg_width": current_avg,
                        "reference_avg_width": reference_avg,
                        "difference": diff,
                        "tolerance": tolerance,
                        "current_widths": [float(w) for w in current_widths],
                        "reference_widths": [float(w) for w in reference_widths],
                    },
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"Afterglow trail width matches reference: {current_avg:.1f}px ≈ {reference_avg:.1f}px (diff={diff:.1f}px)",
                details={
                    "current_avg_width": current_avg,
                    "reference_avg_width": reference_avg,
                    "difference": diff,
                },
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Afterglow width verification failed: {e}",
            )

    def _extract_frame_from_mp4(self, mp4_path: Path, time_sec: float) -> np.ndarray:
        """Extract a single frame from the MP4 at the specified time."""
        w, h = self._ffprobe_size(mp4_path)

        cmd = [
            "ffmpeg", "-v", "error",
            "-ss", str(time_sec),
            "-i", str(mp4_path),
            "-vframes", "1",
            "-f", "rawvideo",
            "-pix_fmt", "rgb24",
            "-"
        ]

        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        frame_bytes = w * h * 3
        buf = proc.stdout.read(frame_bytes)
        proc.stdout.close()
        proc.wait(timeout=10)

        if len(buf) != frame_bytes:
            raise RuntimeError(f"Failed to extract frame: got {len(buf)} bytes, expected {frame_bytes}")

        return np.frombuffer(buf, dtype=np.uint8).reshape((h, w, 3))

    def _ffprobe_size(self, mp4_path: Path) -> tuple[int, int]:
        out = subprocess.check_output([
            "ffprobe", "-v", "error",
            "-select_streams", "v:0",
            "-show_entries", "stream=width,height",
            "-of", "json",
            str(mp4_path),
        ])
        info = json.loads(out)
        stream = info["streams"][0]
        return int(stream["width"]), int(stream["height"])

    def _measure_trail_widths(self, frame: np.ndarray, verbose: bool = False) -> list[float]:
        """
        Measure the afterglow trail widths behind moving diagonal lines.

        The E2E test generates diagonal lines that move 1 pixel per frame.
        Each line leaves an afterglow trail behind it (to the left of the line).
        We measure the width from the peak to where intensity drops to threshold.
        """
        # Use green channel for green monitor preset (works for all presets)
        # For other tints, the green channel still captures the intensity well
        gray = frame[..., 1].astype(float)

        # Sample multiple horizontal lines and average the results
        h, w = gray.shape
        sample_lines = [h // 3, h // 2, 2 * h // 3]  # Sample at 1/3, 1/2, 2/3 height

        all_widths = []

        for y in sample_lines:
            profile = gray[y, :]
            widths = self._measure_line_trail_widths(profile, verbose)
            all_widths.extend(widths)

        return all_widths

    def _measure_line_trail_widths(self, profile: np.ndarray, verbose: bool = False) -> list[float]:
        """Measure trail widths along a single horizontal line."""
        # Smooth to reduce noise
        smooth = uniform_filter1d(profile.astype(float), size=3)

        # Find peaks (line cores)
        min_peak_val = self.thresholds["peak_threshold"]
        peaks = []

        for x in range(10, len(smooth) - 10):
            if smooth[x] > min_peak_val:
                # Check if local maximum
                is_peak = True
                for dx in range(-8, 9):
                    if dx != 0 and smooth[x] < smooth[x + dx]:
                        is_peak = False
                        break
                if is_peak:
                    # Merge nearby peaks
                    if not peaks or x - peaks[-1] > 30:
                        peaks.append(x)

        widths = []
        trail_end_pct = self.thresholds["trail_end_threshold_pct"] / 100.0

        for peak_x in peaks:
            peak_val = float(profile[peak_x])

            # The afterglow trail extends LEFT of the peak (lines move right)
            # Measure distance to where intensity drops below threshold
            trail_threshold = max(peak_val * trail_end_pct, 5.0)  # At least 5 to avoid noise

            trail_end = peak_x
            for x in range(peak_x, 0, -1):
                if profile[x] < trail_threshold:
                    trail_end = x
                    break

            trail_width = peak_x - trail_end

            # Only include meaningful trail widths (> 2 pixels, < max)
            # Cap at max_trail_width_px to exclude unreasonable outliers
            max_width = self.thresholds.get("max_trail_width_px", 80.0)
            if trail_width > 2 and trail_width < max_width:
                widths.append(trail_width)
                if verbose:
                    self.log(f"  Peak at x={peak_x}, val={peak_val:.1f}, trail_width={trail_width}px", True)
            elif verbose and trail_width >= max_width:
                self.log(f"  Peak at x={peak_x}, val={peak_val:.1f}, trail_width={trail_width}px (CAPPED - excluded)", True)

        return widths
