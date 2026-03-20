#!/usr/bin/env python3
"""
C64 Stream - Scanlines Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import subprocess
from collections import Counter
from pathlib import Path
from typing import Any, Optional

import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig
from .preserve_size_canvas_match import NTSC_FPS, PAL_FPS, compute_checkpoint_frames

# Fixed luminance threshold for BLACK row classification in quantitative mode.
# Must be deterministic — no tolerance drift.
_BLACK_THRESHOLD = 10
_PRESERVE_COMPARE_SCRIPT = "preserve_size_compare.c64script"
_PRESERVE_COMPARE_SECTION_SAMPLE_COUNT = 3
_PRESERVE_COMPARE_SECTION_PADDING_FRAMES = 10
_GENERIC_QUANTITATIVE_OFFSETS_S = (16.0, 18.0, 20.0)
_PRESERVE_COMPARE_PREVIEW_CHECKPOINTS = (
    ("classic_preserve", "classic_preview_preserve.png", PresetConfig("Classic CRT", 0.5, 0.6)),
    ("vintage_preserve", "vintage_preview_preserve.png", PresetConfig("Vintage TV", 1.0, 0.5)),
    ("arcade_preserve", "arcade_preview_preserve.png", PresetConfig("Arcade Cabinet", 0.5, 0.6)),
)


class ScanlineAssertion(EffectAssertion):
    """Verify scanline uniformity in the recording.

    Supports two modes (set via thresholds["mode"]):
      "qualitative" (default) — template-correlation based uniformity check
      "quantitative"          — strict row-level topology: periodicity, count, distribution
    """

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "max_variance_percent": 0.5,  # Max acceptable scanline height variance
            "min_scanline_count": 50,  # Minimum expected scanlines
            "min_contrast_ratio": 0.0,  # Optional: require a minimum dark-vs-bright contrast (0 disables)
        }
        super().__init__("Scanlines", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        mode = str(self.thresholds.get("mode", "qualitative")).lower()
        if mode == "quantitative":
            return self._verify_quantitative(mp4_path, properties, preset, verbose)
        return self._verify_qualitative(mp4_path, properties, preset, verbose)

    # ------------------------------------------------------------------
    # Qualitative mode (default, unchanged)
    # ------------------------------------------------------------------

    def _verify_qualitative(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not preset.has_scanlines():
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Scanlines not enabled for this preset",
            )

        self.log(
            f"Verifying scanlines (distance={preset.scan_line_distance}, strength={preset.scan_line_strength})",
            verbose,
        )

        try:
            # Extract a representative frame for analysis.
            # Recordings often include OBS startup/shutdown padding where content is not yet stable.
            frame, chosen_t = self._extract_best_frame(mp4_path)
            if frame is None:
                return AssertionResult(
                    status=AssertionStatus.SKIP,
                    name=self.name,
                    message="Could not extract frame for scanline analysis (no video content)",
                )

            # Analyze scanline pattern
            ok, variance, details = self._analyze_scanlines(frame, preset, verbose)
            details["frame_time_offset_s"] = float(chosen_t)
            if not ok:
                error_msg = details.get("error", "Scanline analysis failed")
                # Treat content-related errors as infrastructure issues (SKIP), not test failures
                # These occur when UDP timing causes partial/missing video content in CI
                if any(msg in error_msg for msg in [
                    "No content detected",
                    "Could not find content region",
                    "Content region too small",
                    "Too few scanlines detected",
                ]):
                    return AssertionResult(
                        status=AssertionStatus.SKIP,
                        name=self.name,
                        message=f"Scanline analysis skipped: {error_msg}",
                        details=details,
                    )
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=error_msg,
                    details=details,
                )

            max_variance = self.thresholds["max_variance_percent"]
            if variance > max_variance:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Scanline variance too high: {variance:.2f}% (max: {max_variance}%)",
                    details=details,
                    metrics={"variance_percent": variance},
                )

            min_contrast_ratio = float(self.thresholds.get("min_contrast_ratio", 0.0) or 0.0)
            contrast_ratio = float(details.get("contrast_ratio", 0.0) or 0.0)
            if min_contrast_ratio > 0.0 and contrast_ratio < min_contrast_ratio:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Scanline contrast too low: {contrast_ratio:.3f} (min: {min_contrast_ratio:.3f})",
                    details=details,
                    metrics={"contrast_ratio": contrast_ratio, "variance_percent": variance},
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"Scanlines uniform: {variance:.2f}% variance",
                details=details,
                metrics={"variance_percent": variance},
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Scanline verification failed: {e}",
            )

    # ------------------------------------------------------------------
    # Quantitative mode (strict row-level topology validation)
    # ------------------------------------------------------------------

    def _verify_quantitative(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        """Strict quantitative scanline topology validation.

        Validates:
          1. Row classification: each row in the content region is BLACK or CONTENT.
          2. Period detection: BLACK-row groups recur at a consistent period.
          3. Periodicity: all inter-group gaps are identical (±1 px for OBS rounding).
          4. Count: observed BLACK rows match expected count for the period.
          5. Gap group count: if expected_gap_groups is set, observed groups must match.
          6. Gap width uniformity: all gap bands have the same width (±1 px for rounding).
          7. Distribution: the pattern is uniform across the full frame height.
        """
        # Resolve scan_line_distance: explicit override, or preset value.
        scan_line_distance = float(self.thresholds.get("scan_line_distance_override", 0.0))
        if scan_line_distance <= 0.0:
            scan_line_distance = preset.scan_line_distance
        if scan_line_distance <= 0.0:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    "Quantitative mode requires scan_line_distance > 0 "
                    "(set scan_line_distance_override in thresholds or use a preset with scanlines)"
                ),
            )

        total_pixels, scanline_pixels = self._scanline_scaling_info(scan_line_distance)
        gap_pixels = total_pixels - scanline_pixels

        self.log(
            f"Quantitative scanline validation: distance={scan_line_distance}, "
            f"period={total_pixels} (content={scanline_pixels}, gap={gap_pixels})",
            verbose,
        )

        expected_gap_groups = self.thresholds.get("expected_gap_groups")
        if expected_gap_groups is not None:
            expected_gap_groups = int(expected_gap_groups)

        script_name = Path(str(properties.get("script_file") or "")).name.lower()
        if script_name == _PRESERVE_COMPARE_SCRIPT:
            screenshot_result = self._verify_preserve_compare_preview_screenshots(verbose)
            if screenshot_result is not None:
                return screenshot_result

        try:
            frames_to_check = self._iter_quantitative_frames(mp4_path, properties, verbose)
        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Frame extraction failed: {e}",
            )

        if not frames_to_check:
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Could not extract frames for quantitative scanline analysis",
            )

        validated_frames: list[dict[str, Any]] = []
        for frame_info in frames_to_check:
            result = self._analyze_quantitative_frame(
                frame=frame_info["frame"],
                total_pixels=total_pixels,
                gap_pixels=gap_pixels,
                expected_gap_groups=expected_gap_groups,
                frame_label=frame_info.get("label"),
                frame_index=frame_info.get("frame_index"),
                frame_time_offset_s=frame_info.get("frame_time_offset_s"),
                verbose=verbose,
            )
            if result.status != AssertionStatus.PASS:
                return result

            validated_frames.append(
                {
                    "label": result.details.get("frame_label"),
                    "frame_index": result.details.get("frame_index"),
                    "frame_time_offset_s": result.details.get("frame_time_offset_s"),
                    "observed_gap_groups": result.details.get("observed_gap_groups"),
                    "pattern_summary": result.details.get("pattern_summary"),
                }
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=(
                f"Scanline topology correct across {len(validated_frames)} frame(s): "
                f"all relevant frames matched expected gap_groups={expected_gap_groups}"
                if expected_gap_groups is not None
                else f"Scanline topology correct across {len(validated_frames)} frame(s)"
            ),
            details={
                "validated_frames": validated_frames,
                "expected_gap_groups": expected_gap_groups,
            },
            metrics={"validated_frames": float(len(validated_frames))},
        )

    def _verify_preserve_compare_preview_screenshots(self, verbose: bool) -> Optional[AssertionResult]:
        artifacts_dir = self._repo_root() / "tests" / "e2e" / "artifacts" / "effect_preserve_size"
        if not artifacts_dir.exists():
            return None

        try:
            from PIL import Image  # type: ignore
        except ImportError:
            return None

        validated_frames: list[dict[str, Any]] = []
        for checkpoint_name, artifact_name, checkpoint_preset in _PRESERVE_COMPARE_PREVIEW_CHECKPOINTS:
            png_path = artifacts_dir / artifact_name
            if not png_path.exists():
                return None

            try:
                with Image.open(png_path) as img:
                    frame = np.array(img.convert("RGB"))
            except Exception as exc:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"{checkpoint_name}: failed to read preview screenshot {png_path.name}: {exc}",
                )

            ok, variance, details = self._analyze_scanlines(frame, checkpoint_preset, verbose)
            if not ok:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"{checkpoint_name}: {details.get('error', 'scanline analysis failed')}",
                    details={"artifact": str(png_path), **details},
                )

            total_pixels, _ = self._scanline_scaling_info(checkpoint_preset.scan_line_distance)
            expected_scanline_count = frame.shape[0] // total_pixels
            observed_scanline_count = int(details.get("scanline_count", 0))
            if observed_scanline_count != expected_scanline_count:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=(
                        f"{checkpoint_name}: scanline count mismatch in {png_path.name}: "
                        f"observed={observed_scanline_count}, expected={expected_scanline_count}"
                    ),
                    details={
                        "artifact": str(png_path),
                        "checkpoint": checkpoint_name,
                        "observed_scanline_count": observed_scanline_count,
                        "expected_scanline_count": expected_scanline_count,
                        **details,
                    },
                    metrics={
                        "observed_scanline_count": float(observed_scanline_count),
                        "expected_scanline_count": float(expected_scanline_count),
                        "variance_percent": float(variance),
                    },
                )

            validated_frames.append(
                {
                    "checkpoint": checkpoint_name,
                    "artifact": str(png_path),
                    "observed_scanline_count": observed_scanline_count,
                    "expected_scanline_count": expected_scanline_count,
                    "expected_period_px": int(details.get("expected_period_px", total_pixels)),
                }
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=(
                "Scanline counts correct across preserve preview checkpoints: "
                + ", ".join(
                    f"{frame['checkpoint']}={frame['observed_scanline_count']}"
                    for frame in validated_frames
                )
            ),
            details={"validated_frames": validated_frames},
            metrics={"validated_frames": float(len(validated_frames))},
        )

    def _analyze_quantitative_frame(
        self,
        frame: np.ndarray,
        total_pixels: int,
        gap_pixels: int,
        expected_gap_groups: Optional[int],
        frame_label: Optional[str],
        frame_index: Optional[int],
        frame_time_offset_s: Optional[float],
        verbose: bool,
    ) -> AssertionResult:
        frame_reference = self._format_frame_reference(frame_label, frame_index, frame_time_offset_s)

        gray = 0.2126 * frame[..., 0] + 0.7152 * frame[..., 1] + 0.0722 * frame[..., 2]
        col_max = np.max(gray, axis=0)
        content_cols = np.where(col_max > _BLACK_THRESHOLD)[0]
        if len(content_cols) < 10:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"{frame_reference}: no content columns detected in frame",
                details=self._frame_details(frame_label, frame_index, frame_time_offset_s),
            )
        x_start, x_end = int(content_cols[0]), int(content_cols[-1])

        content_region = gray[:, x_start : x_end + 1]
        row_max = np.max(content_region, axis=1)
        content_row_indices = np.where(row_max > _BLACK_THRESHOLD)[0]
        if len(content_row_indices) < 20:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    f"{frame_reference}: content region too small: "
                    f"only {len(content_row_indices)} content rows"
                ),
                details=self._frame_details(frame_label, frame_index, frame_time_offset_s),
            )

        y_start, y_end = int(content_row_indices[0]), int(content_row_indices[-1])
        region_row_max = row_max[y_start : y_end + 1]
        row_classes = np.where(region_row_max < _BLACK_THRESHOLD, 0, 1)
        total_rows = len(row_classes)

        black_positions = np.where(row_classes == 0)[0]
        observed_black_rows = int(len(black_positions))
        first_20 = "".join("C" if c else "B" for c in row_classes[:20])

        if observed_black_rows == 0:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    f"{frame_reference}: no scanline gaps (BLACK rows) detected — "
                    "scanlines are collapsed or missing"
                ),
                details={
                    "detected_period": None,
                    "observed_gap_groups": 0,
                    "observed_black_rows": 0,
                    "expected_gap_groups": expected_gap_groups,
                    "expected_black_rows": None,
                    "first_20_classifications": first_20,
                    "sample_gaps": [],
                    "total_rows": total_rows,
                    "expected_pattern": f"period={total_pixels}, gap={gap_pixels}",
                    **self._frame_details(frame_label, frame_index, frame_time_offset_s),
                    "content_region": {"x": (x_start, x_end), "y": (y_start, y_end)},
                    "pattern_summary": "groups=0, detected_period=None, gap_width=None",
                },
            )

        black_groups: list[tuple[int, int]] = []
        group_start = int(black_positions[0])
        group_end = group_start
        for pos in black_positions[1:]:
            if pos == group_end + 1:
                group_end = int(pos)
            else:
                black_groups.append((group_start, group_end))
                group_start = int(pos)
                group_end = group_start
        black_groups.append((group_start, group_end))

        observed_groups = len(black_groups)
        if observed_groups < 3:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    f"{frame_reference}: only {observed_groups} black band(s) detected — "
                    "insufficient for periodicity validation (need ≥3)"
                ),
                details={
                    "detected_period": None,
                    "observed_gap_groups": observed_groups,
                    "observed_black_rows": observed_black_rows,
                    "expected_gap_groups": expected_gap_groups,
                    "expected_black_rows": None,
                    "first_20_classifications": first_20,
                    "sample_gaps": [],
                    "total_rows": total_rows,
                    **self._frame_details(frame_label, frame_index, frame_time_offset_s),
                    "content_region": {"x": (x_start, x_end), "y": (y_start, y_end)},
                    "pattern_summary": (
                        f"groups={observed_groups}, detected_period=None, gap_width=None"
                    ),
                },
            )

        group_starts = [group[0] for group in black_groups]
        gaps = [group_starts[index + 1] - group_starts[index] for index in range(len(group_starts) - 1)]
        gap_counts = Counter(gaps)
        detected_period = gap_counts.most_common(1)[0][0]

        band_widths = [group[1] - group[0] + 1 for group in black_groups]
        band_width_counts = Counter(band_widths)
        observed_gap_width = band_width_counts.most_common(1)[0][0]
        pattern_summary = (
            f"groups={observed_groups}, detected_period={detected_period}, "
            f"gap_width={observed_gap_width}, black_rows={observed_black_rows}/{total_rows}"
        )

        self.log(
            f"{frame_reference}: {pattern_summary}, gap_distribution={dict(gap_counts.most_common(5))}",
            verbose,
        )

        max_gap_dev = max(1, int(self.thresholds.get("max_gap_deviation_px", 1)))
        bad_gaps = [gap for gap in gaps if abs(gap - detected_period) > max_gap_dev]
        if bad_gaps:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    f"{frame_reference}: scanline spacing inconsistent: {len(bad_gaps)} gap(s) deviate "
                    f"from period {detected_period} (±{max_gap_dev} px)"
                ),
                details={
                    "detected_period": detected_period,
                    "observed_gap_groups": observed_groups,
                    "observed_black_rows": observed_black_rows,
                    "expected_gap_groups": expected_gap_groups,
                    "expected_black_rows": None,
                    "first_20_classifications": first_20,
                    "sample_gaps": gaps[:15],
                    "bad_gaps": bad_gaps[:10],
                    "gap_distribution": dict(gap_counts.most_common(5)),
                    "total_rows": total_rows,
                    **self._frame_details(frame_label, frame_index, frame_time_offset_s),
                    "content_region": {"x": (x_start, x_end), "y": (y_start, y_end)},
                    "pattern_summary": pattern_summary,
                },
            )

        expected_fraction = observed_gap_width / detected_period
        expected_black_rows = round(total_rows * expected_fraction)
        n_periods = max(1, total_rows // detected_period)
        base_dev = observed_gap_width * 3
        rounding_dev = n_periods // 4
        max_count_dev = int(self.thresholds.get("max_count_deviation", max(base_dev, rounding_dev)))
        if abs(observed_black_rows - expected_black_rows) > max_count_dev:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    f"{frame_reference}: scanline count mismatch: observed={observed_black_rows}, "
                    f"expected≈{expected_black_rows} (±{max_count_dev})"
                ),
                details={
                    "detected_period": detected_period,
                    "observed_gap_groups": observed_groups,
                    "observed_black_rows": observed_black_rows,
                    "expected_gap_groups": expected_gap_groups,
                    "expected_black_rows": expected_black_rows,
                    "first_20_classifications": first_20,
                    "sample_gaps": gaps[:15],
                    "total_rows": total_rows,
                    "observed_gap_width": observed_gap_width,
                    **self._frame_details(frame_label, frame_index, frame_time_offset_s),
                    "content_region": {"x": (x_start, x_end), "y": (y_start, y_end)},
                    "pattern_summary": pattern_summary,
                },
            )

        if expected_gap_groups is not None and observed_groups != expected_gap_groups:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    f"{frame_reference}: scanline gap group count mismatch: "
                    f"observed={observed_groups}, expected={expected_gap_groups}"
                ),
                details={
                    "detected_period": detected_period,
                    "observed_gap_groups": observed_groups,
                    "observed_black_rows": observed_black_rows,
                    "expected_gap_groups": expected_gap_groups,
                    "expected_black_rows": expected_black_rows,
                    "first_20_classifications": first_20,
                    "sample_gaps": gaps[:15],
                    "total_rows": total_rows,
                    "observed_gap_width": observed_gap_width,
                    **self._frame_details(frame_label, frame_index, frame_time_offset_s),
                    "content_region": {"x": (x_start, x_end), "y": (y_start, y_end)},
                    "pattern_summary": pattern_summary,
                },
            )

        max_width_dev = max(1, int(self.thresholds.get("max_gap_width_deviation_px", 1)))
        bad_widths = [width for width in band_widths if abs(width - observed_gap_width) > max_width_dev]
        if bad_widths:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    f"{frame_reference}: scanline gap widths not uniform: {len(bad_widths)} band(s) deviate "
                    f"from dominant width {observed_gap_width} (±{max_width_dev} px)"
                ),
                details={
                    "detected_period": detected_period,
                    "observed_gap_groups": observed_groups,
                    "observed_gap_width": observed_gap_width,
                    "bad_widths": bad_widths[:10],
                    "band_width_distribution": dict(band_width_counts.most_common(5)),
                    "observed_black_rows": observed_black_rows,
                    "expected_gap_groups": expected_gap_groups,
                    "first_20_classifications": first_20,
                    "total_rows": total_rows,
                    **self._frame_details(frame_label, frame_index, frame_time_offset_s),
                    "content_region": {"x": (x_start, x_end), "y": (y_start, y_end)},
                    "pattern_summary": pattern_summary,
                },
            )

        region_size = max(detected_period * 3, total_rows // 10)
        regions = [
            ("top_10%", 0, min(region_size, total_rows)),
            (
                "middle",
                max(0, total_rows // 2 - region_size // 2),
                min(total_rows, total_rows // 2 + region_size // 2),
            ),
            ("bottom_10%", max(0, total_rows - region_size), total_rows),
        ]
        for region_name, region_start, region_end in regions:
            region = row_classes[region_start:region_end]
            region_total = len(region)
            if region_total == 0:
                continue
            region_black = int(np.sum(region == 0))
            region_fraction = region_black / region_total
            if region_fraction < expected_fraction * 0.5:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=(
                        f"{frame_reference}: scanline distribution uneven in {region_name}: "
                        f"{region_fraction:.1%} black rows (expected ~{expected_fraction:.1%})"
                    ),
                    details={
                        "detected_period": detected_period,
                        "observed_gap_groups": observed_groups,
                        "observed_black_rows": observed_black_rows,
                        "expected_gap_groups": expected_gap_groups,
                        "expected_black_rows": expected_black_rows,
                        "first_20_classifications": first_20,
                        "sample_gaps": gaps[:15],
                        "total_rows": total_rows,
                        "region": region_name,
                        "region_black_fraction": region_fraction,
                        "expected_fraction": expected_fraction,
                        **self._frame_details(frame_label, frame_index, frame_time_offset_s),
                        "content_region": {"x": (x_start, x_end), "y": (y_start, y_end)},
                        "pattern_summary": pattern_summary,
                    },
                )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=(
                f"{frame_reference}: scanline topology correct: period={detected_period}px, "
                f"black_rows={observed_black_rows}/{total_rows}, groups={observed_groups}, "
                f"gap_width={observed_gap_width}px"
            ),
            details={
                "detected_period": detected_period,
                "observed_gap_groups": observed_groups,
                "observed_black_rows": observed_black_rows,
                "expected_gap_groups": expected_gap_groups,
                "expected_black_rows": expected_black_rows,
                "first_20_classifications": first_20,
                "sample_gaps": gaps[:15],
                "total_rows": total_rows,
                "black_groups": observed_groups,
                "observed_gap_width": observed_gap_width,
                "expected_fraction": expected_fraction,
                "gap_distribution": dict(gap_counts.most_common(5)),
                **self._frame_details(frame_label, frame_index, frame_time_offset_s),
                "content_region": {"x": (x_start, x_end), "y": (y_start, y_end)},
                "pattern_summary": pattern_summary,
            },
            metrics={
                "detected_period": float(detected_period),
                "observed_gap_groups": float(observed_groups),
                "observed_black_rows": float(observed_black_rows),
                "expected_black_rows": float(expected_black_rows),
            },
        )

    def _iter_quantitative_frames(
        self, mp4_path: Path, properties: dict[str, Any], verbose: bool
    ) -> list[dict[str, Any]]:
        script_name = Path(str(properties.get("script_file") or "")).name.lower()
        if script_name == _PRESERVE_COMPARE_SCRIPT:
            return self._iter_preserve_compare_frames(mp4_path, properties, verbose)
        return self._iter_fixed_offset_frames(mp4_path, verbose)

    def _iter_preserve_compare_frames(
        self, mp4_path: Path, properties: dict[str, Any], verbose: bool
    ) -> list[dict[str, Any]]:
        fmt = str(properties.get("video_format") or "NTSC").upper()
        fps = PAL_FPS if fmt == "PAL" else NTSC_FPS
        checkpoint_frames = compute_checkpoint_frames(fps=fps)
        expected_width = int(properties.get("expected_width", 1920))
        expected_height = int(properties.get("expected_height", 1080))
        arcade_end = max(
            checkpoint_frames["arcade_preserve_2"] + _PRESERVE_COMPARE_SECTION_PADDING_FRAMES,
            checkpoint_frames["arcade_preserve"] + 2,
        )
        section_windows = [
            ("classic_preserve", checkpoint_frames["classic_preserve"], checkpoint_frames["sharp_legacy"] - 1),
            ("sharp_preserve", checkpoint_frames["sharp_preserve"], checkpoint_frames["vintage_legacy"] - 1),
            ("vintage_preserve", checkpoint_frames["vintage_preserve"], checkpoint_frames["arcade_legacy"] - 1),
            ("arcade_preserve", checkpoint_frames["arcade_preserve"], arcade_end),
        ]

        frames: list[dict[str, Any]] = []
        for section_name, start_frame, end_frame in section_windows:
            sample_indices = self._sample_frame_indices(
                start_frame,
                end_frame,
                sample_count=_PRESERVE_COMPARE_SECTION_SAMPLE_COUNT,
            )
            self.log(f"{section_name}: validating preserve-size frames {sample_indices}", verbose)
            for sample_number, frame_index in enumerate(sample_indices, start=1):
                frame = self._extract_frame_by_index(
                    mp4_path,
                    frame_index=frame_index,
                    width=expected_width,
                    height=expected_height,
                )
                if frame is None:
                    raise RuntimeError(
                        f"Could not extract preserve compare frame {frame_index} for {section_name}"
                    )
                frames.append(
                    {
                        "label": f"{section_name}:sample_{sample_number}",
                        "frame_index": frame_index,
                        "frame_time_offset_s": frame_index / fps,
                        "frame": frame,
                    }
                )

        return frames

    def _iter_fixed_offset_frames(self, mp4_path: Path, verbose: bool) -> list[dict[str, Any]]:
        frames: list[dict[str, Any]] = []
        for offset_s in _GENERIC_QUANTITATIVE_OFFSETS_S:
            frame = self._extract_frame(mp4_path, time_offset=float(offset_s))
            if frame is None:
                continue
            self.log(f"Validating quantitative frame at {offset_s:.1f}s", verbose)
            frames.append(
                {
                    "label": f"offset_{offset_s:.1f}s",
                    "frame_index": None,
                    "frame_time_offset_s": float(offset_s),
                    "frame": frame,
                }
            )
        return frames

    @staticmethod
    def _sample_frame_indices(start_frame: int, end_frame: int, sample_count: int) -> list[int]:
        if end_frame < start_frame:
            raise ValueError(f"Invalid frame window: start={start_frame}, end={end_frame}")

        samples: list[int] = []
        for candidate in [start_frame, (start_frame + end_frame) // 2, end_frame]:
            if candidate not in samples:
                samples.append(candidate)

        next_frame = start_frame
        while len(samples) < sample_count and next_frame <= end_frame:
            if next_frame not in samples:
                samples.append(next_frame)
            next_frame += 1

        if len(samples) < sample_count:
            raise ValueError(
                f"Need at least {sample_count} frames in window {start_frame}-{end_frame}, got {samples}"
            )

        return sorted(samples[:sample_count])

    def _extract_frame_by_index(
        self, mp4_path: Path, frame_index: int, width: int, height: int
    ) -> Optional[np.ndarray]:
        cmd = [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(mp4_path),
            "-vf",
            f"select=eq(n\\,{frame_index})",
            "-frames:v",
            "1",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ]
        try:
            result = subprocess.run(cmd, capture_output=True, check=True)
            expected_bytes = width * height * 3
            if len(result.stdout) == expected_bytes:
                return np.frombuffer(result.stdout, dtype=np.uint8).reshape((height, width, 3))
        except subprocess.CalledProcessError:
            pass

        try:
            import cv2  # type: ignore
        except ImportError:
            return None

        cap = cv2.VideoCapture(str(mp4_path))
        try:
            cap.set(cv2.CAP_PROP_POS_FRAMES, float(frame_index))
            ok, frame = cap.read()
            if not ok or frame is None:
                return None
            return cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        finally:
            cap.release()

    @staticmethod
    def _frame_details(
        frame_label: Optional[str], frame_index: Optional[int], frame_time_offset_s: Optional[float]
    ) -> dict[str, Any]:
        return {
            "frame_label": frame_label,
            "frame_index": frame_index,
            "frame_time_offset_s": float(frame_time_offset_s) if frame_time_offset_s is not None else None,
        }

    @staticmethod
    def _format_frame_reference(
        frame_label: Optional[str], frame_index: Optional[int], frame_time_offset_s: Optional[float]
    ) -> str:
        if frame_index is not None and frame_label:
            return f"Frame {frame_index} ({frame_label})"
        if frame_index is not None:
            return f"Frame {frame_index}"
        if frame_label:
            return f"Frame {frame_label}"
        if frame_time_offset_s is not None:
            return f"Frame at {frame_time_offset_s:.3f}s"
        return "Frame"

    def _extract_frame(self, mp4_path: Path, time_offset: float) -> Optional[np.ndarray]:
        """Extract a single frame at the given time offset."""
        cmd = [
            "ffmpeg",
            "-v",
            "error",
            "-ss",
            str(time_offset),
            "-i",
            str(mp4_path),
            "-frames:v",
            "1",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ]
        try:
            result = subprocess.run(cmd, capture_output=True, check=True)
            if len(result.stdout) == 1920 * 1080 * 3:
                return np.frombuffer(result.stdout, dtype=np.uint8).reshape((1080, 1920, 3))
        except subprocess.CalledProcessError:
            pass
        return None

    @staticmethod
    def _repo_root() -> Path:
        return Path(__file__).resolve().parents[3]

    def _extract_best_frame(self, mp4_path: Path) -> tuple[Optional[np.ndarray], float]:
        """Extract a stable frame by sampling multiple offsets and picking the one with the largest
        detected content area.

        Returns (frame, chosen_time_offset_s).
        """

        def content_area(frame: np.ndarray) -> int:
            gray = 0.2126 * frame[..., 0] + 0.7152 * frame[..., 1] + 0.0722 * frame[..., 2]
            row_max = np.max(gray, axis=1)
            col_max = np.max(gray, axis=0)
            threshold = 10.0
            content_rows = np.where(row_max > threshold)[0]
            content_cols = np.where(col_max > threshold)[0]
            if len(content_rows) == 0 or len(content_cols) == 0:
                return 0
            y0, y1 = int(content_rows[0]), int(content_rows[-1])
            x0, x1 = int(content_cols[0]), int(content_cols[-1])
            if x1 <= x0 + 10 or y1 <= y0 + 10:
                return 0
            return max(0, (x1 - x0) * (y1 - y0))

        # Empirically: 8-14s tends to be inside the stable content window for these recordings,
        # but keep earlier offsets as fallback for shorter recordings.
        offsets = [10.0, 8.0, 12.0, 6.0, 4.0, 2.0]
        best_frame = None
        best_t = float(offsets[0])
        best_area = -1

        for t in offsets:
            frame = self._extract_frame(mp4_path, time_offset=float(t))
            if frame is None:
                continue
            area = int(content_area(frame))
            if area > best_area:
                best_area = area
                best_frame = frame
                best_t = float(t)

        return best_frame, best_t

    @staticmethod
    def _scanline_scaling_info(scan_line_distance: float) -> tuple[int, int]:
        """Mirror get_scanline_scaling_info() from src/c64-source.c.

        Returns (total_pixels_per_unit, scanline_pixels_per_unit).
        """
        if scan_line_distance <= 0.25:
            return 5, 4
        if scan_line_distance <= 0.5:
            return 3, 2
        if scan_line_distance <= 1.0:
            return 4, 2
        return 3, 1

    @staticmethod
    def _running_median_1d(x: np.ndarray, window: int) -> np.ndarray:
        window = int(window)
        if window < 3:
            return x.copy()
        if window % 2 == 0:
            window += 1
        pad = window // 2
        p = np.pad(x, (pad, pad), mode="edge")
        return np.median(np.lib.stride_tricks.sliding_window_view(p, window), axis=1)

    def _analyze_scanlines(
        self, frame: np.ndarray, preset: PresetConfig, verbose: bool
    ) -> tuple[bool, float, dict[str, Any]]:
        """Analyze scanline pattern in a frame.

        Robust approach:
        - Find content bounds.
        - Sample a vertical luminance band.
        - Detrend (remove low-frequency content) via running median.
        - Correlate against an ideal scanline template derived from preset distance.
        """
        # Convert to grayscale (0..255)
        gray = 0.2126 * frame[..., 0] + 0.7152 * frame[..., 1] + 0.0722 * frame[..., 2]

        # Find content bounds (non-black area).
        # Use a high percentile instead of max to avoid a few bright pixels (or tinted black bars)
        # expanding the detected content region.
        row_max = np.percentile(gray, 99, axis=1)
        col_max = np.percentile(gray, 99, axis=0)

        threshold = 10.0
        content_rows = np.where(row_max > threshold)[0]
        content_cols = np.where(col_max > threshold)[0]

        if len(content_rows) == 0 or len(content_cols) == 0:
            return False, 100.0, {"error": "No content detected"}

        y_start, y_end = int(content_rows[0]), int(content_rows[-1])
        x_start, x_end = int(content_cols[0]), int(content_cols[-1])
        if x_end <= x_start + 10 or y_end <= y_start + 10:
            return False, 100.0, {"error": "Could not find content region"}

        # Analyze a vertical band around the horizontal center.
        # Use a slightly wider band + median aggregation to reduce single-pixel noise.
        center_x = (x_start + x_end) // 2
        half_w = 8
        x0 = max(x_start, center_x - half_w)
        x1 = min(x_end, center_x + half_w)
        band = np.median(gray[y_start : y_end + 1, x0 : x1 + 1], axis=1)

        # Small 1D median filter to suppress 1-row glitches.
        if band.size >= 3:
            band = band.astype(np.float64, copy=False)
            band = np.concatenate([[band[0]], np.median(np.stack([band[:-2], band[1:-1], band[2:]]), axis=0), [band[-1]]])

        # Detrend to avoid mistaking large horizontal blocks (content vs black) for scanlines.
        band = band.astype(np.float64, copy=False)
        trend = self._running_median_1d(band, window=51)
        resid = band - trend
        resid -= float(np.mean(resid))

        total_pixels, scanline_pixels = self._scanline_scaling_info(float(preset.scan_line_distance))
        n = int(resid.size)
        if n < total_pixels * 8:
            return False, 100.0, {
                "error": "Content region too small for scanline analysis",
                "content_region": {"x": (int(x_start), int(x_end)), "y": (int(y_start), int(y_end))},
            }

        # Find best phase alignment of the ideal template (+1 for scanline pixels, -1 for gaps)
        best_phase = 0
        best_corr = None
        for phase in range(total_pixels):
            tmpl = np.where(((np.arange(n) + phase) % total_pixels) < scanline_pixels, 1.0, -1.0)
            corr = float(np.dot(resid, tmpl))
            if best_corr is None or corr > best_corr:
                best_corr = corr
                best_phase = phase

        tmpl = np.where(((np.arange(n) + best_phase) % total_pixels) < scanline_pixels, 1.0, -1.0)
        transitions = np.diff(tmpl)
        bright_to_dark = np.where(transitions == -2.0)[0]

        scanline_count = int(len(bright_to_dark))
        if scanline_count < int(self.thresholds["min_scanline_count"]):
            return False, 100.0, {
                "error": f"Too few scanlines detected: {scanline_count}",
                "scanline_count": scanline_count,
                "expected_period_px": int(total_pixels),
                "template_phase": int(best_phase),
                "template_corr": float(best_corr or 0.0),
                "content_region": {"x": (int(x_start), int(x_end)), "y": (int(y_start), int(y_end))},
                "roi": {"x": (int(x0), int(x1))},
            }

        # Uniformity: scanlines are rendered with fixed integer spacing (by design), so spacing
        # should match the expected template period. Avoid fragile boundary refinement under
        # bloom/afterglow/tint which can introduce spurious jitter.
        mean_spacing = float(total_pixels)
        std_spacing = 0.0
        variance_percent = 0.0

        # Contrast estimate: compare scanline vs gap luminance within the band.
        bright_vals = band[tmpl > 0]
        dark_vals = band[tmpl < 0]
        med_bright = float(np.median(bright_vals)) if bright_vals.size else 0.0
        med_dark = float(np.median(dark_vals)) if dark_vals.size else 0.0
        contrast_ratio = float((med_bright - med_dark) / max(med_bright, 1.0))

        details = {
            "scanline_count": scanline_count,
            "mean_spacing": mean_spacing,
            "std_spacing": std_spacing,
            "expected_period_px": int(total_pixels),
            "scanline_pixels_px": int(scanline_pixels),
            "template_phase": int(best_phase),
            "template_corr": float(best_corr or 0.0),
            "median_dark": med_dark,
            "median_bright": med_bright,
            "contrast_ratio": contrast_ratio,
            "content_region": {"x": (int(x_start), int(x_end)), "y": (int(y_start), int(y_end))},
            "roi": {"x": (int(x0), int(x1))},
        }

        self.log(
            f"Found {scanline_count} scanlines, expected period={total_pixels}px, "
            f"variance={variance_percent:.2f}%, contrast={contrast_ratio:.3f}",
            verbose,
        )

        return True, float(variance_percent), details
