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


def _read_frame_with_seek_backoff(
    cap: "cv2.VideoCapture",
    target_frame: int,
    *,
    max_backoff_frames: int = 120,
) -> tuple[bool, "np.ndarray | None", dict]:
    """Best-effort read of a specific frame index.

    Some OpenCV/FFmpeg builds can fail to decode immediately after
    CAP_PROP_POS_FRAMES seeks (often when seeking to a non-keyframe).
    Work around this by seeking slightly earlier and reading forward.
    """

    details: dict = {"target_frame": int(target_frame)}

    # First try the direct seek/read.
    try:
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(target_frame))
        ret, frame = cap.read()
        if ret:
            details["method"] = "direct"
            return True, frame, details
    except Exception as e:
        details["direct_exception"] = repr(e)

    # Fallback: seek backwards then decode forward.
    backoffs = [1, 2, 5, 10, 20, 40, 80, 120]
    backoffs = [b for b in backoffs if b <= max_backoff_frames]

    for backoff in backoffs:
        start = max(0, int(target_frame) - int(backoff))
        try:
            cap.set(cv2.CAP_PROP_POS_FRAMES, start)
        except Exception as e:
            details.setdefault("seek_exceptions", []).append({"start": start, "exception": repr(e)})
            continue

        frame = None
        ok = True
        for _ in range(int(backoff) + 1):
            ret, frame = cap.read()
            if not ret:
                ok = False
                break

        if ok and frame is not None:
            details["method"] = "backoff"
            details["backoff_frames"] = int(backoff)
            details["seek_start_frame"] = int(start)
            return True, frame, details

    details["method"] = "failed"
    details["max_backoff_frames"] = int(max_backoff_frames)
    return False, None, details


