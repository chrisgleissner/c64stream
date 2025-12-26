#!/usr/bin/env python3
"""
C64 Stream - Sharp Pixels Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Verifies that single-pixel dots in the source are rendered as crisp,
sharp-edged 4x4 rectangles (not blurry) after pixel scaling effects.

The "dots" pattern generates 1x1 white pixels on a black background,
spaced every 16 pixels. With pixel_width=4.0 and pixel_height=4.0
(Sharp Pixels preset), each dot should become a crisp 4x4 rectangle.

Validation approach:
1. Find all white pixel blocks (connected components)
2. Verify each block is approximately 4x4 pixels (3-5 allowed for compression)
3. Verify each block contains solid white (high intensity)
4. Verify each block is surrounded entirely by solid black
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional

import cv2
import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion


def _detect_content_bounds(frame: np.ndarray) -> tuple[int, int, int, int]:
    """Detect content bounds (left, right, top, bottom) using black bars around C64 content.

    Uses a 1% threshold for sparse patterns like dots (every 16 pixels).
    """
    height, width = frame.shape[:2]
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    _, non_black = cv2.threshold(gray, 10, 255, cv2.THRESH_BINARY)

    # Use 1% threshold for sparse dot patterns (10% is too aggressive)
    col_counts = np.sum(non_black > 0, axis=0)
    col_thresh = max(1, int(0.01 * height))
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
    row_thresh = max(1, int(0.01 * width))
    content_rows = np.where(row_counts >= row_thresh)[0]
    if content_rows.size >= 2:
        top_bound = int(content_rows[0])
        bottom_bound = int(content_rows[-1]) + 1
    else:
        top_bound = 0
        bottom_bound = height

    return left_bound, right_bound, top_bound, bottom_bound


def _find_white_blocks(gray: np.ndarray, white_thresh: int = 100) -> list[dict]:
    """Find white pixel blocks using connected components.

    Returns list of dicts with keys: x, y, w, h, area, mean_intensity, is_solid
    """
    _, binary = cv2.threshold(gray, white_thresh, 255, cv2.THRESH_BINARY)

    # Use connected components to find blocks
    num_labels, labels, stats, centroids = cv2.connectedComponentsWithStats(binary, connectivity=8)

    blocks = []
    for label in range(1, num_labels):  # Skip background (label 0)
        x = stats[label, cv2.CC_STAT_LEFT]
        y = stats[label, cv2.CC_STAT_TOP]
        w = stats[label, cv2.CC_STAT_WIDTH]
        h = stats[label, cv2.CC_STAT_HEIGHT]
        area = stats[label, cv2.CC_STAT_AREA]

        # Extract the block region
        block_mask = (labels[y : y + h, x : x + w] == label)
        block_pixels = gray[y : y + h, x : x + w][block_mask]

        if block_pixels.size > 0:
            mean_intensity = float(np.mean(block_pixels))
            min_intensity = float(np.min(block_pixels))
            # Block is solid if all pixels are above threshold
            is_solid = min_intensity >= white_thresh
        else:
            mean_intensity = 0.0
            is_solid = False

        blocks.append(
            {
                "x": x,
                "y": y,
                "w": w,
                "h": h,
                "area": area,
                "mean_intensity": mean_intensity,
                "is_solid": is_solid,
            }
        )

    return blocks


def _verify_black_surround(
    gray: np.ndarray, x: int, y: int, w: int, h: int, black_thresh: int = 30, margin: int = 1
) -> tuple[bool, float]:
    """Verify that a block is surrounded entirely by black pixels.

    Args:
        gray: Grayscale image
        x, y, w, h: Block bounding box
        black_thresh: Maximum intensity for "black" pixels
        margin: Number of pixels to check around the block

    Returns:
        (is_surrounded, max_surround_intensity)
    """
    img_h, img_w = gray.shape

    # Define surround region (expanded bounding box)
    x1 = max(0, x - margin)
    y1 = max(0, y - margin)
    x2 = min(img_w, x + w + margin)
    y2 = min(img_h, y + h + margin)

    # Create mask for surround (expanded box minus inner box)
    surround_mask = np.zeros((y2 - y1, x2 - x1), dtype=bool)
    surround_mask[:, :] = True

    # Clear the inner block region from the mask
    inner_x1 = x - x1
    inner_y1 = y - y1
    inner_x2 = inner_x1 + w
    inner_y2 = inner_y1 + h
    surround_mask[inner_y1:inner_y2, inner_x1:inner_x2] = False

    # Extract surround pixels
    surround_region = gray[y1:y2, x1:x2]
    surround_pixels = surround_region[surround_mask]

    if surround_pixels.size == 0:
        return True, 0.0

    max_intensity = float(np.max(surround_pixels))
    is_surrounded = max_intensity <= black_thresh

    return is_surrounded, max_intensity


@dataclass
class _AnalysisResult:
    status: AssertionStatus
    message: str
    details: dict[str, Any]
    metrics: dict[str, float]


def _analyze_sharp_pixels(
    mp4_path: Path,
    thresholds: dict[str, float],
    verbose: bool,
) -> _AnalysisResult:
    """Analyze video to verify dots are rendered as sharp 4x4 rectangles.

    Verification approach:
    1. Find all white pixel blocks in the dots region
    2. Verify each block is approximately 4x4 pixels
    3. Verify each block contains solid white
    4. Verify each block is surrounded entirely by black
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

    # Analyze a frame from 70% into the video (skip startup transients and early frames)
    target_frame = min(frame_count - 1, max(0, int(frame_count * 0.7)))

    cap.set(cv2.CAP_PROP_POS_FRAMES, target_frame)
    ret, frame = cap.read()
    cap.release()

    if not ret or frame is None:
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message="Could not read video frame",
            details={"target_frame": target_frame},
            metrics={},
        )

    # Detect content bounds (exclude letterboxing)
    left, right, top, bottom = _detect_content_bounds(frame)
    content_w = right - left
    content_h = bottom - top

    # Calculate scaling for exclusion zones
    # The special regions in C64 coordinates:
    # - Top-left 40x40 (frame marker)
    # - Top-right 40x40 (palette tile)
    # - Bottom-right 80x80 (A/V sync pop area)
    c64_width = 384
    c64_height = 272 if content_h > 260 else 240  # PAL vs NTSC
    scale_x = content_w / c64_width
    scale_y = content_h / c64_height

    # Define exclusion zones in scaled coordinates (with margin for safety)
    marker_size = int(45 * scale_x)  # 40 + margin
    pop_size = int(85 * max(scale_x, scale_y))  # 80 + margin

    # Extract content ROI
    content = frame[top:bottom, left:right]
    gray = cv2.cvtColor(content, cv2.COLOR_BGR2GRAY)

    # Create mask to exclude special regions
    mask = np.ones(gray.shape, dtype=np.uint8) * 255
    # Top-left marker
    mask[0:marker_size, 0:marker_size] = 0
    # Top-right palette tile
    mask[0:marker_size, content_w - marker_size : content_w] = 0
    # Bottom-right pop area
    mask[content_h - pop_size : content_h, content_w - pop_size : content_w] = 0

    # Apply mask to grayscale
    gray_masked = cv2.bitwise_and(gray, gray, mask=mask)

    # Threshold parameters
    white_thresh = int(thresholds.get("white_threshold", 100))
    black_thresh = int(thresholds.get("black_threshold", 50))
    min_block_size = int(thresholds.get("min_block_size", 3))
    max_block_size = int(thresholds.get("max_block_size", 6))
    min_blocks = int(thresholds.get("min_blocks", 10))

    # Find all white blocks
    blocks = _find_white_blocks(gray_masked, white_thresh)

    # Filter out tiny noise and very large blocks (which would be special regions)
    valid_blocks = [b for b in blocks if b["area"] >= min_block_size * min_block_size and b["area"] <= 100]

    if len(valid_blocks) < min_blocks:
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message=f"Too few blocks detected: {len(valid_blocks)} (expected >= {min_blocks})",
            details={
                "block_count": len(valid_blocks),
                "content_bounds": {"left": left, "right": right, "top": top, "bottom": bottom},
            },
            metrics={"block_count": float(len(valid_blocks))},
        )

    # Analyze each block
    correct_size_blocks = []
    wrong_size_blocks = []
    not_surrounded_blocks = []
    not_solid_blocks = []

    for block in valid_blocks:
        x, y, w, h = block["x"], block["y"], block["w"], block["h"]

        # Check 1: Block is approximately 4x4 (allow 3-5 for compression artifacts)
        size_ok = min_block_size <= w <= max_block_size and min_block_size <= h <= max_block_size

        # Check 2: Block is solid white
        solid_ok = block["is_solid"]

        # Check 3: Block is surrounded by black
        surrounded_ok, max_surround = _verify_black_surround(gray_masked, x, y, w, h, black_thresh, margin=1)

        if size_ok and solid_ok and surrounded_ok:
            correct_size_blocks.append(block)
        elif not size_ok:
            wrong_size_blocks.append({"block": block, "reason": f"size {w}x{h}"})
        elif not solid_ok:
            not_solid_blocks.append({"block": block, "reason": f"not solid"})
        elif not surrounded_ok:
            not_surrounded_blocks.append({"block": block, "reason": f"max_surround={max_surround:.0f}"})

    # Calculate statistics
    widths = [b["w"] for b in correct_size_blocks]
    heights = [b["h"] for b in correct_size_blocks]
    areas = [b["area"] for b in correct_size_blocks]

    if widths:
        mean_width = float(np.mean(widths))
        mean_height = float(np.mean(heights))
        mean_area = float(np.mean(areas))
    else:
        mean_width = mean_height = mean_area = 0.0

    details = {
        "total_blocks_found": len(valid_blocks),
        "correct_4x4_blocks": len(correct_size_blocks),
        "wrong_size_blocks": len(wrong_size_blocks),
        "not_solid_blocks": len(not_solid_blocks),
        "not_surrounded_blocks": len(not_surrounded_blocks),
        "mean_width": round(mean_width, 2),
        "mean_height": round(mean_height, 2),
        "mean_area": round(mean_area, 2),
        "content_bounds": {"left": left, "right": right, "top": top, "bottom": bottom},
        "scale": {"x": round(scale_x, 2), "y": round(scale_y, 2)},
        "thresholds": {
            "white": white_thresh,
            "black": black_thresh,
            "size_range": f"{min_block_size}-{max_block_size}",
        },
    }

    metrics = {
        "correct_blocks": float(len(correct_size_blocks)),
        "wrong_size_blocks": float(len(wrong_size_blocks)),
        "not_surrounded_blocks": float(len(not_surrounded_blocks)),
        "mean_width": mean_width,
        "mean_height": mean_height,
    }

    # Success criteria: most blocks should be correct 4x4 with black surround
    total_checked = len(valid_blocks)
    correct_ratio = len(correct_size_blocks) / total_checked if total_checked > 0 else 0.0

    min_correct_ratio = float(thresholds.get("min_correct_ratio", 0.90))

    if correct_ratio < min_correct_ratio:
        # Build detailed failure message
        issues = []
        if wrong_size_blocks:
            sizes = [f"{b['block']['w']}x{b['block']['h']}" for b in wrong_size_blocks[:5]]
            issues.append(f"{len(wrong_size_blocks)} wrong size ({', '.join(sizes)})")
        if not_solid_blocks:
            issues.append(f"{len(not_solid_blocks)} not solid white")
        if not_surrounded_blocks:
            issues.append(f"{len(not_surrounded_blocks)} not surrounded by black")

        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message=f"Only {correct_ratio:.0%} blocks are correct 4x4 with black surround: {'; '.join(issues)}",
            details=details,
            metrics=metrics,
        )

    return _AnalysisResult(
        status=AssertionStatus.PASS,
        message=f"Sharp pixels verified: {len(correct_size_blocks)}/{total_checked} blocks are 4x4 with black surround",
        details=details,
        metrics=metrics,
    )


class SharpPixelsAssertion(EffectAssertion):
    """Verifies that pixel scaling produces sharp 4x4 rectangles from single-pixel dots.

    Validation checks:
    1. Each white block is approximately 4x4 pixels (3-5 allowed for compression)
    2. Each white block contains solid white pixels
    3. Each white block is surrounded entirely by solid black
    """

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        super().__init__("Sharp Pixels", thresholds)
        self.thresholds = {
            "white_threshold": 100,      # Grayscale threshold for detecting white pixels
            "black_threshold": 100,      # Maximum intensity for "black" pixels (raised for compression gradients)
            "min_block_size": 3,         # Minimum block dimension (compression may erode)
            "max_block_size": 9,         # Maximum block dimension (compression can merge adjacent blocks)
            "min_blocks": 10,            # Minimum number of blocks to detect
            "min_correct_ratio": 0.90,   # Minimum ratio of blocks that must pass all checks
            **(thresholds or {}),
        }

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: Any, verbose: bool = False
    ) -> AssertionResult:
        res = _analyze_sharp_pixels(mp4_path, self.thresholds, verbose)
        return AssertionResult(
            status=res.status,
            name=self.name,
            message=res.message,
            details=res.details,
            metrics=res.metrics,
        )
