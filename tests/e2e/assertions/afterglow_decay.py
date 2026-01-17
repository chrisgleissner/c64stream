#!/usr/bin/env python3
"""
C64 Stream - Afterglow Decay Uniformity Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Verifies that afterglow decay is smooth and monotonic:
1. Temporal decay: brightness at a pixel should only decrease over time
2. Spatial smoothness: afterglow trail should have buttery-smooth falloff (low 2nd derivative)
3. No sudden jumps, blotchiness, or jagged steps that destroy the CRT phosphor illusion
"""

import json
import subprocess
from contextlib import suppress
from pathlib import Path
from typing import Any, Optional

import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion, ffmpeg_hwaccel_args, ffmpeg_vf_with_hwdownload
from .config import PresetConfig


class AfterglowDecayAssertion(EffectAssertion):
    """
    Verify afterglow decay is smooth and monotonic over time AND spatially.

    This assertion:
    1. Tracks the brightness of lit pixels over consecutive frames (temporal)
    2. Verifies brightness only decreases (monotonic decay)
    3. Measures spatial smoothness of the afterglow trail (2nd derivative)
    4. Fails if the afterglow is "jagged", "blotchy", or has visible steps
    """

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "max_increase_tolerance": 1.0,  # Max allowed brightness increase between frames
            "min_decay_samples": 5,  # Minimum frames of decay to analyze
            "min_initial_brightness": 60.0,  # Minimum brightness to consider a "lit" pixel
            "max_plateau_frames": 2,  # Max consecutive frames with <1.0 change allowed
            "max_violation_rate": 0.05,  # Max fraction of pixels that can violate monotonicity
            # Spatial smoothness thresholds (key for detecting jagged GPU afterglow)
            "max_spatial_roughness": 4.5,  # Max 2nd derivative in X direction for bright pixels
            "roughness_brightness_threshold": 15.0,  # Min brightness to include in roughness analysis
        }
        super().__init__("AfterglowDecay", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not preset.has_afterglow():
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Afterglow not enabled for this preset",
            )

        self.log(f"Verifying afterglow decay uniformity (duration={preset.afterglow_duration_ms}ms)", verbose)

        try:
            # Read video frames
            w, h = self._ffprobe_size(mp4_path)
            frames = self._read_frames(mp4_path, w, h, max_frames=120)
            n_frames = frames.shape[0]

            if n_frames < 30:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Not enough frames for decay analysis ({n_frames} < 30)",
                )

            self.log(f"Analyzing {n_frames} frames ({w}x{h})", verbose)

            # Convert to luma
            luma = self._to_luma(frames)

            # === TEMPORAL DECAY CHECK ===
            violations, total_samples, decay_details = self._analyze_temporal_decay(
                luma, verbose
            )

            violation_rate = violations / max(1, total_samples)
            max_rate = self.thresholds["max_violation_rate"]

            if violation_rate > max_rate:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Afterglow decay not monotonic: {violations}/{total_samples} samples ({violation_rate:.1%}) showed unexpected increases (max {max_rate:.1%})",
                    details=decay_details,
                )

            # === SPATIAL SMOOTHNESS CHECK (key for detecting jagged GPU afterglow) ===
            # This measures the "buttery smoothness" of the afterglow trail
            roughness, roughness_details = self._analyze_spatial_smoothness(frames, verbose)

            max_roughness = self.thresholds["max_spatial_roughness"]
            if roughness > max_roughness:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Afterglow is not smooth: spatial roughness {roughness:.2f} exceeds max {max_roughness:.2f} (jagged/blotchy falloff detected)",
                    details={**decay_details, **roughness_details},
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"Afterglow decay is smooth (temporal: {violation_rate:.1%} violations, spatial roughness: {roughness:.2f})",
                details={**decay_details, **roughness_details},
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Afterglow decay analysis failed: {e}",
            )

    def _ffprobe_size(self, mp4_path: Path) -> tuple[int, int]:
        out = subprocess.check_output([
            "ffprobe", "-v", "error", "-select_streams", "v:0",
            "-show_entries", "stream=width,height", "-of", "json", str(mp4_path)
        ])
        info = json.loads(out)
        stream = info["streams"][0]
        return int(stream["width"]), int(stream["height"])

    def _read_frames(self, mp4_path: Path, w: int, h: int, max_frames: int) -> np.ndarray:
        """Read frames as RGB24."""
        cmd = [
            "ffmpeg",
            "-v",
            "error",
            *ffmpeg_hwaccel_args(),
            "-i",
            str(mp4_path),
            "-frames:v",
            str(max_frames),
        ]
        vf = ffmpeg_vf_with_hwdownload(None)
        if vf:
            cmd += ["-vf", vf]
        cmd += [
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        frame_bytes = w * h * 3
        frames = []
        try:
            while len(frames) < max_frames:
                buf = proc.stdout.read(frame_bytes)
                if len(buf) != frame_bytes:
                    break
                frames.append(np.frombuffer(buf, dtype=np.uint8).reshape(h, w, 3))
        finally:
            with suppress(Exception):
                proc.stdout.close()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
        return np.stack(frames, axis=0)

    @staticmethod
    def _to_luma(frames_rgb: np.ndarray) -> np.ndarray:
        """Convert RGB to luma (ITU-R BT.709)."""
        f = frames_rgb.astype(np.float32)
        return 0.2126 * f[..., 0] + 0.7152 * f[..., 1] + 0.0722 * f[..., 2]

    def _analyze_temporal_decay(
        self, luma: np.ndarray, verbose: bool
    ) -> tuple[int, int, dict]:
        """
        Analyze temporal decay of afterglow at fixed pixel positions.

        For each sample position, we find a frame where it becomes bright (lit by
        the diagonal line), then track its brightness over subsequent frames.
        The brightness should monotonically decrease as the afterglow fades.

        This is a TEMPORAL analysis - tracking the same pixel over time, not
        a spatial analysis of the trail within a single frame.
        """
        n_frames, h, w = luma.shape
        min_brightness = self.thresholds["min_initial_brightness"]
        max_increase = self.thresholds["max_increase_tolerance"]
        min_samples = int(self.thresholds["min_decay_samples"])

        violations = 0
        total_samples = 0
        sample_details = []

        # Sample rows avoiding scanline gaps (period ~3px) and pop region
        # Use rows offset to hit lit scanlines
        scanline_period = 3
        step_y = max(scanline_period * 4, h // 12)  # Fewer rows, better coverage
        start_y = (h // 4) - ((h // 4) % scanline_period) + 1

        # Sample x positions, avoiding edges and pop region (right side)
        step_x = max(1, w // 20)

        for y in range(start_y, 3 * h // 4, step_y):
            for x in range(w // 4, w // 2, step_x):  # Left half only (avoid pop region)
                # Get temporal sequence for this fixed pixel position
                pixel_sequence = luma[:, y, x]

                # Find frames where this pixel becomes bright (line passes through)
                # We need to find a "peak" followed by decay
                for frame_idx in range(n_frames - min_samples - 2):
                    current_brightness = pixel_sequence[frame_idx]

                    # Check if this is a peak (brighter than neighbors)
                    if current_brightness >= min_brightness:
                        prev_brightness = pixel_sequence[frame_idx - 1] if frame_idx > 0 else 0
                        next_brightness = pixel_sequence[frame_idx + 1]

                        # A peak should be brighter than what comes before and (possibly) after
                        # due to the diagonal line passing through
                        if current_brightness > prev_brightness and current_brightness >= next_brightness:
                            # Found a peak, now check the decay sequence
                            decay_start = frame_idx
                            decay_end = min(frame_idx + min_samples + 3, n_frames)
                            decay_sequence = pixel_sequence[decay_start:decay_end]

                            if len(decay_sequence) < min_samples:
                                continue

                            # Track decay - brightness should only decrease
                            for i in range(1, len(decay_sequence)):
                                delta = decay_sequence[i] - decay_sequence[i - 1]
                                total_samples += 1

                                # Flag significant increases (brightness going UP during decay)
                                if delta > max_increase:
                                    violations += 1
                                    if verbose and len(sample_details) < 10:
                                        sample_details.append({
                                            "x": x,
                                            "y": y,
                                            "frame": decay_start + i,
                                            "brightness": float(decay_sequence[i]),
                                            "delta": float(delta),
                                        })

                            # Skip to after this decay sequence to avoid re-checking
                            break

        self.log(f"Analyzed {total_samples} temporal decay samples, found {violations} violations", verbose)

        if verbose and sample_details:
            self.log("Sample violations:", verbose)
            for d in sample_details[:5]:
                self.log(f"  ({d['x']}, {d['y']}) frame {d['frame']}: Δ={d['delta']:+.1f}", verbose)

        return violations, total_samples, {
            "violations": violations,
            "total_samples": total_samples,
            "violation_rate": violations / max(1, total_samples),
            "sample_violations": sample_details[:10],
        }

    def _analyze_spatial_smoothness(
        self, frames_rgb: np.ndarray, verbose: bool
    ) -> tuple[float, dict]:
        """
        Analyze spatial smoothness of afterglow using 2nd derivative (acceleration).

        A buttery-smooth afterglow trail has low 2nd derivative (constant decay rate).
        Jagged/blotchy afterglow has high 2nd derivative (sudden brightness jumps).

        We analyze the GREEN channel specifically since afterglow is visible on
        tinted presets (green monitor, amber monitor) and green has highest weight.
        """
        brightness_threshold = self.thresholds["roughness_brightness_threshold"]

        # Sample frames from middle of video (where afterglow is most visible)
        n_frames = frames_rgb.shape[0]
        if n_frames == 1:
            # Single frame (e.g., test image) - use it directly
            sample_frames = [0]
        else:
            sample_frames = range(n_frames // 3, max(1, 2 * n_frames // 3), max(1, n_frames // 15))

        all_roughness = []

        for frame_idx in sample_frames:
            frame = frames_rgb[frame_idx]

            # Use green channel (most visible for tinted presets, and most weight in luma)
            green = frame[:, :, 1].astype(np.float32)

            # Create mask for bright pixels (where afterglow is visible)
            bright_mask = green > brightness_threshold

            if np.sum(bright_mask) < 100:
                continue  # Not enough bright pixels in this frame

            # Compute 2nd derivative in X direction (horizontal roughness)
            # 2nd derivative = acceleration of brightness change
            # Smooth decay has low acceleration; jagged has high acceleration
            diff2_x = np.abs(np.diff(np.diff(green, axis=1), axis=1))

            # Mask to only include bright regions (where afterglow is visible)
            # Account for reduced width after two diffs
            mask_x = bright_mask[:, 1:-1]

            if np.sum(mask_x) > 50:
                roughness_x = np.mean(diff2_x[mask_x])
                all_roughness.append(roughness_x)

        if not all_roughness:
            self.log("No bright regions found for spatial smoothness analysis", verbose)
            return 0.0, {"spatial_roughness": 0.0, "frames_analyzed": 0}

        avg_roughness = float(np.mean(all_roughness))
        max_roughness = float(np.max(all_roughness))

        self.log(
            f"Spatial smoothness: avg roughness {avg_roughness:.2f}, max {max_roughness:.2f} "
            f"(from {len(all_roughness)} frames)",
            verbose
        )

        return avg_roughness, {
            "spatial_roughness": avg_roughness,
            "max_spatial_roughness": max_roughness,
            "frames_analyzed": len(all_roughness),
        }
