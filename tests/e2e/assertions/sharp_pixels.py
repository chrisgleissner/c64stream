#!/usr/bin/env python3
"""
C64 Stream - Sharp Pixels Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Verifies that single-pixel dots in the source are rendered as crisp,
sharp-edged rectangles (not blurry) after pixel scaling effects.

The "dots" pattern generates 1x1 white pixels on a black background,
spaced every 16 pixels. With pixel_width=3.0 and pixel_height=3.0
(Sharp Pixels preset), each dot should become a crisp NxN rectangle
(approximately 3x3 to 4x4 depending on rounding).
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


def _find_white_blobs(gray: np.ndarray, threshold: int = 200) -> list[dict]:
    """Find white blobs (dots) in a grayscale image.
    
    Returns list of dicts with keys: x, y, w, h, area, aspect_ratio, edge_sharpness
    """
    _, binary = cv2.threshold(gray, threshold, 255, cv2.THRESH_BINARY)
    
    contours, _ = cv2.findContours(binary, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    
    blobs = []
    for contour in contours:
        area = cv2.contourArea(contour)
        if area < 4:  # Skip tiny noise
            continue
            
        x, y, w, h = cv2.boundingRect(contour)
        
        # Calculate aspect ratio (should be close to 1.0 for squares)
        aspect_ratio = max(w, h) / max(min(w, h), 1)
        
        # Calculate edge sharpness by looking at the blob boundary
        # Sharp edges = high gradient at boundary, blurry = gradual gradient
        roi = gray[max(0, y-2):min(gray.shape[0], y+h+2), 
                   max(0, x-2):min(gray.shape[1], x+w+2)]
        if roi.size > 0:
            # Compute Sobel gradient magnitude
            sobelx = cv2.Sobel(roi, cv2.CV_64F, 1, 0, ksize=3)
            sobely = cv2.Sobel(roi, cv2.CV_64F, 0, 1, ksize=3)
            gradient_mag = np.sqrt(sobelx**2 + sobely**2)
            # Sharp edges have high max gradient relative to mean
            max_grad = np.max(gradient_mag)
            mean_grad = np.mean(gradient_mag)
            edge_sharpness = max_grad / max(mean_grad, 1.0)
        else:
            edge_sharpness = 0.0
            
        blobs.append({
            'x': x,
            'y': y,
            'w': w,
            'h': h,
            'area': area,
            'aspect_ratio': aspect_ratio,
            'edge_sharpness': edge_sharpness,
        })
    
    return blobs


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
    """Analyze video to verify dots are rendered as sharp rectangles."""
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
    
    # Extract the content region, excluding special areas:
    # - Top-left 40x40 (frame marker)
    # - Top-right 40x40 (palette tile)
    # - Bottom-right 80x80 (A/V sync pop area)
    # Scale these exclusion zones based on content scaling
    c64_width = 384
    c64_height = 272 if content_h > 260 else 240  # PAL vs NTSC
    scale_x = content_w / c64_width
    scale_y = content_h / c64_height
    
    # Define exclusion zones in scaled coordinates
    marker_size = int(40 * scale_x)
    pop_size = int(80 * max(scale_x, scale_y))
    
    # Extract content ROI
    content = frame[top:bottom, left:right]
    gray = cv2.cvtColor(content, cv2.COLOR_BGR2GRAY)
    
    # Mask out exclusion zones
    mask = np.ones(gray.shape, dtype=np.uint8) * 255
    # Top-left marker
    mask[0:marker_size, 0:marker_size] = 0
    # Top-right palette tile
    mask[0:marker_size, content_w-marker_size:content_w] = 0
    # Bottom-right pop area
    mask[content_h-pop_size:content_h, content_w-pop_size:content_w] = 0
    
    # Apply mask
    gray_masked = cv2.bitwise_and(gray, gray, mask=mask)
    
    # Find white blobs (scaled dots)
    blobs = _find_white_blobs(gray_masked, threshold=int(thresholds.get("white_threshold", 200)))
    
    if len(blobs) < int(thresholds.get("min_dots", 10)):
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message=f"Too few dots detected: {len(blobs)} (expected >= {int(thresholds.get('min_dots', 10))})",
            details={
                "dot_count": len(blobs),
                "content_bounds": {"left": left, "right": right, "top": top, "bottom": bottom},
            },
            metrics={"dot_count": float(len(blobs))},
        )
    
    # Analyze blob properties
    areas = [b['area'] for b in blobs]
    aspect_ratios = [b['aspect_ratio'] for b in blobs]
    edge_sharpnesses = [b['edge_sharpness'] for b in blobs]
    widths = [b['w'] for b in blobs]
    heights = [b['h'] for b in blobs]
    
    # Calculate statistics
    mean_area = float(np.mean(areas))
    std_area = float(np.std(areas))
    mean_aspect = float(np.mean(aspect_ratios))
    mean_sharpness = float(np.mean(edge_sharpnesses))
    mean_width = float(np.mean(widths))
    mean_height = float(np.mean(heights))
    
    # Expected dot size after 3x scaling: ~3x3 to 4x4 = area 9-16
    # With some tolerance for subpixel effects
    min_expected_area = float(thresholds.get("min_dot_area", 6))
    max_expected_area = float(thresholds.get("max_dot_area", 25))
    max_aspect_ratio = float(thresholds.get("max_aspect_ratio", 1.5))
    min_edge_sharpness = float(thresholds.get("min_edge_sharpness", 3.0))
    max_area_stddev = float(thresholds.get("max_area_stddev", 5.0))
    
    details = {
        "dot_count": len(blobs),
        "mean_area": round(mean_area, 2),
        "std_area": round(std_area, 2),
        "mean_aspect_ratio": round(mean_aspect, 3),
        "mean_edge_sharpness": round(mean_sharpness, 2),
        "mean_width": round(mean_width, 2),
        "mean_height": round(mean_height, 2),
        "content_bounds": {"left": left, "right": right, "top": top, "bottom": bottom},
        "scale": {"x": round(scale_x, 2), "y": round(scale_y, 2)},
    }
    
    metrics = {
        "dot_count": float(len(blobs)),
        "mean_area": mean_area,
        "std_area": std_area,
        "mean_aspect_ratio": mean_aspect,
        "mean_edge_sharpness": mean_sharpness,
    }
    
    # Check 1: Dot area is in expected range
    if mean_area < min_expected_area:
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message=f"Dots too small: mean area {mean_area:.1f} < {min_expected_area} (pixels may not be scaled)",
            details=details,
            metrics=metrics,
        )
    
    if mean_area > max_expected_area:
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message=f"Dots too large: mean area {mean_area:.1f} > {max_expected_area} (may be blurry/bloomed)",
            details=details,
            metrics=metrics,
        )
    
    # Check 2: Dots are roughly square (aspect ratio close to 1.0)
    if mean_aspect > max_aspect_ratio:
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message=f"Dots not square: mean aspect ratio {mean_aspect:.2f} > {max_aspect_ratio}",
            details=details,
            metrics=metrics,
        )
    
    # Check 3: Edges are sharp (high gradient at boundaries)
    if mean_sharpness < min_edge_sharpness:
        return _AnalysisResult(
            status=AssertionStatus.FAIL,
            message=f"Dots are blurry: edge sharpness {mean_sharpness:.1f} < {min_edge_sharpness}",
            details=details,
            metrics=metrics,
        )
    
    # Check 4: Consistent dot size (low standard deviation)
    if std_area > max_area_stddev:
        return _AnalysisResult(
            status=AssertionStatus.WARNING,
            message=f"Inconsistent dot sizes: std dev {std_area:.1f} > {max_area_stddev}",
            details=details,
            metrics=metrics,
        )
    
    return _AnalysisResult(
        status=AssertionStatus.PASS,
        message=f"Sharp pixels verified: {len(blobs)} dots, mean area {mean_area:.1f}, sharpness {mean_sharpness:.1f}",
        details=details,
        metrics=metrics,
    )


class SharpPixelsAssertion(EffectAssertion):
    """Verifies that pixel scaling produces sharp, crisp rectangles from single-pixel dots."""
    
    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        super().__init__("Sharp Pixels", thresholds)
        self.thresholds = {
            "white_threshold": 100,      # Grayscale threshold for detecting white dots
            "min_dots": 10,              # Minimum number of dots to detect
            "min_dot_area": 4,           # Minimum area for a valid scaled dot
            "max_dot_area": 30,          # Maximum area (larger = blurry/bloomed)
            "max_aspect_ratio": 2.0,     # Maximum width/height ratio (1.0 = perfect square)
            "min_edge_sharpness": 2.0,   # Minimum edge gradient ratio (higher = sharper)
            "max_area_stddev": 8.0,      # Maximum standard deviation in dot areas
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
