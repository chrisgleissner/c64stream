#!/usr/bin/env python3
"""
C64 Stream - Content Boundary Detection
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Detects the precise frame boundaries where:
1. The c64stream logo is replaced by actual C64U video content
2. The C64U video content stops changing (final frame)

This is resilient to visual effects (CRT, scanlines, bloom, etc.) because it
detects *changes* in frame content rather than absolute pixel values.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import cv2
import numpy as np


@dataclass
class ContentBounds:
    """Result of content boundary detection."""

    first_content_frame: int  # First frame with actual C64U content (logo -> content transition)
    last_content_frame: int  # Last frame before content freezes (content -> frozen transition)
    fps: float
    total_frames: int
    detection_confidence: float  # 0.0 to 1.0, how confident we are in the detection

    # Additional diagnostics
    logo_end_frame: int  # Last frame that's clearly the logo
    content_start_diff: float  # Frame difference magnitude at content start
    content_end_diff: float  # Frame difference magnitude at content end (before freeze)
    avg_content_diff: float  # Average frame difference during content


def _frame_difference(frame1: np.ndarray, frame2: np.ndarray) -> float:
    """
    Calculate the perceptual difference between two frames.
    Returns a normalized value where 0 = identical, higher = more different.

    Uses grayscale conversion and blur to be resilient to color shifts from filters.
    """
    if frame1 is None or frame2 is None:
        return 0.0
    if frame1.shape != frame2.shape:
        return 0.0

    # Convert to grayscale
    gray1 = cv2.cvtColor(frame1, cv2.COLOR_BGR2GRAY) if len(frame1.shape) == 3 else frame1
    gray2 = cv2.cvtColor(frame2, cv2.COLOR_BGR2GRAY) if len(frame2.shape) == 3 else frame2

    # Apply slight blur to reduce noise from compression artifacts and scanlines
    gray1 = cv2.GaussianBlur(gray1, (5, 5), 0)
    gray2 = cv2.GaussianBlur(gray2, (5, 5), 0)

    # Calculate absolute difference
    diff = cv2.absdiff(gray1, gray2)

    # Return mean absolute difference normalized to [0, 255]
    return float(np.mean(diff))


def _detect_content_region(frame: np.ndarray) -> tuple[int, int, int, int]:
    """
    Detect the C64 content region within the frame.
    Returns (left, right, top, bottom) bounds.
    """
    height, width = frame.shape[:2]
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

    # Find non-black pixels (content area vs black bars)
    _, non_black = cv2.threshold(gray, 10, 255, cv2.THRESH_BINARY)

    col_counts = np.sum(non_black > 0, axis=0)
    col_thresh = max(1, int(0.10 * height))
    content_cols = np.where(col_counts >= col_thresh)[0]

    if content_cols.size >= 2:
        left = int(content_cols[0])
        right = int(content_cols[-1]) + 1
    else:
        # Fallback: assume centered C64 content
        scale_factor = height / 272.0
        scaled_width = int(384 * scale_factor)
        left = (width - scaled_width) // 2
        right = (width + scaled_width) // 2

    row_counts = np.sum(non_black > 0, axis=1)
    row_thresh = max(1, int(0.10 * width))
    content_rows = np.where(row_counts >= row_thresh)[0]

    if content_rows.size >= 2:
        top = int(content_rows[0])
        bottom = int(content_rows[-1]) + 1
    else:
        top = 0
        bottom = height

    return left, right, top, bottom


def _crop_to_content(frame: np.ndarray, bounds: tuple[int, int, int, int]) -> np.ndarray:
    """Crop frame to content region."""
    left, right, top, bottom = bounds
    height, width = frame.shape[:2]
    left = max(0, min(width - 1, left))
    right = max(left + 1, min(width, right))
    top = max(0, min(height - 1, top))
    bottom = max(top + 1, min(height, bottom))
    return frame[top:bottom, left:right]


def detect_content_bounds(
    video_path: Path | str,
    sample_stride: int = 1,
    min_content_diff: float = 0.5,
    freeze_threshold: float = 0.3,
    min_content_frames: int = 30,
    verbose: bool = False,
) -> Optional[ContentBounds]:
    """
    Detect content boundaries in a video recording.

    The algorithm:
    1. Scan through frames, calculating frame-to-frame differences
    2. Detect the logo -> content transition: sharp increase in frame differences
    3. Detect the content -> freeze transition: frame differences drop to near-zero

    Args:
        video_path: Path to the video file
        sample_stride: Process every Nth frame (1 = all frames)
        min_content_diff: Minimum frame difference to consider as "content changing"
        freeze_threshold: Maximum frame difference to consider as "frozen"
        min_content_frames: Minimum number of content frames required
        verbose: Print debug info

    Returns:
        ContentBounds if successful, None if detection failed
    """
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        return None

    fps = float(cap.get(cv2.CAP_PROP_FPS) or 30.0)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)

    if total_frames < min_content_frames:
        cap.release()
        return None

    # Get content bounds from a frame in the middle of the video
    cap.set(cv2.CAP_PROP_POS_FRAMES, total_frames // 2)
    ret, mid_frame = cap.read()
    if not ret:
        cap.release()
        return None
    content_bounds = _detect_content_region(mid_frame)
    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)

    # Collect frame differences
    frame_diffs: list[tuple[int, float]] = []  # (frame_index, difference)
    prev_frame = None
    frame_idx = 0

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        if frame_idx % sample_stride == 0:
            cropped = _crop_to_content(frame, content_bounds)

            if prev_frame is not None:
                diff = _frame_difference(prev_frame, cropped)
                frame_diffs.append((frame_idx, diff))

            prev_frame = cropped.copy()

        frame_idx += 1

    cap.release()

    if len(frame_diffs) < min_content_frames:
        return None

    # Convert to numpy for analysis
    frames = np.array([fd[0] for fd in frame_diffs])
    diffs = np.array([fd[1] for fd in frame_diffs])

    if verbose:
        print(f"[content_bounds] Analyzed {len(diffs)} frame pairs")
        print(f"[content_bounds] Diff range: {diffs.min():.2f} - {diffs.max():.2f}")
        print(f"[content_bounds] Diff mean: {diffs.mean():.2f}, median: {np.median(diffs):.2f}")

    # Phase 1: Find logo -> content transition
    # The logo is static, so diffs should be near-zero until content starts.
    # Look for the first sustained increase in frame differences.

    # Use a sliding window to smooth noise
    window_size = max(3, int(fps / 10))  # ~0.1 seconds
    smoothed = np.convolve(diffs, np.ones(window_size) / window_size, mode="same")

    # Find baseline (logo period) - use the minimum smoothed diff region
    baseline_percentile = np.percentile(smoothed, 10)

    # Content threshold: above baseline + margin, or absolute minimum
    content_threshold = max(min_content_diff, baseline_percentile * 3)

    if verbose:
        print(f"[content_bounds] Baseline: {baseline_percentile:.2f}, content threshold: {content_threshold:.2f}")

    # Find first frame where smoothed diff exceeds threshold
    first_content_idx = None
    for i, (frame_num, diff) in enumerate(zip(frames, smoothed)):
        if diff > content_threshold:
            # Verify it's sustained (next few frames also above threshold)
            sustained = True
            for j in range(i, min(i + 5, len(smoothed))):
                if smoothed[j] < content_threshold * 0.5:
                    sustained = False
                    break
            if sustained:
                first_content_idx = i
                break

    if first_content_idx is None:
        if verbose:
            print("[content_bounds] Could not find content start")
        return None

    first_content_frame = int(frames[first_content_idx])

    # Phase 2: Find content -> freeze transition
    # Look for where diffs drop to near-zero and stay there.

    # Search from the end backwards for sustained low diff
    last_content_idx = len(diffs) - 1
    freeze_window = max(5, int(fps / 4))  # ~0.25 seconds

    for i in range(len(diffs) - 1, first_content_idx, -1):
        # Check if this region is frozen (low diffs)
        window_start = max(first_content_idx, i - freeze_window)
        window_diffs = diffs[window_start : i + 1]

        if len(window_diffs) > 0 and np.mean(window_diffs) > freeze_threshold:
            # Found non-frozen content
            last_content_idx = i
            break

    last_content_frame = int(frames[last_content_idx])

    # Calculate confidence based on how clear the transitions are
    content_diffs = diffs[first_content_idx : last_content_idx + 1]
    if len(content_diffs) > 0:
        avg_content_diff = float(np.mean(content_diffs))
        content_start_diff = float(diffs[first_content_idx])
        content_end_diff = float(diffs[last_content_idx]) if last_content_idx < len(diffs) else 0.0

        # Confidence based on: clear separation between logo and content
        separation_ratio = avg_content_diff / max(baseline_percentile, 0.1)
        confidence = min(1.0, separation_ratio / 10.0)  # Full confidence at 10x separation
    else:
        avg_content_diff = 0.0
        content_start_diff = 0.0
        content_end_diff = 0.0
        confidence = 0.0

    if verbose:
        print(f"[content_bounds] First content frame: {first_content_frame}")
        print(f"[content_bounds] Last content frame: {last_content_frame}")
        print(f"[content_bounds] Avg content diff: {avg_content_diff:.2f}")
        print(f"[content_bounds] Confidence: {confidence:.2f}")

    # Logo end is the frame before content starts
    logo_end_frame = max(0, first_content_frame - sample_stride)

    return ContentBounds(
        first_content_frame=first_content_frame,
        last_content_frame=last_content_frame,
        fps=fps,
        total_frames=total_frames,
        detection_confidence=confidence,
        logo_end_frame=logo_end_frame,
        content_start_diff=content_start_diff,
        content_end_diff=content_end_diff,
        avg_content_diff=avg_content_diff,
    )


def detect_content_bounds_precise(
    video_path: Path | str,
    coarse_bounds: Optional[ContentBounds] = None,
    verbose: bool = False,
) -> Optional[ContentBounds]:
    """
    Perform precise frame-by-frame detection around the content boundaries.

    If coarse_bounds is provided, only scans a small window around those boundaries.
    Otherwise, does a full coarse scan first.

    Args:
        video_path: Path to the video file
        coarse_bounds: Optional coarse boundaries from detect_content_bounds()
        verbose: Print debug info

    Returns:
        ContentBounds with precise frame numbers
    """
    # First get coarse bounds if not provided
    if coarse_bounds is None:
        coarse_bounds = detect_content_bounds(video_path, sample_stride=3, verbose=verbose)
        if coarse_bounds is None:
            return None

    # Now do precise scan around the boundaries
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        return None

    fps = coarse_bounds.fps
    total_frames = coarse_bounds.total_frames

    # Get content bounds from a frame in the middle
    cap.set(cv2.CAP_PROP_POS_FRAMES, total_frames // 2)
    ret, mid_frame = cap.read()
    if not ret:
        cap.release()
        return None
    content_region = _detect_content_region(mid_frame)

    # Scan window around first content frame
    search_window = max(10, int(fps))  # 1 second window
    start_search = max(0, coarse_bounds.first_content_frame - search_window)
    end_search = min(total_frames - 1, coarse_bounds.first_content_frame + search_window)

    cap.set(cv2.CAP_PROP_POS_FRAMES, start_search)
    prev_frame = None
    frame_diffs: list[tuple[int, float]] = []

    for frame_idx in range(start_search, end_search + 1):
        ret, frame = cap.read()
        if not ret:
            break

        cropped = _crop_to_content(frame, content_region)
        if prev_frame is not None:
            diff = _frame_difference(prev_frame, cropped)
            frame_diffs.append((frame_idx, diff))
        prev_frame = cropped.copy()

    if not frame_diffs:
        cap.release()
        return coarse_bounds  # Fall back to coarse

    # Find precise first content frame: first significant diff increase
    diffs = np.array([fd[1] for fd in frame_diffs])
    frames = np.array([fd[0] for fd in frame_diffs])

    baseline = np.percentile(diffs[:max(1, len(diffs) // 4)], 50)
    threshold = max(0.5, baseline * 3)

    first_content_frame = coarse_bounds.first_content_frame
    for i, (frame_num, diff) in enumerate(zip(frames, diffs)):
        if diff > threshold:
            first_content_frame = int(frame_num)
            break

    # Now scan around last content frame
    start_search = max(first_content_frame, coarse_bounds.last_content_frame - search_window)
    end_search = min(total_frames - 1, coarse_bounds.last_content_frame + search_window)

    cap.set(cv2.CAP_PROP_POS_FRAMES, start_search)
    prev_frame = None
    frame_diffs = []

    for frame_idx in range(start_search, end_search + 1):
        ret, frame = cap.read()
        if not ret:
            break

        cropped = _crop_to_content(frame, content_region)
        if prev_frame is not None:
            diff = _frame_difference(prev_frame, cropped)
            frame_diffs.append((frame_idx, diff))
        prev_frame = cropped.copy()

    cap.release()

    if not frame_diffs:
        return ContentBounds(
            first_content_frame=first_content_frame,
            last_content_frame=coarse_bounds.last_content_frame,
            fps=fps,
            total_frames=total_frames,
            detection_confidence=coarse_bounds.detection_confidence,
            logo_end_frame=max(0, first_content_frame - 1),
            content_start_diff=coarse_bounds.content_start_diff,
            content_end_diff=coarse_bounds.content_end_diff,
            avg_content_diff=coarse_bounds.avg_content_diff,
        )

    # Find precise last content frame: last frame with significant diff
    diffs = np.array([fd[1] for fd in frame_diffs])
    frames = np.array([fd[0] for fd in frame_diffs])

    last_content_frame = coarse_bounds.last_content_frame
    freeze_threshold = 0.3

    # Search from end backwards
    for i in range(len(diffs) - 1, -1, -1):
        if diffs[i] > freeze_threshold:
            last_content_frame = int(frames[i])
            break

    return ContentBounds(
        first_content_frame=first_content_frame,
        last_content_frame=last_content_frame,
        fps=fps,
        total_frames=total_frames,
        detection_confidence=coarse_bounds.detection_confidence,
        logo_end_frame=max(0, first_content_frame - 1),
        content_start_diff=float(diffs[0]) if len(diffs) > 0 else 0.0,
        content_end_diff=float(diffs[-1]) if len(diffs) > 0 else 0.0,
        avg_content_diff=float(np.mean(diffs)) if len(diffs) > 0 else 0.0,
    )


# Convenience function for external use
def get_content_frame_range(
    video_path: Path | str,
    precise: bool = True,
    verbose: bool = False,
) -> Optional[tuple[int, int, float]]:
    """
    Get the frame range containing actual C64U content.

    Returns:
        Tuple of (first_content_frame, last_content_frame, fps) or None if detection failed.
    """
    if precise:
        bounds = detect_content_bounds_precise(video_path, verbose=verbose)
    else:
        bounds = detect_content_bounds(video_path, verbose=verbose)

    if bounds is None:
        return None

    return bounds.first_content_frame, bounds.last_content_frame, bounds.fps