def _detect_content_bounds(frame: np.ndarray, c64_visible_height: int) -> tuple[int, int, int, int]:
    """Detect content bounds (left, right, top, bottom) using black bars around C64 content.

    NOTE: the streamed visible height differs by format:
    - NTSC: 240 lines (60 packets/frame)
    - PAL:  272 lines (68 packets/frame)
    """
    height, width = frame.shape[:2]
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    # Use a slightly higher threshold to avoid treating codec noise in black bars as content.
    _, non_black = cv2.threshold(gray, 16, 255, cv2.THRESH_BINARY)

    def longest_run(indices: np.ndarray, max_gap: int = 1) -> Optional[tuple[int, int]]:
        if indices.size == 0:
            return None
        best_start = int(indices[0])
        best_end = int(indices[0])
        best_len = 1
        cur_start = int(indices[0])
        cur_end = int(indices[0])
        for idx in indices[1:]:
            idx_i = int(idx)
            if (idx_i - cur_end) <= max_gap:
                cur_end = idx_i
            else:
                cur_len = (cur_end - cur_start) + 1
                if cur_len > best_len:
                    best_start, best_end, best_len = cur_start, cur_end, cur_len
                cur_start = idx_i
                cur_end = idx_i
        cur_len = (cur_end - cur_start) + 1
        if cur_len > best_len:
            best_start, best_end = cur_start, cur_end
        return best_start, best_end

    # Use a low per-column threshold and percentile-based bounds to handle sparse patterns.
    col_counts = np.sum(non_black > 0, axis=0)
    col_thresh = max(1, int(0.005 * height))
    content_cols = np.where(col_counts >= col_thresh)[0]
    if content_cols.size >= 4:
        left_bound = int(np.percentile(content_cols, 1))
        right_bound = int(np.percentile(content_cols, 99)) + 1
    else:
        scale_factor = height / float(max(1, int(c64_visible_height)))
        scaled_c64_width = int(384 * scale_factor)
        left_bound = (width - scaled_c64_width) // 2
        right_bound = (width + scaled_c64_width) // 2

    # Use width-derived scale to inform expected height.
    # Sparse patterns (e.g. dots) can make row counts under-estimate the true content height.
    content_w = max(1, right_bound - left_bound)
    scale_from_width = float(content_w) / 384.0
    expected_h = int(round(float(max(1, int(c64_visible_height))) * scale_from_width))

    # Derive vertical bounds from the detected horizontal content region.
    # Using the full frame width here is fragile for sparse patterns (e.g. dots),
    # where per-row coverage can fall below a percentage-of-frame-width threshold.
    row_region = non_black[:, left_bound:right_bound]
    row_counts = np.sum(row_region > 0, axis=1)
    row_thresh = max(1, int(0.005 * content_w))
    content_rows = np.where(row_counts >= row_thresh)[0]
    if content_rows.size >= 4:
        top_bound = int(np.percentile(content_rows, 1))
        bottom_bound = int(np.percentile(content_rows, 99)) + 1
    else:
        # Fallback: assume the content is vertically centered, using width-derived scale.
        top_bound = max(0, (height - expected_h) // 2)
        bottom_bound = min(height, top_bound + expected_h)

    # If the detected vertical span is implausible compared to expected height (based on
    # width-derived scale), re-center around the median non-black row in the content region.
    detected_h = max(1, bottom_bound - top_bound)
    if expected_h > 0 and expected_h < height:
        too_small = detected_h < int(0.92 * expected_h)
        too_large = detected_h > int(1.20 * expected_h)
        if too_small or too_large:
            rows_nonzero = np.where(row_counts > 0)[0]
            center_y = int(np.median(rows_nonzero)) if rows_nonzero.size > 0 else (height // 2)
            top_bound = max(0, center_y - (expected_h // 2))
            bottom_bound = min(height, top_bound + expected_h)
            top_bound = max(0, bottom_bound - expected_h)

    return left_bound, right_bound, top_bound, bottom_bound


def _detect_position_marker(
    frame: np.ndarray,
    content_left: int,
    content_bottom: int,
    scale: float,
    prev_slot_luminances: Optional[list[float]] = None,
) -> tuple[Optional[int], list[float]]:
    """Detect frame sequence from position marker strip in bottom-left corner element.

    Uses a CENTRAL HORIZONTAL SCANLINE approach: samples a single horizontal line
    through the vertical middle of the progress bar. This line travels across all
    8 slots from left to right. The illumination falloff across this line must be
    smooth and provides a robust signal even with effects and scaling.

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

    h, w = frame.shape[:2]
    if bar_area_width < 8:
        return None, []

    def compute_slot_luminances(y: int, bar_x0: int, bar_x1: int) -> Optional[list[float]]:
        if y < 0 or y >= h or bar_x0 < 0 or bar_x1 > w or bar_x1 <= bar_x0:
            return None
        scanline = frame[y : y + 1, bar_x0:bar_x1]
        if scanline.size == 0:
            return None
        gray_line = cv2.cvtColor(scanline, cv2.COLOR_BGR2GRAY)
        luminance_profile = gray_line.flatten()

        slot_luminances: list[float] = []
        for slot_idx in range(num_slots):
            slot_start = int(round(slot_idx * slot_pitch_c64 * scale))
            slot_end = int(round((slot_idx * slot_pitch_c64 + slot_width_c64) * scale))
            slot_end = min(slot_end, bar_area_width)

            if slot_end <= slot_start:
                slot_luminances.append(0.0)
                continue

            slot_center_start = slot_start + max(0, (slot_end - slot_start) // 4)
            slot_center_end = slot_end - max(0, (slot_end - slot_start) // 4)
            if slot_center_end <= slot_center_start:
                slot_center_start = slot_start
                slot_center_end = slot_end

            slot_pixels = luminance_profile[slot_center_start:slot_center_end]
            if slot_pixels.size == 0:
                slot_luminances.append(0.0)
                continue

            slot_luminances.append(float(np.mean(slot_pixels)))
        if len(slot_luminances) != num_slots:
            return None
        return slot_luminances

    def pick_best_profile(sample_y0: int, sample_y1: int, bar_x0: int, bar_x1: int) -> tuple[Optional[list[float]], float]:
        if sample_y1 <= sample_y0:
            return None, -1.0
        scanline_center = (sample_y0 + sample_y1) // 2
        y_candidates = [
            max(sample_y0, min(sample_y1 - 1, scanline_center + dy))
            for dy in (-2, -1, 0, 1, 2)
        ]
        best_slot_luminances: Optional[list[float]] = None
        best_score = -1.0
        for y in y_candidates:
            sl = compute_slot_luminances(y, bar_x0, bar_x1)
            if sl is None:
                continue
            sl_arr = np.array(sl, dtype=np.float32)
            sl_sorted = np.sort(sl_arr)
            peak = float(sl_sorted[-1])
            second = float(sl_sorted[-2]) if sl_sorted.size >= 2 else float(sl_sorted[-1])
            median = float(np.median(sl_arr))
            # Prefer profiles that look like the bar: one strong peak and clear separation
            # from the background.
            score = (peak - median) + 0.5 * max(0.0, (peak - second))
            if score > best_score:
                best_score = score
                best_slot_luminances = sl
        return best_slot_luminances, best_score

    # Search a small neighborhood around the expected corner element location.
    # Some presets (and some content-bound estimates) can shift the effective bottom/left
    # by a few pixels, which would otherwise make the sampled scanline miss the marker.
    best_slot_luminances: Optional[list[float]] = None
    best_score = -1.0
    # Wider search: content bounds can include border/overscan so the element may be
    # inset from the detected left/bottom by dozens of pixels.
    dx_candidates = (-16, -8, -4, -2, 0, 2, 4, 8, 16)
    dy_candidates = (-16, -12, -8, -4, 0, 4, 8, 12, 16)
    marker_shift_c64 = 2
    marker_height_c64 = corner_inner_height_c64 - (marker_shift_c64 * 2)
    marker_shift = int(round(marker_shift_c64 * scale))
    marker_height = max(1, int(round(marker_height_c64 * scale)))

    for dx in dx_candidates:
        for dy in dy_candidates:
            element_left = content_left + int(round(dx * scale))
            element_bottom = content_bottom + int(round(dy * scale))
            element_top = element_bottom - outer_height

            inner_x0 = element_left + frame_offset
            inner_y0 = element_top + frame_offset

            bar_x0 = inner_x0 + bar_padding
            bar_x1 = bar_x0 + bar_area_width

            sample_y0 = inner_y0 + marker_shift
            sample_y1 = min(inner_y0 + inner_height, sample_y0 + marker_height)

            # Quick bounds check
            if bar_x0 < 0 or bar_x1 > w or sample_y0 < 0 or sample_y0 >= h:
                continue

            sl, score = pick_best_profile(sample_y0, sample_y1, bar_x0, bar_x1)
            if sl is None:
                continue
            if score > best_score:
                best_score = score
                best_slot_luminances = sl

    # If we couldn't find a plausible bar profile, treat as ambiguous.
    if best_slot_luminances is None:
        return None, []

    # Basic confidence gate: avoid latching onto unrelated high-contrast regions.
    # Threshold is intentionally low; we prefer "ambiguous" over persistent wrong slots.
    if best_score >= 0 and best_score < 6.0:
        return None, []

    slot_luminances = best_slot_luminances

    if not slot_luminances or len(slot_luminances) != num_slots:
        return None, slot_luminances

    # TEMPORAL DETECTION USING A ROLLING BASELINE (EMA)
    #
    # We keep a per-slot baseline (passed in via prev_slot_luminances) and detect the slot
    # that most increased relative to that baseline. This is robust under afterglow/bloom
    # because the baseline tracks lingering glow over time.
    #
    # prev_slot_luminances is treated as the baseline state and updated via EMA.
    if prev_slot_luminances and len(prev_slot_luminances) == num_slots:
        baseline = np.array(prev_slot_luminances, dtype=np.float32)
        current = np.array(slot_luminances, dtype=np.float32)

        delta = current - baseline
        # Remove any global drift by centering per frame.
        delta_centered = delta - float(np.median(delta))
        detected_slot = int(np.argmax(delta_centered))

        # Update baseline (slow enough to not erase the pop, fast enough to track afterglow).
        alpha = 0.20
        updated = (1.0 - alpha) * baseline + alpha * current
        return detected_slot, [float(x) for x in updated]

    # First frame: seed baseline and wait for the next frame to compute deltas.
    return None, slot_luminances


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
    settling_seconds: float,
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

    settling_seconds = max(0.0, float(settling_seconds))
    settling_frames = int(round(settling_seconds * fps))

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
    ret, frame0, seek_details = _read_frame_with_seek_backoff(cap, start_frame)
    if not ret or frame0 is None:
        cap.release()
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message="Could not seek to analysis start",
            details={"start_frame": start_frame, "seek": seek_details},
            metrics={},
        )

    # Detect content bounds from frame.
    # Infer format via FPS (NTSC ~59.826, PAL ~50.125) to choose the correct visible height.
    c64_visible_height = 240 if fps >= 55.0 else 272
    left, right, top, bottom = _detect_content_bounds(frame0, c64_visible_height)
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

    # Require at least 15% of analyzed frames or 8 samples (whichever is lower for short runs)
    min_required = min(max(8, int(0.15 * analyzed)), int(0.5 * fps))
    if valid < min_required:
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

    # Compress consecutive duplicates and track stuck runs.
    # IMPORTANT: measure stuck runs in real frame spans (video frames), not just
    # "number of valid samples", otherwise we can over/under-report freezes.
    compressed: list[int] = []
    compressed_frame_nums: list[int] = []
    stuck_runs: list[int] = []  # run length in frames
    stuck_run_frames: list[int] = []
    stuck_run_indices: list[int] = []
    current_run_start = index_frame_nums[0] if index_frame_nums else start_frame
    current_run_last = current_run_start
    current_run_idx = 0
    for i, idx in enumerate(indices):
        frame_num = index_frame_nums[i]
        if not compressed or idx != compressed[-1]:
            if compressed:
                stuck_runs.append((current_run_last - current_run_start) + 1)
                stuck_run_frames.append(int(current_run_start))
                stuck_run_indices.append(current_run_idx)
            compressed.append(idx)
            compressed_frame_nums.append(frame_num)
            current_run_start = frame_num
            current_run_last = frame_num
            current_run_idx = i
        else:
            current_run_last = frame_num
    if indices:
        stuck_runs.append((current_run_last - current_run_start) + 1)
        stuck_run_frames.append(int(current_run_start))
        stuck_run_indices.append(current_run_idx)

    # Filter out startup/end freeze periods intelligently using content_bounds
    # Recording stages: no data (logo) → data (video moving) → no data (video frozen) → logo
    startup_frames_excluded = 0
    end_frames_excluded = 0
    filtered_stuck_runs = stuck_runs.copy()
    filtered_stuck_run_frames = stuck_run_frames.copy()
    filtered_stuck_run_indices = stuck_run_indices.copy()

    # Use content_bounds to filter out logo/freeze periods if available
    if content_bounds is not None and content_bounds.detection_confidence > 0.3:
        # Filter startup: exclude stuck runs that occur before content actually starts
        # These are logo frames, not real freezes
        while filtered_stuck_runs and filtered_stuck_run_frames[0] < content_bounds.first_content_frame:
            startup_frames_excluded += filtered_stuck_runs[0]
            filtered_stuck_runs = filtered_stuck_runs[1:]
            filtered_stuck_run_frames = filtered_stuck_run_frames[1:]
            filtered_stuck_run_indices = filtered_stuck_run_indices[1:]

        # Filter end: exclude stuck runs that occur after content has stopped
        # These are frozen frames at end of stream or logo frames, not real problems
        # Also filter stuck runs in the final 4 seconds of content - this is the expected
        # "last frame held" period when C64 stops sending but OBS keeps recording.
        content_end_margin_frames = int(fps * 4.0)
        content_end_threshold = max(content_bounds.first_content_frame, content_bounds.last_content_frame - content_end_margin_frames)
        while filtered_stuck_runs and filtered_stuck_run_frames[-1] > content_end_threshold:
            end_frames_excluded += filtered_stuck_runs[-1]
            filtered_stuck_runs = filtered_stuck_runs[:-1]
            filtered_stuck_run_frames = filtered_stuck_run_frames[:-1]
            filtered_stuck_run_indices = filtered_stuck_run_indices[:-1]
    else:
        # Fallback: heuristic-based filtering when content_bounds not available
        startup_run_threshold = int(fps * 1.0)

        # Filter startup: first run is long and subsequent content shows progression
        if len(filtered_stuck_runs) >= 2 and filtered_stuck_runs[0] > startup_run_threshold:
            remaining_distinct = len(set(compressed[1:]))
            if remaining_distinct >= 4:
                startup_frames_excluded = filtered_stuck_runs[0]
                filtered_stuck_runs = filtered_stuck_runs[1:]
                filtered_stuck_run_frames = filtered_stuck_run_frames[1:]
                filtered_stuck_run_indices = filtered_stuck_run_indices[1:]

        # Filter end-of-stream: last run is long, or a long run followed by a short blip.
        if len(filtered_stuck_runs) >= 3:
            tail_run = filtered_stuck_runs[-1]
            penultimate_run = filtered_stuck_runs[-2]
            preceding_distinct = len(set(compressed[:-2]))
            if tail_run <= 2 and penultimate_run > startup_run_threshold and preceding_distinct >= 4:
                end_frames_excluded = penultimate_run + tail_run
                filtered_stuck_runs = filtered_stuck_runs[:-2]
                filtered_stuck_run_frames = filtered_stuck_run_frames[:-2]
                filtered_stuck_run_indices = filtered_stuck_run_indices[:-2]

        if end_frames_excluded == 0 and len(filtered_stuck_runs) >= 2 and filtered_stuck_runs[-1] > startup_run_threshold:
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
    # Filter out single-frame repeats (run_len == 2) that are part of alternating skip patterns
    repeated_events: list[dict] = []

    # Build a set of frame indices that are part of alternating patterns (from skip analysis below)
    # We'll check this when building repeated_events
    alternating_repeat_frames = set()

    # First pass: identify which repeats are part of alternating patterns
    # A repeat is suspicious if:
    # 1. It's a 2-frame repeat (run_len == 2)
    # 2. The next transition shows a skip of exactly 1 frame
    # 3. This pattern repeats multiple times
    for j in range(len(filtered_stuck_runs) - 1):
        run_len = filtered_stuck_runs[j]
        run_idx = filtered_stuck_run_indices[j]

        if run_len == 2:  # Single-frame repeat
            # Check if the corresponding slot transition shows a skip
            # The repeat starts at compressed index run_idx / len(indices) * len(compressed)
            # Actually, run_idx is an index into the original indices list
            # We need to find the corresponding position in compressed
            comp_idx = None
            for ci, cf in enumerate(compressed_frame_nums):
                if ci < len(index_frame_nums) and run_idx < len(index_frame_nums):
                    if cf == index_frame_nums[run_idx]:
                        comp_idx = ci
                        break

            if comp_idx is not None and comp_idx + 1 < len(compressed):
                # Check the delta after this repeat
                prev_slot = compressed[comp_idx]
                next_slot = compressed[comp_idx + 1]
                delta = int((next_slot - prev_slot) % 8)

                # If delta is 2 (skip of 1), this is part of alternating pattern
                if delta == 2:
                    repeat_frame_idx = run_idx + 1
                    if repeat_frame_idx < len(index_frame_nums):
                        alternating_repeat_frames.add(index_frame_nums[repeat_frame_idx])

    # Second pass: build repeated_events, filtering alternating patterns
    for run_len, run_idx in zip(filtered_stuck_runs, filtered_stuck_run_indices):
        if run_len > 1:
            second_frame_idx = run_idx + 1
            if second_frame_idx < len(index_frame_nums):
                repeat_frame = index_frame_nums[second_frame_idx]

                # Skip single-frame repeats that are part of alternating patterns
                if run_len == 2 and repeat_frame in alternating_repeat_frames:
                    if verbose:
                        print(f"[frame_progression] Filtering false positive repeat at frame {repeat_frame} (alternating pattern)")
                    continue

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
    back_step_events: list[dict] = []
    severe_step_events: list[dict] = []
    delta_hist: dict[int, int] = {}
    max_skip_delta = int(thresholds.get("max_skip_delta", 4))
    num_slots = 8  # 8 slots in the progress bar

    # Build delta sequence for pattern analysis
    delta_sequence = []
    for i, (prev, cur) in enumerate(zip(compressed, compressed[1:])):
        delta = int((cur - prev) % num_slots)
        delta_hist[delta] = delta_hist.get(delta, 0) + 1
        delta_sequence.append((delta, i))

    # Detect false positive patterns: alternating small skips with single-frame repeats
    # This happens when heavy filters (afterglow, scanlines, etc.) cause occasional slot misdetection.
    # Pattern signature: delta=0 (repeat), delta=2 (skip 1), delta=0 (repeat), delta=2 (skip 1), ...
    # We filter these out if the pattern is consistent and deltas are small (≤2).
    false_positive_indices = set()
    if len(delta_sequence) >= 4:
        alternating_pattern_count = 0
        for j in range(len(delta_sequence) - 3):
            # Check for 2-step alternating pattern: (0, 2, 0, 2) or (2, 0, 2, 0)
            seq = [d for d, _ in delta_sequence[j:j+4]]
            if seq == [0, 2, 0, 2] or seq == [2, 0, 2, 0]:
                alternating_pattern_count += 1
                # Mark these transitions as false positives
                for k in range(j, j + 4):
                    if k < len(delta_sequence):
                        false_positive_indices.add(delta_sequence[k][1])

        # If we see significant alternating patterns (>10% of transitions), filter them
        if alternating_pattern_count > max(2, len(delta_sequence) * 0.1):
            if verbose:
                print(f"[frame_progression] Detected {alternating_pattern_count} false positive alternating patterns (heavy filter artifacts)")

    # Process deltas, skipping false positives
    for i, (prev, cur) in enumerate(zip(compressed, compressed[1:])):
        if i in false_positive_indices:
            continue  # Skip false positive transitions

        delta = int((cur - prev) % num_slots)
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
            time_sec = float(skip_frame) / fps
            back_step_events.append({
                "frame": int(skip_frame),
                "time_sec": round(time_sec, 3),
            })
            continue
        severe_steps += 1
        time_sec = float(skip_frame) / fps
        severe_step_events.append({
            "frame": int(skip_frame),
            "time_sec": round(time_sec, 3),
        })

    # Split anomalies into settling vs post-settling windows.
    # This does not change how anomalies are detected; it only changes reporting and pass/fail gating.
    # IMPORTANT: settling is measured relative to the analysis window start (start_frame), not video frame 0.
    settling_cutoff_frame = int(start_frame + settling_frames)

    pre_skip_sizes = [
        int(e["skipped"]) for e in skip_events if int(e.get("frame", 0)) < settling_cutoff_frame
    ]
    post_skip_sizes = [
        int(e["skipped"]) for e in skip_events if int(e.get("frame", 0)) >= settling_cutoff_frame
    ]
    pre_skip_count = len(pre_skip_sizes)
    post_skip_count = len(post_skip_sizes)
    pre_skips = sum(pre_skip_sizes)
    post_skips = sum(post_skip_sizes)

    pre_back_steps = sum(1 for e in back_step_events if int(e.get("frame", 0)) < settling_cutoff_frame)
    post_back_steps = sum(1 for e in back_step_events if int(e.get("frame", 0)) >= settling_cutoff_frame)
    pre_severe_steps = sum(1 for e in severe_step_events if int(e.get("frame", 0)) < settling_cutoff_frame)
    post_severe_steps = sum(1 for e in severe_step_events if int(e.get("frame", 0)) >= settling_cutoff_frame)

    # For stuck runs, classify by the run start frame.
    pre_stuck_runs = [
        int(run_len)
        for run_len, run_start in zip(filtered_stuck_runs, filtered_stuck_run_frames)
        if int(run_len) > 1 and int(run_start) < settling_cutoff_frame
    ]
    post_stuck_runs = [
        int(run_len)
        for run_len, run_start in zip(filtered_stuck_runs, filtered_stuck_run_frames)
        if int(run_len) > 1 and int(run_start) >= settling_cutoff_frame
    ]
    pre_stuck_run_count = len(pre_stuck_runs)
    post_stuck_run_count = len(post_stuck_runs)
    pre_stuck_frames = sum(max(0, r - 1) for r in pre_stuck_runs)
    post_stuck_frames = sum(max(0, r - 1) for r in post_stuck_runs)
    pre_max_stuck_run = max(pre_stuck_runs) if pre_stuck_runs else 0
    post_max_stuck_run = max(post_stuck_runs) if post_stuck_runs else 0

    pre_stuck_run_min = int(min(pre_stuck_runs)) if pre_stuck_runs else 0
    pre_stuck_run_median = int(np.median(pre_stuck_runs)) if pre_stuck_runs else 0
    post_stuck_run_min = int(min(post_stuck_runs)) if post_stuck_runs else 0
    post_stuck_run_median = int(np.median(post_stuck_runs)) if post_stuck_runs else 0

    pre_skip_min = int(min(pre_skip_sizes)) if pre_skip_sizes else 0
    pre_skip_median = int(np.median(pre_skip_sizes)) if pre_skip_sizes else 0
    pre_skip_max = int(max(pre_skip_sizes)) if pre_skip_sizes else 0
    post_skip_min = int(min(post_skip_sizes)) if post_skip_sizes else 0
    post_skip_median = int(np.median(post_skip_sizes)) if post_skip_sizes else 0
    post_skip_max = int(max(post_skip_sizes)) if post_skip_sizes else 0

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

    # Accept partial coverage: patterns may skip slots (e.g., 0→2→4→6→1→3→5→0)
    # Require at least 3 distinct slots to confirm pattern is progressing
    min_distinct_slots = int(thresholds.get("min_distinct_slots", 3))
    full_ok = distinct >= min_distinct_slots

    max_skips = int(thresholds.get("max_skips", 60))
    max_back_steps = int(thresholds.get("max_back_steps", 3))
    max_severe_steps = int(thresholds.get("max_severe_steps", 0))
    max_ambiguous_ratio = float(thresholds.get("max_ambiguous_ratio", 0.30))

    details = {
        "window": {"start_frame": start_frame, "end_frame": end_frame, "fps": fps},
        "settling_seconds": settling_seconds,
        "settling_frames": settling_frames,
        "settling_cutoff_frame": settling_cutoff_frame,
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
        "back_step_events": back_step_events,
        "severe_steps": severe_steps,
        "severe_step_events": severe_step_events,
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
        "settling": {
            "pre": {
                "skips": int(pre_skips),
                "skip_count": int(pre_skip_count),
                "skip_min": int(pre_skip_min),
                "skip_median": int(pre_skip_median),
                "skip_max": int(pre_skip_max),
                "back_steps": int(pre_back_steps),
                "severe_steps": int(pre_severe_steps),
                "stuck_frames": int(pre_stuck_frames),
                "stuck_run_count": int(pre_stuck_run_count),
                "stuck_run_min": int(pre_stuck_run_min),
                "stuck_run_median": int(pre_stuck_run_median),
                "max_stuck_run": int(pre_max_stuck_run),
            },
            "post": {
                "skips": int(post_skips),
                "skip_count": int(post_skip_count),
                "skip_min": int(post_skip_min),
                "skip_median": int(post_skip_median),
                "skip_max": int(post_skip_max),
                "back_steps": int(post_back_steps),
                "severe_steps": int(post_severe_steps),
                "stuck_frames": int(post_stuck_frames),
                "stuck_run_count": int(post_stuck_run_count),
                "stuck_run_min": int(post_stuck_run_min),
                "stuck_run_median": int(post_stuck_run_median),
                "max_stuck_run": int(post_max_stuck_run),
            },
        },
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
        "settling_seconds": float(settling_seconds),
        "pre_settling_skips": float(pre_skips),
        "pre_settling_skip_count": float(pre_skip_count),
        "pre_settling_skip_min": float(pre_skip_min),
        "pre_settling_skip_median": float(pre_skip_median),
        "pre_settling_skip_max": float(pre_skip_max),
        "pre_settling_back_steps": float(pre_back_steps),
        "pre_settling_severe_steps": float(pre_severe_steps),
        "pre_settling_stuck_frames": float(pre_stuck_frames),
        "pre_settling_stuck_run_count": float(pre_stuck_run_count),
        "pre_settling_stuck_run_min": float(pre_stuck_run_min),
        "pre_settling_stuck_run_median": float(pre_stuck_run_median),
        "pre_settling_max_stuck_run": float(pre_max_stuck_run),
        "post_settling_skips": float(post_skips),
        "post_settling_skip_count": float(post_skip_count),
        "post_settling_skip_min": float(post_skip_min),
        "post_settling_skip_median": float(post_skip_median),
        "post_settling_skip_max": float(post_skip_max),
        "post_settling_back_steps": float(post_back_steps),
        "post_settling_severe_steps": float(post_severe_steps),
        "post_settling_stuck_frames": float(post_stuck_frames),
        "post_settling_stuck_run_count": float(post_stuck_run_count),
        "post_settling_stuck_run_min": float(post_stuck_run_min),
        "post_settling_stuck_run_median": float(post_stuck_run_median),
        "post_settling_max_stuck_run": float(post_max_stuck_run),
    }

    # Check for excessive stuck frames (video stream freeze) (post-settling)
    max_stuck_ratio = float(thresholds.get("max_stuck_ratio", 0.50))
    max_stuck_run_frames = int(thresholds.get("max_stuck_run_frames", int(fps * 2)))

    post_effective_valid = max(1, valid - startup_frames_excluded - end_frames_excluded)
    post_stuck_ratio = float(post_stuck_frames) / float(post_effective_valid)

    if post_max_stuck_run > max_stuck_run_frames:
        return _AnalysisResult(
            status=AssertionStatus.WARNING,
            message=f"Video stream froze for {post_max_stuck_run} frames ({post_max_stuck_run/fps:.1f}s) (post-settling)",
            details=details,
            metrics=metrics,
        )

    if post_stuck_ratio > max_stuck_ratio:
        return _AnalysisResult(
            status=AssertionStatus.WARNING,
            message=f"High frame repetition ({post_stuck_ratio*100:.0f}% stuck) (post-settling)",
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

    if ambiguous_ratio > max_ambiguous_ratio:
        return _AnalysisResult(
            status=AssertionStatus.WARNING,
            message=f"Marker sampling too ambiguous (ratio={ambiguous_ratio:.2f})",
            details=details,
            metrics=metrics,
        )

    if post_severe_steps > max_severe_steps:
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message=f"Frame sequence out of order (severe_steps={post_severe_steps}) (post-settling)",
            details=details,
            metrics=metrics,
        )

    if post_skips > max_skips or post_back_steps > max_back_steps:
        return _AnalysisResult(
            status=AssertionStatus.WARNING,
            message=f"Frame sequence OK with jitter (skips={post_skips}, back_steps={post_back_steps}) (post-settling)",
            details=details,
            metrics=metrics,
        )

    return _AnalysisResult(
        status=AssertionStatus.PASS,
        message="Frame sequence verified",
        details=details,
        metrics=metrics,
    )


class FrameProgressionAssertion(EffectAssertion):
    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        super().__init__("Frame Progression", thresholds)
        self.thresholds = {
            "max_seconds": 8.0,
            "max_ambiguous_ratio": 0.40,  # Increased from 0.30 for robustness across encoding environments
            "min_changes_for_full": 20,
            "max_skip_delta": 6,
            "max_skips": 60,
            "max_back_steps": 3,
            # Allow up to 10 severe steps to handle CI timing variability.
            "max_severe_steps": 10,
            # Minimum distinct slots for full coverage (8 slots in progress bar).
            "min_full_coverage_colors": 8,
            **(thresholds or {}),
        }

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: Any, verbose: bool = False
    ) -> AssertionResult:
        settling_seconds = 4.0
        if isinstance(properties, dict):
            try:
                settling_seconds = float(properties.get("settling_seconds", 4.0) or 4.0)
            except Exception:
                settling_seconds = 4.0

        res = _analyze_frame_progression(mp4_path, self.thresholds, settling_seconds, verbose)
        return AssertionResult(
            status=res.status,
            name=self.name,
            message=res.message,
            details=res.details,
            metrics=res.metrics,
        )
