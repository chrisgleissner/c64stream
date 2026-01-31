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
            from .base import is_ci
            scale_factor = 0.5 if is_ci() else 1.0

            frames = self._read_frames_rgb24(mp4_path, int(self.thresholds["max_frames"]))
            luma = self._luma_u8(frames)

            # Find pop ROI
            roi = self._find_pop_roi(luma, self.thresholds["bright_thresh"], scale_factor)
            self.log(f"Found pop ROI: {roi}", verbose)

            # Verify afterglow decay
            ok, details, debug = self._verify_afterglow_decay(luma, roi, verbose)

            if not ok:
                self._write_debug_dump(mp4_path, debug)
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
            self._write_debug_dump(mp4_path, {"error": str(e)})
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Afterglow verification failed: {e}",
            )

    def _write_debug_dump(self, mp4_path: Path, debug: dict[str, Any]) -> None:
        try:
            debug_path = mp4_path.with_suffix(".afterglow_debug.json")
            debug_path.write_text(json.dumps(debug, indent=2))
        except Exception:
            pass

    def _read_frames_rgb24(self, mp4_path: Path, max_frames: int) -> np.ndarray:
        """Decode video to RGB24 frames. Returns array [N,H,W,3] uint8.
        In CI, scales down to 960x540 to reduce memory usage."""
        from .base import is_ci

        w, h = self._ffprobe_size(mp4_path)

        # Scale down by 2x in CI to reduce memory usage (2.1GB -> 525MB)
        scale_filter = ""
        if is_ci():
            w, h = w // 2, h // 2
            scale_filter = f"scale={w}:{h},"

        cmd = [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(mp4_path),
            "-frames:v",
            str(max_frames),
        ]

        if scale_filter:
            cmd.extend(["-vf", scale_filter.rstrip(",")])

        cmd.extend([
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ])

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

    def _find_pop_roi(self, luma_frames: np.ndarray, bright_thresh: float, scale_factor: float = 1.0) -> tuple[int, int, int, int]:
        """Auto-locate the A/V pop ROI by selecting the cluster around the brightest pixel.
        scale_factor: 0.5 for half-resolution, 1.0 for full resolution"""
        p = np.percentile(luma_frames.reshape((luma_frames.shape[0], -1)), 99.95, axis=1)
        peak_idx = int(np.argmax(p))

        frame_peak = float(p[peak_idx])
        thr = max(float(bright_thresh), frame_peak * 0.98)

        mask = luma_frames[peak_idx] > thr
        ys, xs = np.where(mask)

        # Scale pixel count threshold (40 @ 1080p -> 10 @ 540p)
        min_pixels = int(40 * scale_factor * scale_factor)
        if xs.size < min_pixels:
            raise RuntimeError(f"Could not locate pop ROI (thr={thr:.2f}, peak={frame_peak:.2f}, pixels={xs.size}, min={min_pixels})")

        peak_xy = np.unravel_index(int(np.argmax(luma_frames[peak_idx])), luma_frames[peak_idx].shape)
        cy, cx = int(peak_xy[0]), int(peak_xy[1])

        # Scale radius (160 @ 1080p -> 80 @ 540p)
        radius = int(160 * scale_factor)
        near = (np.abs(xs - cx) <= radius) & (np.abs(ys - cy) <= radius)
        xs_r, ys_r = xs[near], ys[near]
        if xs_r.size < min_pixels:
            raise RuntimeError(f"Could not isolate pop cluster near brightest pixel (pixels={xs_r.size}, min={min_pixels})")
            raise RuntimeError("Could not isolate pop cluster near brightest pixel")

        x0, x1 = int(xs_r.min()), int(xs_r.max())
        y0, y1 = int(ys_r.min()), int(ys_r.max())

        pad = 4
        h, w = luma_frames.shape[1], luma_frames.shape[2]
        return max(0, x0 - pad), max(0, y0 - pad), min(w - 1, x1 + pad), min(h - 1, y1 + pad)

    def _verify_afterglow_decay(
        self,
        luma_frames: np.ndarray,
        roi: tuple[int, int, int, int],
        verbose: bool = False,
    ) -> tuple[bool, str, dict[str, Any]]:
        x0, y0, x1, y1 = roi
        roi_luma = luma_frames[:, y0 : y1 + 1, x0 : x1 + 1].mean(axis=(1, 2))

        p90 = float(np.percentile(roi_luma, 90))
        p99 = float(np.percentile(roi_luma, 99))
        high_thresh = max(20.0, (p90 + p99) / 2.0)
        idx = np.where(roi_luma > high_thresh)[0]
        if idx.size == 0:
            return (
                False,
                f"No pop frames detected in ROI (threshold={high_thresh:.2f})",
                {
                    "roi": roi,
                    "high_thresh": float(high_thresh),
                    "pop_segments": [],
                },
            )

        # Build contiguous pop segments for diagnostics.
        segments = []
        seg_start = int(idx[0])
        seg_end = seg_start
        for i in idx[1:]:
            i = int(i)
            if i == seg_end + 1:
                seg_end = i
            else:
                segments.append((seg_start, seg_end))
                seg_start = i
                seg_end = i
        segments.append((seg_start, seg_end))

        # Use the contiguous pop run that contains the peak ROI frame.
        # This avoids selecting a short pre-peak spike that can cause false tail increases.
        peak = int(np.argmax(roi_luma))
        if roi_luma[peak] <= high_thresh:
            peak = int(idx[0])

        s = peak
        while s - 1 >= 0 and roi_luma[s - 1] > high_thresh:
            s -= 1

        e = peak
        while e + 1 < len(roi_luma) and roi_luma[e + 1] > high_thresh:
            e += 1

        self.log(f"Pop window: peak={peak}, range=[{s},{e}], high_thresh={high_thresh:.2f}", verbose)

        if e + 10 >= len(roi_luma):
            return (
                False,
                "Recording too short to evaluate afterglow tail",
                {
                    "roi": roi,
                    "high_thresh": float(high_thresh),
                    "peak_frame": int(peak),
                    "pop_window": {"start": int(s), "end": int(e)},
                    "pop_segments": [{"start": int(a), "end": int(b)} for a, b in segments],
                },
            )

        tail = roi_luma[e + 1 : e + 11]
        min_tail = self.thresholds["min_tail_luma"]

        if float(tail[0]) < min_tail:
            return (
                False,
                f"Afterglow tail missing: first tail frame luma={float(tail[0]):.2f} (peak={float(roi_luma[e]):.2f})",
                {
                    "roi": roi,
                    "high_thresh": float(high_thresh),
                    "peak_frame": int(peak),
                    "pop_window": {"start": int(s), "end": int(e)},
                    "pop_segments": [{"start": int(a), "end": int(b)} for a, b in segments],
                    "tail": [float(v) for v in tail],
                },
            )

        max_tail_increase = float(self.thresholds["max_tail_increase"])
        tail_diffs = np.diff(tail)
        if tail_diffs.size and float(np.max(tail_diffs)) > max_tail_increase:
            return (
                False,
                "Afterglow tail is not decaying (unexpected brightness increase)",
                {
                    "roi": roi,
                    "high_thresh": float(high_thresh),
                    "peak_frame": int(peak),
                    "pop_window": {"start": int(s), "end": int(e)},
                    "pop_segments": [{"start": int(a), "end": int(b)} for a, b in segments],
                    "tail": [float(v) for v in tail],
                    "tail_diffs": [float(v) for v in tail_diffs],
                    "max_tail_increase": float(max_tail_increase),
                },
            )

        if float(np.mean(tail[2:6])) < 4.0:
            return (
                False,
                "Afterglow tail fades too quickly (mean tail too low)",
                {
                    "roi": roi,
                    "high_thresh": float(high_thresh),
                    "peak_frame": int(peak),
                    "pop_window": {"start": int(s), "end": int(e)},
                    "pop_segments": [{"start": int(a), "end": int(b)} for a, b in segments],
                    "tail": [float(v) for v in tail],
                },
            )

        return (
            True,
            "Afterglow persistence detected (tail decays across frames)",
            {
                "roi": roi,
                "high_thresh": float(high_thresh),
                "peak_frame": int(peak),
                "pop_window": {"start": int(s), "end": int(e)},
                "pop_segments": [{"start": int(a), "end": int(b)} for a, b in segments],
                "tail": [float(v) for v in tail],
                "tail_diffs": [float(v) for v in tail_diffs] if tail_diffs.size else [],
                "max_tail_increase": float(max_tail_increase),
            },
        )
