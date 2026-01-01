#!/usr/bin/env python3
"""
C64 Stream - Palette Mapping Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Verifies that the rendered palette matches the expected VPL palette by analyzing
the 16-color watch region in the top-right corner of the C64 video output.

This assertion is robust to:
- Rescaling and different canvas resolutions
- Scan lines
- Blur/bloom effects
- Codec artifacts (NV12/H.264)

It will SKIP when tint is enabled (tint intentionally destroys per-color identity).
"""

from __future__ import annotations

import os
import re
from pathlib import Path
from typing import Any, Optional

import cv2
import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig
from .content_bounds import detect_content_bounds

# C64 coordinate constants from generate_packets.py
# Top-right corner widget: 88x56 outer, 72x40 inner
C64_CORNER_OUTER_WIDTH = 88
C64_CORNER_OUTER_HEIGHT = 56
C64_CORNER_FRAME_OUTER = 8  # Outer frame width
C64_CORNER_FRAME_INNER = 0  # Inner frame width (no inner frame)
C64_CORNER_FRAME_TOTAL = C64_CORNER_FRAME_OUTER + C64_CORNER_FRAME_INNER  # 8px total frame
C64_CORNER_INNER_WIDTH = 72
C64_CORNER_INNER_HEIGHT = 40

# Grid layout: 4x4 colors with 2px padding, each cell is 17x9, swatch is 15x7
C64_PALETTE_PADDING = 2
C64_SWATCH_WIDTH = 17  # Cell width including gap
C64_SWATCH_HEIGHT = 9  # Cell height including gap
C64_SWATCH_COLOR_WIDTH = 15  # Solid color portion
C64_SWATCH_COLOR_HEIGHT = 7  # Solid color portion

# C64 video dimensions
C64_VIDEO_WIDTH = 384
C64_VIDEO_HEIGHT_NTSC = 240
C64_VIDEO_HEIGHT_PAL = 272


def load_vpl_palette(vpl_path: Path) -> Optional[list[tuple[int, int, int]]]:
    """Load RGB colors from a VPL palette file.

    Supports both formats:
    - Space-separated: "FF FF FF  # comment"
    - Concatenated: "FFFFFF # comment"

    Returns a list of 16 RGB tuples, or None if loading failed.
    """
    if not vpl_path.exists():
        return None

    colors = []
    try:
        with open(vpl_path, "r") as f:
            for line in f:
                line = line.strip()
                # Skip empty lines and comment-only lines
                if not line or line.startswith("#"):
                    continue

                # Remove inline comments
                if "#" in line:
                    line = line[: line.index("#")].strip()

                # Try space-separated format first (e.g., "FF FF FF")
                parts = line.split()
                if len(parts) >= 3:
                    try:
                        r = int(parts[0], 16)
                        g = int(parts[1], 16)
                        b = int(parts[2], 16)
                        colors.append((r, g, b))
                        if len(colors) >= 16:
                            break
                        continue
                    except ValueError:
                        pass

                # Fallback to concatenated format (e.g., "FFFFFF")
                if len(line) >= 6:
                    hex_part = line[:6]
                    try:
                        r = int(hex_part[0:2], 16)
                        g = int(hex_part[2:4], 16)
                        b = int(hex_part[4:6], 16)
                        colors.append((r, g, b))
                    except ValueError:
                        continue
                if len(colors) >= 16:
                    break
        return colors if len(colors) == 16 else None
    except Exception:
        return None


def find_palette_vpl(palette_name: str, data_dir: Path) -> Optional[Path]:
    """Find the VPL file for a given palette name.

    Searches in the plugin data/palettes directory.
    """
    palettes_dir = data_dir / "palettes"
    if not palettes_dir.exists():
        return None

    # Try exact name match first
    vpl_path = palettes_dir / f"{palette_name}.vpl"
    if vpl_path.exists():
        return vpl_path

    # Try case-insensitive match
    for f in palettes_dir.iterdir():
        if f.suffix.lower() == ".vpl" and f.stem.lower() == palette_name.lower():
            return f

    return None


