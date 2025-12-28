#!/usr/bin/env python3
"""
C64 Stream - Frame Progression Assertion
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


def _detect_content_bounds(frame: np.ndarray) -> tuple[int, int, int, int]:
    """Detect content bounds (left, right, top, bottom) using black bars around C64 content."""
    height, width = frame.shape[:2]
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    _, non_black = cv2.threshold(gray, 10, 255, cv2.THRESH_BINARY)

    col_counts = np.sum(non_black > 0, axis=0)
    col_thresh = max(1, int(0.10 * height))
    content_cols = np.where(col_counts >= col_thresh)[0]
    if content_cols.size >= 2:
        left_bound = int(content_cols[0])
        right_bound = int(content_cols[-1]) + 1
    else:
        scale_factor = height / 272.0
        scaled_c64_width = int(384 * scale_factor)
        left_bound = (width - scaled_c64_width) // 2
        right_bound = (width + scaled_c64_width) // 2

    row_counts = np.sum(non_black > 0, axis=1)
    row_thresh = max(1, int(0.10 * width))
    content_rows = np.where(row_counts >= row_thresh)[0]
    if content_rows.size >= 2:
        top_bound = int(content_rows[0])
        bottom_bound = int(content_rows[-1]) + 1
    else:
        top_bound = 0
        bottom_bound = height

    return left_bound, right_bound, top_bound, bottom_bound


def _detect_position_marker(
    frame: np.ndarray,
    content_left: int,
    content_bottom: int,
    scale: float,
    prev_slot_luminances: Optional[list[float]] = None,
) -> tuple[Optional[int], list[float]]:
    """Detect frame sequence from position marker strip at bottom-left.

    Uses temporal delta detection: finds which slot had the largest brightness INCREASE
    compared to the previous frame. This is robust against afterglow effects where
    previous positions remain lit.

    The position marker is inside a framed corner element at bottom-left:
    - Outer element size: 88x56 pixels (C64 coordinates) - matches C64 aspect ratio 1.57
    - Frame: 1px white outer + 7px black inner = 8px total border
    - Inner content: 72x40 pixels starting at offset (8, 8)
    - Position bar: 8 slots × 7px + 7 gaps × 1px = 63px, centered in 72px (4px left padding)

    Args:
        frame: BGR frame from video
        content_left: X coordinate of content left edge
        content_bottom: Y coordinate of content bottom edge
        scale: Scale factor from C64 to video coordinates
        prev_slot_luminances: Luminances from previous frame (for delta detection)

    Returns:
        Tuple of (detected slot index 0-7 or None, current slot luminances for next frame)
    """
    num_slots = 8
    slot_width_c64 = 7
    gap_width_c64 = 1
    slot_pitch_c64 = slot_width_c64 + gap_width_c64  # 8px per slot

    # Corner element dimensions in C64 coordinates (88x56 aspect ratio ~1.57)
    corner_outer_width_c64 = 88
    corner_outer_height_c64 = 56
    corner_frame_total_c64 = 8    # 1px white + 7px black border
    corner_inner_width_c64 = 72   # 88 - 16
    corner_inner_height_c64 = 40  # 56 - 16

    # Position bar dimensions in C64 coordinates (inside the 72x40 inner area)
    bar_area_width_c64 = 63  # 8 slots × 7px + 7 gaps × 1px
    bar_left_padding_c64 = 4  # Centered in 72px

    # Scale to video coordinates
    inner_width = int(round(corner_inner_width_c64 * scale))
    inner_height = int(round(corner_inner_height_c64 * scale))
    bar_area_width = int(round(bar_area_width_c64 * scale))
    slot_pitch = max(2, int(round(slot_pitch_c64 * scale)))
    slot_width = max(1, int(round(slot_width_c64 * scale)))
    frame_offset = int(round(corner_frame_total_c64 * scale))
    outer_height = int(round(corner_outer_height_c64 * scale))
    bar_padding = int(round(bar_left_padding_c64 * scale))

    # Calculate element and inner area location
    element_bottom = content_bottom
    element_left = content_left
    element_top = element_bottom - outer_height

    # Inner content area
    inner_x0 = element_left + frame_offset
    inner_y0 = element_top + frame_offset

    # Bar area is offset from left edge of inner area
    bar_x0 = inner_x0 + bar_padding
    bar_x1 = bar_x0 + bar_area_width

    # Sample the entire inner height
    sample_y0 = inner_y0
    sample_y1 = inner_y0 + inner_height

    # Bounds check
    h, w = frame.shape[:2]
    if bar_x0 < 0 or bar_x1 > w or sample_y0 < 0 or sample_y1 > h:
        return None, []
    if bar_area_width < 8:
        return None, []

    # Extract bar region and convert to grayscale
    bar_region = frame[sample_y0:sample_y1, bar_x0:bar_x1]
    if bar_region.size == 0:
        return None, []
    gray = cv2.cvtColor(bar_region, cv2.COLOR_BGR2GRAY)

    # Measure mean luminance for each slot (sample center of slot, avoiding gaps)
    slot_luminances = []
    for slot_idx in range(num_slots):
        # Calculate slot center position
        slot_start = int(round(slot_idx * slot_pitch_c64 * scale))
        slot_end = int(round((slot_idx * slot_pitch_c64 + slot_width_c64) * scale))
        slot_end = min(slot_end, bar_area_width)

        if slot_end <= slot_start:
            slot_luminances.append(0.0)
            continue

        slot_region = gray[:, slot_start:slot_end]
        if slot_region.size == 0:
            slot_luminances.append(0.0)
            continue

        mean_lum = float(np.mean(slot_region))
        slot_luminances.append(mean_lum)

    if not slot_luminances or len(slot_luminances) != num_slots:
        return None, slot_luminances

    # TEMPORAL DELTA DETECTION: Find slot with largest brightness increase
    if prev_slot_luminances and len(prev_slot_luminances) == num_slots:
        deltas = [curr - prev for curr, prev in zip(slot_luminances, prev_slot_luminances)]
        max_delta = max(deltas)

        # Need significant positive delta to detect new bar position
        if max_delta > 15:  # Threshold for "newly lit" slot
            detected_slot = int(np.argmax(deltas))
            # Verify this slot is reasonably bright (not just noise)
            if slot_luminances[detected_slot] > 50:
                return detected_slot, slot_luminances

    # FALLBACK: First frame or no clear delta - use absolute brightness
    max_lum = max(slot_luminances)
    min_lum = min(slot_luminances)

    # Require significant contrast
    if max_lum - min_lum < 30:
        return None, slot_luminances

    brightest_slot = int(np.argmax(slot_luminances))
    return brightest_slot, slot_luminances


def _group_consecutive_frames(frames: list[int]) -> list[int]:
    if not frames:
        return []
    frames_sorted = sorted(int(f) for f in frames)
    grouped = [frames_sorted[0]]
    last = frames_sorted[0]
    for f in frames_sorted[1:]:
        if (f - last) <= 1:
            pass
        else:
            grouped.append(f)
        last = f
    return grouped


@dataclass
class _AnalysisResult:
    status: AssertionStatus
    message: str
    details: dict[str, Any]
    metrics: dict[str, float]


def _analyze_frame_progression(
    mp4_path: Path,
    thresholds: dict[str, float],
    verbose: bool,
) -> _AnalysisResult:
    """Analyze frame sequence using position marker detection with temporal delta.

    The position marker is inside a framed corner element at bottom-left:
    - Outer element size: 88x56 pixels (C64 coordinates) - matches C64 aspect ratio
    - Frame: 1px white outer + 7px black inner = 8px total border
    - Inner content: 72x40 pixels starting at offset (8, 8)
    - Position bar: 8 slots × 7px + 7 gaps × 1px = 63px, centered in 72px

    Uses temporal delta detection: finds which slot had the largest brightness
    increase compared to the previous frame. This is robust against afterglow
    effects where previous positions remain lit. Works for ALL presets including
    monochrome (Green Monitor, Amber Monitor) because it uses luminance only.
    """
    cap = cv2.VideoCapture(str(mp4_path))
    if not cap.isOpened():
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message="Could not open video",
            details={"path": str(mp4_path)},
            metrics={},
        )

    fps = float(cap.get(cv2.CAP_PROP_FPS) or 30.0)
    frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)

    # Primary method: detect content boundaries using frame difference analysis.
    try:
        from .content_bounds import detect_content_bounds_precise
    except ImportError:
        try:
            from content_bounds import detect_content_bounds_precise
        except ImportError:
            detect_content_bounds_precise = None

    content_bounds = None
    if detect_content_bounds_precise is not None:
        content_bounds = detect_content_bounds_precise(mp4_path, verbose=verbose)
        if verbose and content_bounds:
            print(f"[frame_progression] Content bounds detected: frames {content_bounds.first_content_frame}-{content_bounds.last_content_frame}")

    # Fallback method: detect video pops (requires test pattern with pops).
    try:
        from test_av_sync import detect_video_pops
    except Exception:
        try:
            from ..test_av_sync import detect_video_pops  # type: ignore
        except Exception:
            detect_video_pops = None

    pop_starts: list[int] = []
    if detect_video_pops is not None:
        pop_frames = detect_video_pops(str(mp4_path), frame_rate=fps)
        pop_starts = _group_consecutive_frames([int(f) for f in pop_frames])

    # Determine analysis window
    max_seconds = float(thresholds.get("max_seconds", 8.0))
    max_frames = int(max(1, round(max_seconds * fps)))

    if content_bounds is not None and content_bounds.detection_confidence > 0.3:
        start_frame = content_bounds.first_content_frame
        end_frame = min(content_bounds.last_content_frame, start_frame + max_frames - 1)
        if verbose:
            print(f"[frame_progression] Using content bounds: {start_frame}-{end_frame}")
    elif len(pop_starts) >= 1:
        start_frame = min(frame_count - 1, max(0, int(pop_starts[0] + 1)))
        end_frame = min(frame_count - 1, start_frame + max_frames - 1)
        if verbose:
            print(f"[frame_progression] Using video pops: {start_frame}-{end_frame}")
    else:
        cap.release()
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message="Could not detect content boundaries or video pops",
            details={
                "content_bounds": content_bounds.__dict__ if content_bounds else None,
                "video_pop_starts": pop_starts,
            },
            metrics={"video_pop_count": float(len(pop_starts))},
        )

    start_frame = min(frame_count - 1, max(0, start_frame))
    end_frame = min(frame_count - 1, end_frame)

    if end_frame <= start_frame:
        cap.release()
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message="Invalid analysis window",
            details={
                "start_frame": start_frame,
                "end_frame": end_frame,
                "content_bounds": content_bounds.__dict__ if content_bounds else None,
                "video_pop_starts": pop_starts,
                "frame_count": frame_count,
            },
            metrics={},
        )

    # Read first frame to detect content bounds
    cap.set(cv2.CAP_PROP_POS_FRAMES, start_frame)
    ret, frame0 = cap.read()
    if not ret:
        cap.release()
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message="Could not seek to analysis start",
            details={"start_frame": start_frame},
            metrics={},
        )

    # Detect content bounds from frame
    left, right, top, bottom = _detect_content_bounds(frame0)
    content_w = max(1, right - left)
    content_h = max(1, bottom - top)
    scale = float(content_w) / 384.0

    if verbose:
        print(f"[frame_progression] Content: left={left}, right={right}, top={top}, bottom={bottom}, scale={scale:.3f}")

    # Reset to start for analysis loop
    cap.set(cv2.CAP_PROP_POS_FRAMES, start_frame)

    indices: list[int] = []
    index_frame_nums: list[int] = []
    ambiguous = 0
    analyzed = 0
    prev_slot_luminances: list[float] = []  # For temporal delta detection

    current_frame = start_frame
    while True:
        ret, frame = cap.read()
        if not ret:
            break

        analyzed += 1

        # Detect position marker using temporal delta detection (bottom-left of content)
        slot, prev_slot_luminances = _detect_position_marker(frame, left, bottom, scale, prev_slot_luminances)
        if slot is not None:
            indices.append(slot)
            index_frame_nums.append(current_frame)
        else:
            ambiguous += 1

        if current_frame >= end_frame:
            break
        current_frame += 1

    cap.release()

    valid = len(indices)
    ambiguous_ratio = float(ambiguous) / float(max(1, analyzed))

    if valid < int(max(10, round(0.5 * fps))):
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message="Too few valid marker samples",
            details={
                "valid_frames": valid,
                "analyzed_frames": analyzed,
                "ambiguous_frames": ambiguous,
                "ambiguous_ratio": ambiguous_ratio,
                "bounds": {"left": left, "right": right, "top": top, "bottom": bottom},
                "scale": scale,
            },
            metrics={"valid_frames": float(valid), "ambiguous_ratio": ambiguous_ratio},
        )

    # Compress consecutive duplicates and track stuck runs
    compressed: list[int] = []
    compressed_frame_nums: list[int] = []
    stuck_runs: list[int] = []
    stuck_run_frames: list[int] = []
    stuck_run_indices: list[int] = []
    current_run = 1
    current_run_start = index_frame_nums[0] if index_frame_nums else start_frame
    current_run_idx = 0
    for i, idx in enumerate(indices):
        frame_num = index_frame_nums[i]
        if not compressed or idx != compressed[-1]:
            if compressed:
                stuck_runs.append(current_run)
                stuck_run_frames.append(current_run_start)
                stuck_run_indices.append(current_run_idx)
            compressed.append(idx)
            compressed_frame_nums.append(frame_num)
            current_run = 1
            current_run_start = frame_num
            current_run_idx = i
        else:
            current_run += 1
    if indices:
        stuck_runs.append(current_run)
        stuck_run_frames.append(current_run_start)
        stuck_run_indices.append(current_run_idx)

    # Filter out startup/end freeze periods
    startup_frames_excluded = 0
    end_frames_excluded = 0
    startup_run_threshold = int(fps * 1.0)
    filtered_stuck_runs = stuck_runs.copy()
    filtered_stuck_run_frames = stuck_run_frames.copy()
    filtered_stuck_run_indices = stuck_run_indices.copy()

    # Filter startup: first run is long and subsequent content shows progression
    if len(filtered_stuck_runs) >= 2 and filtered_stuck_runs[0] > startup_run_threshold:
        remaining_distinct = len(set(compressed[1:]))
        if remaining_distinct >= 4:
            startup_frames_excluded = filtered_stuck_runs[0]
            filtered_stuck_runs = filtered_stuck_runs[1:]
            filtered_stuck_run_frames = filtered_stuck_run_frames[1:]
            filtered_stuck_run_indices = filtered_stuck_run_indices[1:]

    # Filter end-of-stream: last run is long
    if len(filtered_stuck_runs) >= 2 and filtered_stuck_runs[-1] > startup_run_threshold:
        preceding_distinct = len(set(compressed[:-1]))
        if preceding_distinct >= 4:
            end_frames_excluded = filtered_stuck_runs[-1]
            filtered_stuck_runs = filtered_stuck_runs[:-1]
            filtered_stuck_run_frames = filtered_stuck_run_frames[:-1]
            filtered_stuck_run_indices = filtered_stuck_run_indices[:-1]

    # Calculate stuck frame statistics
    max_stuck_run = max(filtered_stuck_runs) if filtered_stuck_runs else 0
    total_stuck_frames = sum(r - 1 for r in filtered_stuck_runs)
    effective_valid = max(1, valid - startup_frames_excluded - end_frames_excluded)
    stuck_ratio = float(total_stuck_frames) / float(effective_valid)

    # Build detailed list of repeated frame events
    repeated_events: list[dict] = []
    for run_len, run_idx in zip(filtered_stuck_runs, filtered_stuck_run_indices):
        if run_len > 1:
            second_frame_idx = run_idx + 1
            if second_frame_idx < len(index_frame_nums):
                repeat_frame = index_frame_nums[second_frame_idx]
                time_sec = float(repeat_frame) / fps
                repeated_events.append({
                    "frame": repeat_frame,
                    "time_sec": round(time_sec, 3),
                    "count": run_len,
                })

    # Calculate min/median/max for stuck runs
    repeated_runs = [r for r in filtered_stuck_runs if r > 1]
    if repeated_runs:
        min_stuck_run = min(repeated_runs)
        median_stuck_run = int(np.median(repeated_runs))
        max_stuck_run_stat = max(repeated_runs)
        repeated_run_count = len(repeated_runs)
    else:
        min_stuck_run = 0
        median_stuck_run = 0
        max_stuck_run_stat = 0
        repeated_run_count = 0

    distinct = len(set(indices))
    skips = 0
    skip_sizes: list[int] = []
    skip_events: list[dict] = []
    back_steps = 0
    severe_steps = 0
    delta_hist: dict[int, int] = {}
    max_skip_delta = int(thresholds.get("max_skip_delta", 4))
    num_slots = 8  # 8 slots in the progress bar
    for i, (prev, cur) in enumerate(zip(compressed, compressed[1:])):
        delta = int((cur - prev) % num_slots)
        delta_hist[delta] = delta_hist.get(delta, 0) + 1
        skip_frame = compressed_frame_nums[i + 1] if i + 1 < len(compressed_frame_nums) else 0
        if delta == 1:
            continue
        if 2 <= delta <= max_skip_delta:
            skip_amount = delta - 1
            skips += skip_amount
            skip_sizes.append(skip_amount)
            time_sec = float(skip_frame) / fps
            skip_events.append({
                "frame": skip_frame,
                "time_sec": round(time_sec, 3),
                "skipped": skip_amount,
            })
            continue
        if delta == (num_slots - 1):  # Going backwards by 1 slot (7 for 8 slots)
            back_steps += 1
            continue
        severe_steps += 1

    # Calculate skip statistics
    if skip_sizes:
        min_skip = min(skip_sizes)
        median_skip = int(np.median(skip_sizes))
        max_skip = max(skip_sizes)
        skip_count = len(skip_sizes)
    else:
        min_skip = 0
        median_skip = 0
        max_skip = 0
        skip_count = 0

    # Require full coverage when we have enough changes
    min_changes_for_full = int(thresholds.get("min_changes_for_full", 20))
    require_full = len(compressed) >= min_changes_for_full
    min_full_slots = int(thresholds.get("min_full_coverage_colors", 8))
    full_ok = (distinct >= min_full_slots) if require_full else (distinct >= 8)

    max_skips = int(thresholds.get("max_skips", 60))
    max_back_steps = int(thresholds.get("max_back_steps", 3))
    max_severe_steps = int(thresholds.get("max_severe_steps", 0))
    max_ambiguous_ratio = float(thresholds.get("max_ambiguous_ratio", 0.30))

    details = {
        "window": {"start_frame": start_frame, "end_frame": end_frame, "fps": fps},
        "content_bounds": {
            "first_content_frame": content_bounds.first_content_frame,
            "last_content_frame": content_bounds.last_content_frame,
            "logo_end_frame": content_bounds.logo_end_frame,
            "detection_confidence": content_bounds.detection_confidence,
        } if content_bounds else None,
        "video_pop_starts": pop_starts,
        "analyzed_frames": analyzed,
        "valid_frames": valid,
        "compressed_len": len(compressed),
        "distinct_slots": distinct,
        # Map from video frame number to detected slot index (0-15).
        "frame_slots": {int(fn): int(idx) for fn, idx in zip(index_frame_nums, indices)},
        "skips": skips,
        "skip_stats": {"count": skip_count, "min": min_skip, "median": median_skip, "max": max_skip},
        "skip_events": skip_events,
        "back_steps": back_steps,
        "severe_steps": severe_steps,
        "delta_hist": delta_hist,
        "ambiguous_frames": ambiguous,
        "ambiguous_ratio": ambiguous_ratio,
        "stuck_frames": total_stuck_frames,
        "stuck_ratio": stuck_ratio,
        "max_stuck_run": max_stuck_run,
        "startup_frames_excluded": startup_frames_excluded,
        "end_frames_excluded": end_frames_excluded,
        "stuck_stats": {
            "count": repeated_run_count,
            "min": min_stuck_run,
            "median": median_stuck_run,
            "max": max_stuck_run_stat,
        },
        "repeated_events": repeated_events,
        "bounds": {"left": left, "right": right, "top": top, "bottom": bottom},
        "scale": scale,
    }

    metrics = {
        "valid_frames": float(valid),
        "distinct_slots": float(distinct),
        "skips": float(skips),
        "skip_count": float(skip_count),
        "skip_min": float(min_skip),
        "skip_median": float(median_skip),
        "skip_max": float(max_skip),
        "back_steps": float(back_steps),
        "severe_steps": float(severe_steps),
        "ambiguous_ratio": float(ambiguous_ratio),
        "stuck_frames": float(total_stuck_frames),
        "stuck_ratio": float(stuck_ratio),
        "stuck_run_count": float(repeated_run_count),
        "stuck_run_min": float(min_stuck_run),
        "stuck_run_median": float(median_stuck_run),
        "max_stuck_run": float(max_stuck_run),
    }

    # Check for excessive stuck frames (video stream freeze)
    max_stuck_ratio = float(thresholds.get("max_stuck_ratio", 0.50))
    max_stuck_run_frames = int(thresholds.get("max_stuck_run_frames", int(fps * 2)))

    if max_stuck_run > max_stuck_run_frames:
        return _AnalysisResult(
            status=AssertionStatus.WARNING,
            message=f"Video stream froze for {max_stuck_run} frames ({max_stuck_run/fps:.1f}s)",
            details=details,
            metrics=metrics,
        )

    if stuck_ratio > max_stuck_ratio:
        return _AnalysisResult(
            status=AssertionStatus.WARNING,
            message=f"High frame repetition ({stuck_ratio*100:.0f}% stuck)",
            details=details,
            metrics=metrics,
        )

    if ambiguous_ratio > max_ambiguous_ratio:
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message="Marker sampling too ambiguous",
            details=details,
            metrics=metrics,
        )

    if not full_ok:
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message=f"Position marker did not cover full range (distinct={distinct})",
            details=details,
            metrics=metrics,
        )

    if severe_steps > max_severe_steps:
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message=f"Frame sequence out of order (severe_steps={severe_steps})",
            details=details,
            metrics=metrics,
        )

    if skips > max_skips or back_steps > max_back_steps:
        return _AnalysisResult(
            status=AssertionStatus.WARNING,
            message=f"Frame sequence OK with jitter (skips={skips}, back_steps={back_steps})",
            details=details,
            metrics=metrics,
        )

    return _AnalysisResult(
        status=AssertionStatus.PASS,
        message="Frame sequence verified",
        details=details,
        metrics=metrics,
    )


class FrameBoxSequenceAssertion(EffectAssertion):
    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        super().__init__("Frame Box Seq", thresholds)
        self.thresholds = {
            "max_seconds": 8.0,
            "max_ambiguous_ratio": 0.30,
            "min_changes_for_full": 20,
            "max_skip_delta": 4,
            "max_skips": 60,
            "max_back_steps": 3,
            # Allow up to 5 severe steps to handle CI timing variability.
            "max_severe_steps": 5,
            # Minimum distinct slots for full coverage (8 slots in progress bar).
            "min_full_coverage_colors": 8,
            **(thresholds or {}),
        }

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: Any, verbose: bool = False
    ) -> AssertionResult:
        res = _analyze_frame_progression(mp4_path, self.thresholds, verbose)
        return AssertionResult(
            status=res.status,
            name=self.name,
            message=res.message,
            details=res.details,
            metrics=res.metrics,
        )
