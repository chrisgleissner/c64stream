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

            # Verify frame progression slots show consistent afterglow
            frames_bgr = frames[..., ::-1]  # Convert RGB to BGR for OpenCV
            slot_ok, slot_details = self._verify_frame_progression_slots(frames_bgr, properties, verbose)

            if not slot_ok:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Frame progression slot discontinuity: {slot_details}",
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message="Afterglow persistence verified (tail decays, slots consistent)",
                details={
                    "roi": {"x0": roi[0], "y0": roi[1], "x1": roi[2], "y1": roi[3]},
                    "decay_details": details,
                    "slot_details": slot_details,
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
        cmd = ["ffmpeg", "-v", "error", "-i", str(mp4_path), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
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

        if not np.all(np.diff(tail) <= 2.5):
            return False, "Afterglow tail is not decaying (unexpected brightness increase)"

        if float(np.mean(tail[2:6])) < 4.0:
            return False, "Afterglow tail fades too quickly (mean tail too low)"

        return True, "Afterglow persistence detected (tail decays across frames)"
    def _verify_frame_progression_slots(
        self, frames_bgr: np.ndarray, properties: dict[str, Any], verbose: bool
    ) -> tuple[bool, str]:
        """Verify frame progression slots show consistent afterglow without discontinuities.

        Checks that inactive slots in the frame progression marker show brightness indicating
        afterglow persistence. Discontinuities (slots suddenly going dark) indicate frames
        where afterglow was not applied.

        Args:
            frames_bgr: Video frames in BGR format [N,H,W,3] uint8
            properties: Source properties (may contain canvas size)
            verbose: Enable verbose logging

        Returns:
            (ok, details) tuple where ok=True if slots are consistent, details=message
        """
        import cv2

        # Detect content bounds from first frame
        from .frame_progression import _detect_content_bounds

        first_frame = frames_bgr[0]
        content_left, content_right, content_top, content_bottom = _detect_content_bounds(first_frame)

        h, w = frames_bgr.shape[1:3]
        c64_height = 272  # PAL/NTSC both scale from this
        scale = h / c64_height

        # Calculate slot region (from frame_progression.py logic)
        corner_outer_height_c64 = 56
        corner_frame_total_c64 = 8
        corner_inner_height_c64 = 40
        bar_left_padding_c64 = 4
        bar_area_width_c64 = 63  # 8 slots × 7px + 7 gaps × 1px
        slot_width_c64 = 7
        gap_width_c64 = 1
        slot_pitch_c64 = slot_width_c64 + gap_width_c64

        outer_height = int(round(corner_outer_height_c64 * scale))
        frame_offset = int(round(corner_frame_total_c64 * scale))
        inner_height = int(round(corner_inner_height_c64 * scale))
        bar_padding = int(round(bar_left_padding_c64 * scale))
        bar_area_width = int(round(bar_area_width_c64 * scale))

        element_bottom = content_bottom
        element_left = content_left
        element_top = element_bottom - outer_height
        inner_x0 = element_left + frame_offset
        inner_y0 = element_top + frame_offset
        bar_x0 = inner_x0 + bar_padding
        bar_x1 = bar_x0 + bar_area_width
        sample_y0 = inner_y0
        sample_y1 = inner_y0 + inner_height

        # Bounds check
        if bar_x0 < 0 or bar_x1 > w or sample_y0 < 0 or sample_y1 > h or bar_area_width < 8:
            return True, "Frame progression slots not visible (skipping slot check)"

        # Extract slot luminances across all frames
        num_slots = 8
        all_slot_lumas = []  # Shape: [N, 8] where N=num_frames

        for frame_idx in range(len(frames_bgr)):
            frame = frames_bgr[frame_idx]
            bar_region = frame[sample_y0:sample_y1, bar_x0:bar_x1]
            if bar_region.size == 0:
                continue
            gray = cv2.cvtColor(bar_region, cv2.COLOR_BGR2GRAY)

            slot_lumas = []
            for slot_idx in range(num_slots):
                slot_start = int(round(slot_idx * slot_pitch_c64 * scale))
                slot_end = int(round((slot_idx * slot_pitch_c64 + slot_width_c64) * scale))
                slot_end = min(slot_end, bar_area_width)

                if slot_end <= slot_start:
                    slot_lumas.append(0.0)
                    continue

                slot_region = gray[:, slot_start:slot_end]
                if slot_region.size == 0:
                    slot_lumas.append(0.0)
                    continue

                slot_lumas.append(float(np.mean(slot_region)))

            if len(slot_lumas) == num_slots:
                all_slot_lumas.append(slot_lumas)

        if len(all_slot_lumas) < 20:
            return True, "Not enough frames to verify slot consistency"

        # Convert to numpy array for analysis
        slot_array = np.array(all_slot_lumas)  # Shape: [N, 8]

        # Check for discontinuities: frames where inactive slots suddenly go dark (missing afterglow)
        # CRITICAL: When a source frame is skipped, the corresponding slot should STILL be updated
        # in the GPU afterglow accumulation buffer. If not, that slot stays dark, breaking the
        # expected smooth decay pattern from brightest (most recent) to dimmest (oldest).
        discontinuities = []

        for frame_idx in range(20, len(slot_array)):
            lumas = slot_array[frame_idx]

            # Find brightest slot (most recently updated)
            brightest_slot = int(np.argmax(lumas))
            max_luma = lumas[brightest_slot]

            if max_luma < 20:  # Skip frames with no bright content
                continue

            # Check for smooth decay pattern from brightest slot leftward (older slots)
            # Wrap around: if brightest is 3, check order: 3, 2, 1, 0, 7, 6, 5, 4
            for offset in range(1, 8):
                slot_idx = (brightest_slot - offset) % 8
                prev_slot_idx = (brightest_slot - offset + 1) % 8

                curr_luma = lumas[slot_idx]
                prev_luma = lumas[prev_slot_idx]

                # Expected: current slot should be dimmer (decay) or similar to previous slot
                # If current slot is WAY darker (>50% drop when previous was bright), it's stuck
                if prev_luma > 20:  # Only check if previous slot was bright enough
                    expected_min = prev_luma * 0.4  # Allow up to 60% decay
                    if curr_luma < expected_min:
                        discontinuities.append(
                            f"Frame {frame_idx}: Slot {slot_idx} abnormally dark ({curr_luma:.1f}) compared to "
                            f"slot {prev_slot_idx} ({prev_luma:.1f}) - {((prev_luma-curr_luma)/prev_luma*100):.0f}% drop. "
                            f"Likely missing render during frame skip."
                        )
                        break  # One discontinuity per frame is enough

        # Calculate discontinuity rate
        frames_checked = max(1, len(slot_array) - 20)
        discontinuity_rate = len(discontinuities) / frames_checked

        if discontinuity_rate > 0.01:  # Fail if >1% of frames have discontinuities (was 5%, now stricter)
            if verbose:
                for d in discontinuities[:10]:  # Log first 10 discontinuities
                    self.log(f"  {d}", True)
            return (
                False,
                f"Slot discontinuities detected: {len(discontinuities)}/{frames_checked} frames "
                f"({discontinuity_rate*100:.1f}%) exceed 1.0% threshold - slots not updated during frame skips",
            )

        # Also check that inactive slots show brightness (indicating afterglow exists)
        afterglow_detected = False
        for frame_idx in range(20, len(slot_array)):
            lumas = slot_array[frame_idx]
            current_slot = frame_idx % 8
            # Check if any non-current slot is significantly lit
            for slot_idx in range(8):
                if slot_idx != current_slot and lumas[slot_idx] > 15:  # Afterglow threshold
                    afterglow_detected = True
                    break
            if afterglow_detected:
                break

        if not afterglow_detected:
            return (
                False,
                "NO AFTERGLOW DETECTED: No inactive slots show brightness above threshold. "
                "Afterglow appears to be completely disabled or non-functional.",
            )

        return True, f"Frame progression slots consistent ({len(discontinuities)} discontinuities, {discontinuity_rate*100:.1f}%)"