class PaletteMappingAssertion(EffectAssertion):
    """Verify palette colors match expected VPL values in the 16-color watch region."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            # Per-channel max delta tolerance (0-255 scale)
            # This needs to be loose enough for codec artifacts but tight enough to catch errors
            "max_channel_delta": 35.0,
            # Minimum frames to analyze for multi-frame aggregation
            "min_frames": 5,
            # ROI shrink factor (sample inner portion of each swatch)
            "roi_shrink": 0.4,
            # Skip first/last frames to avoid transition artifacts
            "skip_frames": 2,
        }
        super().__init__("PaletteMapping", {**defaults, **(thresholds or {})})

    def verify(
        self,
        mp4_path: Path,
        properties: dict[str, Any],
        preset: PresetConfig,
        verbose: bool = False,
    ) -> AssertionResult:
        """Verify palette mapping against expected VPL colors."""

        # Check for tint - skip if tint is enabled
        if preset.has_tint():
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Skipped: tint is enabled (destroys per-color identity)",
            )

        if not mp4_path.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Recording file not found: {mp4_path}",
            )

        # Get expected palette name from preset/settings
        palette_name = self._get_palette_name(properties, preset)
        self.log(f"Expected palette: {palette_name}", verbose)

        # Find and load the VPL palette
        data_dir = self._find_data_dir(mp4_path)
        vpl_path = find_palette_vpl(palette_name, data_dir)

        if vpl_path is None:
            # Try to find Default.vpl as fallback for default palette
            if palette_name.lower() == "default":
                # Use hardcoded default palette colors
                expected_colors = self._get_default_palette()
            else:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Could not find VPL file for palette: {palette_name}",
                )
        else:
            expected_colors = load_vpl_palette(vpl_path)
            self.log(f"Loaded palette from: {vpl_path}", verbose)

        if expected_colors is None:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Failed to load palette colors from: {vpl_path}",
            )

        # Extract observed colors from the recording
        try:
            observed_colors = self._extract_palette_colors(mp4_path, properties, verbose)
        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Failed to extract palette colors: {e}",
            )

        if observed_colors is None:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="Could not extract palette colors from recording",
            )

        # Compare colors
        max_delta, failing_indices, comparison_details = self._compare_palettes(
            expected_colors, observed_colors, verbose
        )

        max_allowed = self.thresholds["max_channel_delta"]

        if failing_indices:
            # Build failure message with worst offenders
            worst = comparison_details[:3]  # Top 3 worst
            worst_msg = "; ".join(
                f"idx {d['index']}: exp {d['expected']} vs obs {d['observed']} (delta {d['delta']})"
                for d in worst
            )
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Palette mismatch: max delta {max_delta:.1f} > {max_allowed:.1f}, {len(failing_indices)} failing indices. Worst: {worst_msg}",
                details={
                    "failing_indices": failing_indices,
                    "comparison": comparison_details,
                    "palette_name": palette_name,
                },
                metrics={"max_delta": max_delta, "failing_count": len(failing_indices)},
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=f"Palette mapping verified: max delta {max_delta:.1f} <= {max_allowed:.1f}",
            details={"palette_name": palette_name, "comparison": comparison_details},
            metrics={"max_delta": max_delta},
        )

    def _get_palette_name(self, properties: dict[str, Any], preset: PresetConfig) -> str:
        """Get the expected palette name from properties or preset."""
        # Check properties for palette setting
        if "palette" in properties:
            return properties["palette"]
        # Check if preset has palette override
        if hasattr(preset, "palette") and preset.palette:
            return preset.palette
        # Default
        return "Default"

    def _find_data_dir(self, mp4_path: Path) -> Path:
        """Find the plugin data directory relative to the test location."""
        # First, try to find relative to the assertions module (most reliable)
        assertions_dir = Path(__file__).parent  # tests/e2e/assertions/
        project_root = assertions_dir.parent.parent.parent  # c64stream/
        data_dir = project_root / "data"
        if data_dir.exists() and (data_dir / "palettes").exists():
            return data_dir

        # Navigate up from the mp4 path to find the project root
        current = mp4_path.parent
        for _ in range(10):  # Limit depth
            data_dir = current / "data"
            if data_dir.exists() and (data_dir / "palettes").exists():
                return data_dir
            # Check for top-level data dir by looking for src
            if (current / "src").exists() and (current / "data").exists():
                return current / "data"
            current = current.parent

        # Fallback: try relative to cwd
        cwd_data = Path.cwd() / "data"
        if cwd_data.exists():
            return cwd_data

        # Last resort: check for installed plugin data
        for plugin_path in [
            Path.home() / ".config" / "obs-studio" / "plugins" / "c64stream" / "data",
            Path("/usr/share/obs/obs-plugins/c64stream"),
            Path("/usr/local/share/obs/obs-plugins/c64stream"),
        ]:
            if plugin_path.exists() and (plugin_path / "palettes").exists():
                return plugin_path

        return Path.cwd() / "data"  # Fallback

    def _get_default_palette(self) -> list[tuple[int, int, int]]:
        """Return the hardcoded default C64 palette (matches c64-color.c)."""
        # These are stored as 0xFFBBGGRR in c64-color.c, convert to RGB
        bgra_colors = [
            0xFF000000,  # 0: Black
            0xFFF7F7F7,  # 1: White
            0xFF342F8D,  # 2: Red (BGRA: B=0x34, G=0x2F, R=0x8D)
            0xFFCDD46A,  # 3: Cyan
            0xFFA43598,  # 4: Purple
            0xFF42B44C,  # 5: Green
            0xFFB1292C,  # 6: Blue
            0xFF5DEFEF,  # 7: Yellow
            0xFF204E98,  # 8: Orange
            0xFF00385B,  # 9: Brown
            0xFF6D67D1,  # 10: Pink
            0xFF4A4A4A,  # 11: Dark Grey
            0xFF7B7B7B,  # 12: Medium Grey
            0xFF93EF9F,  # 13: Light Green
            0xFFEF6A6D,  # 14: Light Blue
            0xFFB2B2B2,  # 15: Light Grey
        ]
        # Convert BGRA to RGB
        rgb_colors = []
        for bgra in bgra_colors:
            b = (bgra >> 0) & 0xFF
            g = (bgra >> 8) & 0xFF
            r = (bgra >> 16) & 0xFF
            rgb_colors.append((r, g, b))
        return rgb_colors

    def _extract_palette_colors(
        self, mp4_path: Path, properties: dict[str, Any], verbose: bool
    ) -> Optional[list[tuple[int, int, int]]]:
        """Extract the 16 palette colors from the recording's top-right watch region."""

        cap = cv2.VideoCapture(str(mp4_path))
        if not cap.isOpened():
            return None

        try:
            total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            fps = cap.get(cv2.CAP_PROP_FPS)
            width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
            height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

            self.log(f"Video: {width}x{height}, {total_frames} frames @ {fps:.2f} fps", verbose)

            # Detect content bounds to find the C64 video region
            bounds = detect_content_bounds(mp4_path)

            if bounds is None:
                self.log("Could not detect content bounds, using defaults", verbose)
                # Assume C64 content is centered
                c64_height = C64_VIDEO_HEIGHT_NTSC
                if "format" in properties and properties["format"].upper() == "PAL":
                    c64_height = C64_VIDEO_HEIGHT_PAL
                scale = height / c64_height
                content_width = int(C64_VIDEO_WIDTH * scale)
                content_left = (width - content_width) // 2
                content_right = content_left + content_width
                content_top = 0
                content_bottom = height
            else:
                # Get content region from first content frame
                first_frame_idx = bounds.first_content_frame
                last_frame_idx = bounds.last_content_frame

                # Read a frame to determine content bounds
                cap.set(cv2.CAP_PROP_POS_FRAMES, first_frame_idx + 5)
                ret, frame = cap.read()
                if not ret:
                    return None

                content_left, content_right, content_top, content_bottom = self._detect_content_region(frame)

            content_width = content_right - content_left
            content_height = content_bottom - content_top

            self.log(
                f"Content region: ({content_left}, {content_top}) to ({content_right}, {content_bottom})",
                verbose,
            )

            # Calculate scale from C64 coordinates to output pixels
            c64_height = C64_VIDEO_HEIGHT_NTSC
            if "format" in properties and properties["format"].upper() == "PAL":
                c64_height = C64_VIDEO_HEIGHT_PAL
            scale_x = content_width / C64_VIDEO_WIDTH
            scale_y = content_height / c64_height

            self.log(f"Scale factors: x={scale_x:.3f}, y={scale_y:.3f}", verbose)

            # Calculate top-right corner position in output coordinates
            # In C64 coords: top-right corner starts at (384 - 88, 0) = (296, 0)
            tr_c64_x = C64_VIDEO_WIDTH - C64_CORNER_OUTER_WIDTH  # 296
            tr_c64_y = 0

            # Inner region in C64 coords (after 8px frame)
            inner_c64_x = tr_c64_x + C64_CORNER_FRAME_TOTAL  # 304
            inner_c64_y = tr_c64_y + C64_CORNER_FRAME_TOTAL  # 8

            # Map to output coordinates
            inner_out_x = content_left + int(inner_c64_x * scale_x)
            inner_out_y = content_top + int(inner_c64_y * scale_y)
            inner_out_w = int(C64_CORNER_INNER_WIDTH * scale_x)
            inner_out_h = int(C64_CORNER_INNER_HEIGHT * scale_y)

            self.log(
                f"Palette inner region: ({inner_out_x}, {inner_out_y}) size {inner_out_w}x{inner_out_h}",
                verbose,
            )

            # Collect color samples from multiple frames
            skip_frames = int(self.thresholds["skip_frames"])
            min_frames = int(self.thresholds["min_frames"])
            roi_shrink = self.thresholds["roi_shrink"]

            # Calculate per-swatch regions using correct C64 dimensions
            # Each swatch cell is 17x9 in C64 coords, with 15x7 colored portion
            swatch_out_w = C64_SWATCH_WIDTH * scale_x
            swatch_out_h = C64_SWATCH_HEIGHT * scale_y
            color_out_w = C64_SWATCH_COLOR_WIDTH * scale_x
            color_out_h = C64_SWATCH_COLOR_HEIGHT * scale_y

            # Shrink factor for sampling (avoid edges and gaps)
            shrink_w = color_out_w * roi_shrink
            shrink_h = color_out_h * roi_shrink

            # Sample frames
            if bounds:
                start_frame = bounds.first_content_frame + skip_frames
                end_frame = bounds.last_content_frame - skip_frames
            else:
                start_frame = skip_frames
                end_frame = total_frames - skip_frames - 1

            num_sample_frames = min(min_frames + 5, max(1, end_frame - start_frame))
            frame_step = max(1, (end_frame - start_frame) // num_sample_frames)

            # Accumulate color samples for each of 16 colors
            color_samples = [[] for _ in range(16)]

            for frame_idx in range(start_frame, end_frame, frame_step):
                cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
                ret, frame = cap.read()
                if not ret:
                    continue

                # Convert BGR to RGB for comparison
                frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

                # Extract each swatch color
                for color_idx in range(16):
                    row = color_idx // 4
                    col = color_idx % 4

                    # Calculate swatch position in output coords
                    # Layout: 2px padding, then 4x4 grid of 17x9 cells (each with 15x7 color, 2px gap)
                    padding_out_x = C64_PALETTE_PADDING * scale_x
                    padding_out_y = C64_PALETTE_PADDING * scale_y

                    # Cell origin (top-left of the 17x9 cell)
                    cell_x = inner_out_x + padding_out_x + col * swatch_out_w
                    cell_y = inner_out_y + padding_out_y + row * swatch_out_h

                    # Center of the colored portion (15x7), which starts at cell origin
                    color_center_x = cell_x + color_out_w / 2
                    color_center_y = cell_y + color_out_h / 2

                    # Get shrunk ROI centered on the colored portion
                    roi_x1 = int(color_center_x - shrink_w / 2)
                    roi_y1 = int(color_center_y - shrink_h / 2)
                    roi_x2 = int(color_center_x + shrink_w / 2)
                    roi_y2 = int(color_center_y + shrink_h / 2)

                    # Clamp to frame bounds
                    roi_x1 = max(0, min(roi_x1, width - 1))
                    roi_y1 = max(0, min(roi_y1, height - 1))
                    roi_x2 = max(roi_x1 + 1, min(roi_x2, width))
                    roi_y2 = max(roi_y1 + 1, min(roi_y2, height))

                    # Extract ROI
                    roi = frame_rgb[roi_y1:roi_y2, roi_x1:roi_x2]
                    if roi.size == 0:
                        continue

                    # Use median for robustness against scanlines and artifacts
                    median_color = np.median(roi, axis=(0, 1))
                    color_samples[color_idx].append(median_color)

            # Aggregate samples using median across frames
            observed_colors = []
            for color_idx in range(16):
                samples = color_samples[color_idx]
                if len(samples) == 0:
                    self.log(f"No samples for color {color_idx}", verbose)
                    observed_colors.append((0, 0, 0))
                else:
                    median_rgb = np.median(samples, axis=0).astype(int)
                    observed_colors.append(tuple(median_rgb))
                    self.log(
                        f"Color {color_idx}: observed RGB = {tuple(median_rgb)} from {len(samples)} samples",
                        verbose,
                    )

            return observed_colors

        finally:
            cap.release()

    def _detect_content_region(self, frame: np.ndarray) -> tuple[int, int, int, int]:
        """Detect the C64 content region within the frame (excluding letterbox)."""
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
            # Fallback
            left = 0
            right = width

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

    def _compare_palettes(
        self,
        expected: list[tuple[int, int, int]],
        observed: list[tuple[int, int, int]],
        verbose: bool,
    ) -> tuple[float, list[int], list[dict]]:
        """Compare expected and observed palettes.

        Returns:
            max_delta: Maximum per-channel delta across all colors
            failing_indices: List of color indices that exceed threshold
            comparison_details: List of dicts with comparison info, sorted by delta
        """
        max_allowed = self.thresholds["max_channel_delta"]
        max_delta = 0.0
        failing_indices = []
        comparison_details = []

        for idx in range(16):
            exp_r, exp_g, exp_b = expected[idx]
            obs_r, obs_g, obs_b = observed[idx]

            delta_r = abs(exp_r - obs_r)
            delta_g = abs(exp_g - obs_g)
            delta_b = abs(exp_b - obs_b)
            delta = max(delta_r, delta_g, delta_b)

            max_delta = max(max_delta, delta)

            detail = {
                "index": idx,
                "expected": f"({exp_r},{exp_g},{exp_b})",
                "observed": f"({obs_r},{obs_g},{obs_b})",
                "delta": delta,
                "delta_rgb": (delta_r, delta_g, delta_b),
            }
            comparison_details.append(detail)

            if delta > max_allowed:
                failing_indices.append(idx)
                self.log(
                    f"Color {idx} FAIL: expected ({exp_r},{exp_g},{exp_b}), "
                    f"observed ({obs_r},{obs_g},{obs_b}), delta={delta}",
                    verbose,
                )
            else:
                self.log(
                    f"Color {idx} OK: expected ({exp_r},{exp_g},{exp_b}), "
                    f"observed ({obs_r},{obs_g},{obs_b}), delta={delta}",
                    verbose,
                )

        # Sort by delta descending
        comparison_details.sort(key=lambda x: x["delta"], reverse=True)

        return max_delta, failing_indices, comparison_details
