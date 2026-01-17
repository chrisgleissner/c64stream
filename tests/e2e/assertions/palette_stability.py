#!/usr/bin/env python3
"""
C64 Stream - Palette Stability Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import subprocess
from contextlib import suppress
from pathlib import Path
from typing import Any, Optional

import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion, ffmpeg_hwaccel_args, ffmpeg_vf_with_hwdownload
from .config import PresetConfig


class PaletteStabilityAssertion(EffectAssertion):
    """Verify that the VIC-II color palette remains stable (no color drift) over time.

    This assertion should only be used for presets WITHOUT afterglow, as afterglow
    causes physically accurate red-shift which is expected behavior.
    """

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "max_drift_tol": 8.0,  # Max RGB channel delta from baseline
            "max_frames": 180,
        }
        super().__init__("Palette Stability", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        # Skip if afterglow is enabled (red-shift is expected)
        if preset.has_afterglow():
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Palette stability not checked when afterglow is enabled (red-shift is expected)",
            )

        self.log("Verifying palette color stability", verbose)

        try:
            frames = self._read_frames_rgb24(mp4_path, int(self.thresholds["max_frames"]))
            luma = self._luma_u8(frames)

            ok, message, details = self._check_palette_stability(frames, luma, verbose)

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
                message=f"Palette stability check failed: {e}",
            )

    def _read_frames_rgb24(self, mp4_path: Path, max_frames: int) -> np.ndarray:
        """Decode video to RGB24 frames. Returns array [N,H,W,3] uint8."""
        w, h = self._ffprobe_size(mp4_path)
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
                arr = np.frombuffer(buf, dtype=np.uint8).reshape((h, w, 3))
                frames.append(arr)
        finally:
            with suppress(Exception):
                proc.stdout.close()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
        return np.array(frames) if frames else np.zeros((0, h, w, 3), dtype=np.uint8)

    def _ffprobe_size(self, mp4_path: Path) -> tuple[int, int]:
        """Get video dimensions via ffprobe."""
        cmd = [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height",
            "-of",
            "csv=s=x:p=0",
            str(mp4_path),
        ]
        out = subprocess.check_output(cmd, text=True).strip()
        w_str, h_str = out.split("x")
        return int(w_str), int(h_str)

    def _luma_u8(self, frames: np.ndarray) -> np.ndarray:
        """Convert RGB frames to luma (grayscale). Returns [N,H,W] uint8."""
        if frames.size == 0:
            return np.zeros((0, 0, 0), dtype=np.uint8)
        # ITU-R BT.601 luma: Y = 0.299*R + 0.587*G + 0.114*B
        luma_float = 0.299 * frames[:, :, :, 0] + 0.587 * frames[:, :, :, 1] + 0.114 * frames[:, :, :, 2]
        return np.clip(luma_float, 0, 255).astype(np.uint8)

    def _check_palette_stability(
        self, frames: np.ndarray, luma: np.ndarray, verbose: bool
    ) -> tuple[bool, str, dict[str, Any]]:
        """Check that the VIC-II palette tile doesn't drift over time.

        Samples the top-right corner (palette display area) and checks that
        average RGB values remain stable across frames.
        """
        # Find content bounding box
        n = min(luma.shape[0], 60)
        avg = luma[:n].mean(axis=0)
        mask = avg > 8.0
        ys, xs = np.where(mask)
        if xs.size == 0:
            return True, "Could not locate content (skipping palette check)", {"frames_checked": 0}

        cx0, cy0, cx1, cy1 = int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())
        cw, ch = max(1, cx1 - cx0 + 1), max(1, cy1 - cy0 + 1)

        # Top-right corner element: 88x56 outer, 72x40 inner in C64 coordinates
        # Sample the inner area where solid color patterns appear
        bw = max(1, int(round(cw * (88.0 / 384.0))))
        bh = max(1, int(round(ch * (56.0 / 272.0))))
        gx0, gx1 = max(cx0, cx1 - bw + 1), cx1
        gy0, gy1 = cy0, min(cy1, cy0 + bh - 1)

        tile = frames[:, gy0 : gy1 + 1, gx0 : gx1 + 1, :].astype(np.float32)

        # Baseline from first 18 frames
        base_n = min(tile.shape[0], 18)
        if base_n < 3:
            return True, f"Not enough frames ({base_n}) for palette stability check", {"frames_checked": base_n}

        baseline = tile[:base_n].mean(axis=(0, 1, 2))  # Average RGB across first N frames

        # Check for signal loss (video ends)
        signal_end = tile.shape[0]
        for i in range(base_n, tile.shape[0]):
            delta = np.max(np.abs(tile[i].mean(axis=(0, 1)) - baseline))
            if delta > 50.0:
                signal_end = i
                break

        tile = tile[:signal_end]

        # Check max drift per channel
        max_drift_per_channel = np.zeros(3, dtype=np.float32)
        for i in range(tile.shape[0]):
            frame_avg = tile[i].mean(axis=(0, 1))  # RGB averages for this frame
            drift = np.abs(frame_avg - baseline)
            max_drift_per_channel = np.maximum(max_drift_per_channel, drift)

        peak_delta = float(np.max(max_drift_per_channel))
        tol = self.thresholds["max_drift_tol"]

        details = {
            "frames_checked": signal_end,
            "baseline_rgb": [float(baseline[0]), float(baseline[1]), float(baseline[2])],
            "max_drift_r": float(max_drift_per_channel[0]),
            "max_drift_g": float(max_drift_per_channel[1]),
            "max_drift_b": float(max_drift_per_channel[2]),
            "peak_delta": peak_delta,
            "tolerance": tol,
        }

        if peak_delta > tol:
            return (
                False,
                f"Palette drift detected (peak_delta={peak_delta:.2f} > tol={tol:.2f})",
                details,
            )

        self.log(f"Palette stable (max drift={peak_delta:.2f})", verbose)
        return True, f"Palette stable (max drift={peak_delta:.2f} <= {tol:.2f})", details
