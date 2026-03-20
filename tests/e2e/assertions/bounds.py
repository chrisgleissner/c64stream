#!/usr/bin/env python3
"""
C64 Stream - Content Bounds Assertions
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional

import cv2
import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


@dataclass
class FrameBounds:
    time_s: float
    left: int
    right: int
    top: int
    bottom: int

    @property
    def width(self) -> int:
        return self.right - self.left

    @property
    def height(self) -> int:
        return self.bottom - self.top

    @property
    def center_x(self) -> float:
        return (self.left + self.right) / 2.0

    @property
    def center_y(self) -> float:
        return (self.top + self.bottom) / 2.0


def _sample_times(duration_s: float, *, samples: int, lead_in_s: float, tail_s: float) -> list[float]:
    if duration_s <= 0.0:
        return []

    start = min(max(0.5, lead_in_s), duration_s)
    end = max(start, duration_s - max(0.5, tail_s))
    if samples <= 1 or end <= start:
        return [start]

    step = (end - start) / float(samples - 1)
    return [start + (step * i) for i in range(samples)]


def _detect_frame_bounds(frame: np.ndarray) -> Optional[FrameBounds]:
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    height, width = gray.shape

    thresh = max(10.0, float(np.percentile(gray, 99.0) * 0.18))
    mask = np.zeros_like(gray, dtype=np.uint8)
    mask[gray > thresh] = 255

    kernel = np.ones((5, 5), dtype=np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)

    component_count, labels, stats, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
    if component_count <= 1:
        return None

    best_index = -1
    best_area = 0
    for index in range(1, component_count):
        area = int(stats[index, cv2.CC_STAT_AREA])
        if area <= best_area:
            continue

        left = int(stats[index, cv2.CC_STAT_LEFT])
        top = int(stats[index, cv2.CC_STAT_TOP])
        width_px = int(stats[index, cv2.CC_STAT_WIDTH])
        height_px = int(stats[index, cv2.CC_STAT_HEIGHT])
        right = left + width_px
        bottom = top + height_px

        # Prefer a large connected component that spans the central content area.
        if right <= width * 0.25 or left >= width * 0.75:
            continue
        if bottom <= height * 0.25 or top >= height * 0.75:
            continue

        best_index = index
        best_area = area

    if best_index < 0:
        return None

    left = int(stats[best_index, cv2.CC_STAT_LEFT])
    top = int(stats[best_index, cv2.CC_STAT_TOP])
    right = left + int(stats[best_index, cv2.CC_STAT_WIDTH])
    bottom = top + int(stats[best_index, cv2.CC_STAT_HEIGHT])

    left = max(0, min(width - 1, left))
    right = max(left + 1, min(width, right))
    top = max(0, min(height - 1, top))
    bottom = max(top + 1, min(height, bottom))

    return FrameBounds(0.0, left, right, top, bottom)


def _collect_bounds(mp4_path: Path, *, samples: int, lead_in_s: float, tail_s: float) -> tuple[list[FrameBounds], dict[str, Any]]:
    cap = cv2.VideoCapture(str(mp4_path))
    if not cap.isOpened():
        raise RuntimeError(f"Could not open recording: {mp4_path}")

    try:
        fps = float(cap.get(cv2.CAP_PROP_FPS) or 0.0)
        total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
        duration_s = (total_frames / fps) if fps > 0.0 else 0.0

        sampled: list[FrameBounds] = []
        attempted: list[float] = []
        for time_s in _sample_times(duration_s, samples=samples, lead_in_s=lead_in_s, tail_s=tail_s):
            attempted.append(float(time_s))
            cap.set(cv2.CAP_PROP_POS_MSEC, float(time_s) * 1000.0)
            ok, frame = cap.read()
            if not ok or frame is None:
                continue
            bounds = _detect_frame_bounds(frame)
            if bounds is None:
                continue
            bounds.time_s = float(time_s)
            sampled.append(bounds)

        return sampled, {
            "fps": fps,
            "total_frames": total_frames,
            "duration_s": duration_s,
            "attempted_times_s": attempted,
        }
    finally:
        cap.release()


def _summarize_bounds(bounds: list[FrameBounds]) -> dict[str, Any]:
    widths = [b.width for b in bounds]
    heights = [b.height for b in bounds]
    centers_x = [b.center_x for b in bounds]
    centers_y = [b.center_y for b in bounds]
    lefts = [b.left for b in bounds]
    rights = [b.right for b in bounds]
    tops = [b.top for b in bounds]
    bottoms = [b.bottom for b in bounds]

    return {
        "sample_count": len(bounds),
        "width_range_px": max(widths) - min(widths),
        "height_range_px": max(heights) - min(heights),
        "center_x_range_px": max(centers_x) - min(centers_x),
        "center_y_range_px": max(centers_y) - min(centers_y),
        "left_range_px": max(lefts) - min(lefts),
        "right_range_px": max(rights) - min(rights),
        "top_range_px": max(tops) - min(tops),
        "bottom_range_px": max(bottoms) - min(bottoms),
        "samples": [
            {
                "time_s": round(b.time_s, 3),
                "left": b.left,
                "right": b.right,
                "top": b.top,
                "bottom": b.bottom,
                "width": b.width,
                "height": b.height,
                "center_x": round(b.center_x, 3),
                "center_y": round(b.center_y, 3),
            }
            for b in bounds
        ],
    }


class BoundsStabilityAssertion(EffectAssertion):
    """Verify that the visible content footprint stays stable across the recording."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "samples": 8,
            "lead_in_s": 2.0,
            "tail_s": 2.0,
            "min_samples": 4,
            "max_width_delta_px": 1.0,
            "max_height_delta_px": 1.0,
            "max_center_drift_px": 1.0,
        }
        super().__init__("Bounds Stability", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        del properties
        del preset
        del verbose
        try:
            bounds, details = _collect_bounds(
                mp4_path,
                samples=int(self.thresholds["samples"]),
                lead_in_s=float(self.thresholds["lead_in_s"]),
                tail_s=float(self.thresholds["tail_s"]),
            )
            if len(bounds) < int(self.thresholds["min_samples"]):
                return AssertionResult(
                    status=AssertionStatus.SKIP,
                    name=self.name,
                    message=f"Too few valid frame samples for bounds analysis ({len(bounds)})",
                    details=details,
                )

            summary = _summarize_bounds(bounds)
            details.update(summary)

            width_delta = float(summary["width_range_px"])
            height_delta = float(summary["height_range_px"])
            center_delta = max(float(summary["center_x_range_px"]), float(summary["center_y_range_px"]))

            max_width = float(self.thresholds["max_width_delta_px"])
            max_height = float(self.thresholds["max_height_delta_px"])
            max_center = float(self.thresholds["max_center_drift_px"])

            if width_delta > max_width or height_delta > max_height or center_delta > max_center:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=(
                        f"Bounds drifted: width Δ={width_delta:.1f}px, height Δ={height_delta:.1f}px, "
                        f"center Δ={center_delta:.1f}px"
                    ),
                    details=details,
                    metrics={
                        "width_delta_px": width_delta,
                        "height_delta_px": height_delta,
                        "center_delta_px": center_delta,
                    },
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=(
                    f"Bounds stable: width Δ={width_delta:.1f}px, height Δ={height_delta:.1f}px, "
                    f"center Δ={center_delta:.1f}px"
                ),
                details=details,
                metrics={
                    "width_delta_px": width_delta,
                    "height_delta_px": height_delta,
                    "center_delta_px": center_delta,
                },
            )
        except Exception as exc:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Bounds stability verification failed: {exc}",
            )


