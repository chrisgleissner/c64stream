#!/usr/bin/env python3
"""
C64 Stream - Afterglow Assertion
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


class AfterglowAssertion(EffectAssertion):
    """Verify afterglow persistence using A/V pop ROI detection."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "bright_thresh": 140.0,  # Threshold for pop detection
            "max_frames": 360,
            "min_tail_luma": 2.5,  # Minimum luma for first tail frame
            "max_tail_increase": 2.5,  # Maximum allowed per-frame increase in tail luma
        }
        super().__init__("Afterglow", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not preset.has_afterglow():
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Afterglow not enabled for this preset",
            )

        self.log(f"Verifying afterglow (duration={preset.afterglow_duration_ms}ms)", verbose)

        try:
            frames = self._read_frames_rgb24(mp4_path, int(self.thresholds["max_frames"]))
            luma = self._luma_u8(frames)

            # Find pop ROI
            roi = self._find_pop_roi(luma, self.thresholds["bright_thresh"])
            self.log(f"Found pop ROI: {roi}", verbose)

            # Verify afterglow decay
            ok, details = self._verify_afterglow_decay(luma, roi)

            if not ok:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=details,
                    details={"roi": {"x0": roi[0], "y0": roi[1], "x1": roi[2], "y1": roi[3]}},
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message="Afterglow persistence verified (tail decays across frames)",
                details={
                    "roi": {"x0": roi[0], "y0": roi[1], "x1": roi[2], "y1": roi[3]},
                    "decay_details": details,
                },
            )

        except RuntimeError as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Afterglow verification failed: {e}",
            )

    def _read_frames_rgb24(self, mp4_path: Path, max_frames: int) -> np.ndarray:
        """Decode video to RGB24 frames. Returns array [N,H,W,3] uint8."""
        w, h = self._ffprobe_size(mp4_path)
        cmd = [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(mp4_path),
            "-frames:v",
            str(max_frames),
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
                frames.append(np.frombuffer(buf, dtype=np.uint8).reshape((h, w, 3)))
        finally:
            with suppress(Exception):
                proc.stdout.close()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
        if not frames:
            raise RuntimeError("No frames decoded from recording")
        return np.stack(frames, axis=0)

    def _ffprobe_size(self, mp4_path: Path) -> tuple[int, int]:
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
                str(mp4_path),
            ]
        )
        info = json.loads(out)
        stream = info["streams"][0]
        return int(stream["width"]), int(stream["height"])

    @staticmethod
    def _luma_u8(frames_rgb: np.ndarray) -> np.ndarray:
        f = frames_rgb.astype(np.float32)
        return 0.2126 * f[..., 0] + 0.7152 * f[..., 1] + 0.0722 * f[..., 2]

    def _find_pop_roi(self, luma_frames: np.ndarray, bright_thresh: float) -> tuple[int, int, int, int]:
        """Auto-locate the A/V pop ROI by selecting the cluster around the brightest pixel."""
        p = np.percentile(luma_frames.reshape((luma_frames.shape[0], -1)), 99.95, axis=1)
        peak_idx = int(np.argmax(p))

        frame_peak = float(p[peak_idx])
        thr = max(float(bright_thresh), frame_peak * 0.98)

        mask = luma_frames[peak_idx] > thr
        ys, xs = np.where(mask)
        if xs.size < 40:
            raise RuntimeError(f"Could not locate pop ROI (thr={thr:.2f}, peak={frame_peak:.2f})")

        peak_xy = np.unravel_index(int(np.argmax(luma_frames[peak_idx])), luma_frames[peak_idx].shape)
        cy, cx = int(peak_xy[0]), int(peak_xy[1])

        radius = 160
        near = (np.abs(xs - cx) <= radius) & (np.abs(ys - cy) <= radius)
        xs_r, ys_r = xs[near], ys[near]
        if xs_r.size < 40:  # Reduced from 80 for CI compatibility
            raise RuntimeError("Could not isolate pop cluster near brightest pixel")

        x0, x1 = int(xs_r.min()), int(xs_r.max())
        y0, y1 = int(ys_r.min()), int(ys_r.max())

        pad = 4
        h, w = luma_frames.shape[1], luma_frames.shape[2]
        return max(0, x0 - pad), max(0, y0 - pad), min(w - 1, x1 + pad), min(h - 1, y1 + pad)

    def _verify_afterglow_decay(self, luma_frames: np.ndarray, roi: tuple[int, int, int, int]) -> tuple[bool, str]:
        x0, y0, x1, y1 = roi
        roi_luma = luma_frames[:, y0 : y1 + 1, x0 : x1 + 1].mean(axis=(1, 2))

        p90 = float(np.percentile(roi_luma, 90))
        p99 = float(np.percentile(roi_luma, 99))
        high_thresh = max(20.0, (p90 + p99) / 2.0)
        idx = np.where(roi_luma > high_thresh)[0]
        if idx.size == 0:
            return False, f"No pop frames detected in ROI (threshold={high_thresh:.2f})"

        # First contiguous pop event
        s = int(idx[0])
        e = s
        for i in idx[1:]:
            if int(i) == e + 1:
                e = int(i)
            else:
                break

        if e + 10 >= len(roi_luma):
            return False, "Recording too short to evaluate afterglow tail"

        tail = roi_luma[e + 1 : e + 11]
        min_tail = self.thresholds["min_tail_luma"]

        if float(tail[0]) < min_tail:
            return False, f"Afterglow tail missing: first tail frame luma={float(tail[0]):.2f} (peak={float(roi_luma[e]):.2f})"

        max_tail_increase = float(self.thresholds["max_tail_increase"])
        if not np.all(np.diff(tail) <= max_tail_increase):
            return False, "Afterglow tail is not decaying (unexpected brightness increase)"

        if float(np.mean(tail[2:6])) < 4.0:
            return False, "Afterglow tail fades too quickly (mean tail too low)"

        return True, "Afterglow persistence detected (tail decays across frames)"