class BoundsVariationAssertion(EffectAssertion):
    """Verify that visible bounds change materially while remaining centered."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "samples": 8,
            "lead_in_s": 2.0,
            "tail_s": 2.0,
            "min_samples": 4,
            "min_size_delta_px": 32.0,
            "max_center_drift_px": 1.0,
        }
        super().__init__("Bounds Variation", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        del properties
        del preset
        del verbose
        try:
            bounds, details = _collect_bounds(
                mp4_path,
                samples=int(self.thresholds["samples"]),
                lead_in_s=float(self.thresholds["lead_in_s"]),
                tail_s=float(self.thresholds["tail_s"]),
            )
            if len(bounds) < int(self.thresholds["min_samples"]):
                return AssertionResult(
                    status=AssertionStatus.SKIP,
                    name=self.name,
                    message=f"Too few valid frame samples for bounds analysis ({len(bounds)})",
                    details=details,
                )

            summary = _summarize_bounds(bounds)
            details.update(summary)

            size_delta = max(float(summary["width_range_px"]), float(summary["height_range_px"]))
            center_delta = max(float(summary["center_x_range_px"]), float(summary["center_y_range_px"]))

            min_size = float(self.thresholds["min_size_delta_px"])
            max_center = float(self.thresholds["max_center_drift_px"])

            if size_delta < min_size:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Bounds did not change enough: size Δ={size_delta:.1f}px (min {min_size:.1f}px)",
                    details=details,
                    metrics={"size_delta_px": size_delta, "center_delta_px": center_delta},
                )

            if center_delta > max_center:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Bounds changed but drifted off-center: center Δ={center_delta:.1f}px",
                    details=details,
                    metrics={"size_delta_px": size_delta, "center_delta_px": center_delta},
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"Bounds changed as expected: size Δ={size_delta:.1f}px, center Δ={center_delta:.1f}px",
                details=details,
                metrics={"size_delta_px": size_delta, "center_delta_px": center_delta},
            )
        except Exception as exc:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Bounds variation verification failed: {exc}",
            )
