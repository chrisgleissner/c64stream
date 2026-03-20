#!/usr/bin/env python3
"""
C64 Stream - Unit tests for ScanlineAssertion quantitative mode.

Tests cover:
  - Correct periodic scanlines → PASS
  - Collapsed scanlines (no BLACK rows) → FAIL
  - Insufficient black bands → FAIL
  - Inconsistent spacing → FAIL
  - Uneven distribution → FAIL
  - Qualitative mode backward compatibility → SKIP (Default preset)

Operates on synthetic in-memory frames (no live OBS or real MP4 required).
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

_E2E_DIR = Path(__file__).resolve().parents[1]
if str(_E2E_DIR) not in sys.path:
    sys.path.insert(0, str(_E2E_DIR))

from assertions.base import AssertionStatus
from assertions.config import PresetConfig
from assertions.scanlines import ScanlineAssertion, _BLACK_THRESHOLD


def _default_preset() -> PresetConfig:
    """Default preset: no scanlines."""
    return PresetConfig(name="Default", scan_line_distance=0.0, scan_line_strength=0.6)


def _classic_crt_preset() -> PresetConfig:
    """Classic CRT: scan_line_distance=0.5 → period=3, content=2, gap=1."""
    return PresetConfig(name="Classic CRT", scan_line_distance=0.5, scan_line_strength=0.6)


def _make_frame_with_scanlines(
    width: int = 1920,
    height: int = 1080,
    content_y: tuple[int, int] = (0, 1080),
    content_x: tuple[int, int] = (96, 1824),
    period: int = 14,
    gap_width: int = 4,
    content_lum: int = 120,
) -> np.ndarray:
    """Create a synthetic canvas frame with periodic scanline gaps.

    Within the content region, every `period` rows, `gap_width` rows are
    BLACK; the rest are CONTENT with uniform luminance.
    Outside the content region (letterbox bars), all pixels are black.
    """
    frame = np.zeros((height, width, 3), dtype=np.uint8)
    y0, y1 = content_y
    x0, x1 = content_x

    for y in range(y0, y1):
        row_in_content = y - y0
        phase = row_in_content % period
        if phase < (period - gap_width):
            # CONTENT row
            frame[y, x0:x1, :] = content_lum
        # else: BLACK row (remains 0)
    return frame


def _make_frame_no_scanlines(
    width: int = 1920,
    height: int = 1080,
    content_y: tuple[int, int] = (0, 1080),
    content_x: tuple[int, int] = (96, 1824),
    content_lum: int = 120,
) -> np.ndarray:
    """Create a frame with uniform content (no scanline gaps)."""
    frame = np.zeros((height, width, 3), dtype=np.uint8)
    y0, y1 = content_y
    x0, x1 = content_x
    frame[y0:y1, x0:x1, :] = content_lum
    return frame


class TestQuantitativePassCorrectPattern(unittest.TestCase):
    """A frame with correct periodic scanlines should PASS."""

    def test_uniform_scanlines_pass(self):
        frame = _make_frame_with_scanlines(
            period=14, gap_width=4, content_lum=120,
        )
        assertion = ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
        })
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.PASS, result.message)
        self.assertIn("topology correct", result.message)
        self.assertIn("detected_period", result.details)
        self.assertEqual(result.details["detected_period"], 14)

    def test_small_period_pass(self):
        """Period=5, gap_width=1 — like a fine scanline at 4.5× scaling."""
        frame = _make_frame_with_scanlines(period=5, gap_width=1, content_lum=80)
        assertion = ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
        })
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 18.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.PASS, result.message)


class TestQuantitativeFailCollapsed(unittest.TestCase):
    """A frame with no scanline gaps should FAIL."""

    def test_no_black_rows_fail(self):
        frame = _make_frame_no_scanlines()
        assertion = ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
        })
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.FAIL)
        self.assertIn("collapsed or missing", result.message)
        self.assertEqual(result.details["observed_black_rows"], 0)


class TestQuantitativeFailTooFewBands(unittest.TestCase):
    """Only 1 or 2 black bands → FAIL (insufficient for periodicity check)."""

    def test_single_black_band_fail(self):
        frame = _make_frame_no_scanlines()
        # Insert a single black band
        frame[500:510, 96:1824, :] = 0
        assertion = ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
        })
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.FAIL)
        self.assertIn("insufficient", result.message.lower())


class TestQuantitativeFailInconsistentSpacing(unittest.TestCase):
    """Gaps with wildly different periods → FAIL."""

    def test_mixed_periods_fail(self):
        frame = _make_frame_no_scanlines(content_lum=120)
        # Create irregular black bands: at rows 100, 120, 180 (gaps: 20, 60 — differ by >1)
        for start in [100, 120, 180, 250, 360]:
            frame[start : start + 3, 96:1824, :] = 0
        assertion = ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
        })
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.FAIL)
        self.assertIn("inconsistent", result.message.lower())


class TestQuantitativeFailUnevenDistribution(unittest.TestCase):
    """Scanlines present only in top half → FAIL on distribution check."""

    def test_top_only_scanlines_fail(self):
        frame = _make_frame_no_scanlines(content_lum=120)
        # Add scanline pattern only in top 30% of content (rows 0-324 of 1080)
        for y in range(0, 324):
            if y % 14 >= 10:  # gap at rows 10-13 of each period
                frame[y, 96:1824, :] = 0
        assertion = ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
        })
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.FAIL)
        msg = result.message.lower()
        self.assertTrue(
            "distribution" in msg or "uneven" in msg or "count" in msg,
            f"Expected distribution/count failure, got: {result.message}",
        )


class TestQuantitativeNoScanDistance(unittest.TestCase):
    """Quantitative mode without scan_line_distance → FAIL."""

    def test_missing_distance_fail(self):
        assertion = ScanlineAssertion({"mode": "quantitative"})
        result = assertion.verify(
            mp4_path=Path("/dev/null"),
            properties={},
            preset=_default_preset(),
            verbose=False,
        )
        self.assertEqual(result.status, AssertionStatus.FAIL)
        self.assertIn("scan_line_distance", result.message)


class TestQualitativeBackwardCompat(unittest.TestCase):
    """Default (qualitative) mode should SKIP for Default preset."""

    def test_default_mode_skip(self):
        assertion = ScanlineAssertion()
        result = assertion.verify(
            mp4_path=Path("/dev/null"),
            properties={},
            preset=_default_preset(),
            verbose=False,
        )
        self.assertEqual(result.status, AssertionStatus.SKIP)
        self.assertIn("not enabled", result.message)

    def test_explicit_qualitative_skip(self):
        assertion = ScanlineAssertion({"mode": "qualitative"})
        result = assertion.verify(
            mp4_path=Path("/dev/null"),
            properties={},
            preset=_default_preset(),
            verbose=False,
        )
        self.assertEqual(result.status, AssertionStatus.SKIP)


class TestQuantitativeNoFrame(unittest.TestCase):
    """When quantitative mode can't extract a frame → SKIP."""

    def test_no_frame_skip(self):
        assertion = ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
        })
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(None, 0.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.SKIP)


class TestScanlineGroupCount(unittest.TestCase):
    """Verify _scanline_group_count picks scanlined frames over plain ones."""

    def test_scanlined_frame_scores_higher(self):
        plain = _make_frame_no_scanlines()
        scanlined = _make_frame_with_scanlines(period=14, gap_width=4)
        score_plain = ScanlineAssertion._scanline_group_count(plain)
        score_scanlined = ScanlineAssertion._scanline_group_count(scanlined)
        self.assertGreater(score_scanlined, score_plain)

    def test_black_frame_scores_zero(self):
        frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
        score = ScanlineAssertion._scanline_group_count(frame)
        self.assertEqual(score, 0)


class TestQuantitativeRoundingTolerance(unittest.TestCase):
    """Periods that alternate ±1 px (non-integer OBS scaling) should still PASS."""

    def test_alternating_period_pass(self):
        frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
        x0, x1 = 96, 1824
        content_lum = 120
        # Build a pattern where period alternates between 13 and 14
        y = 0
        period_idx = 0
        while y < 1080:
            period = 13 if period_idx % 2 == 0 else 14
            gap_width = 4
            content_rows = period - gap_width
            for r in range(content_rows):
                if y + r < 1080:
                    frame[y + r, x0:x1, :] = content_lum
            for r in range(gap_width):
                if y + content_rows + r < 1080:
                    frame[y + content_rows + r, x0:x1, :] = 0
            y += period
            period_idx += 1

        assertion = ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
            "max_gap_deviation_px": 1,
        })
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.PASS, result.message)


class TestQuantitativeExpectedGapGroups(unittest.TestCase):
    """Validate the expected_gap_groups threshold."""

    def _make_assertion(self, expected_groups, max_deviation=2):
        return ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
            "expected_gap_groups": expected_groups,
            "max_gap_group_deviation": max_deviation,
        })

    def test_exact_match_pass(self):
        """Frame with exactly the expected number of gap groups → PASS."""
        # period=14, gap=4: content region 1080 rows → 1080/14 ≈ 77 groups
        frame = _make_frame_with_scanlines(period=14, gap_width=4)
        # Count actual groups to set expected
        groups = ScanlineAssertion._scanline_group_count(frame)
        assertion = self._make_assertion(expected_groups=groups)
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.PASS, result.message)

    def test_within_tolerance_pass(self):
        """Gap groups within ±deviation → PASS."""
        frame = _make_frame_with_scanlines(period=14, gap_width=4)
        groups = ScanlineAssertion._scanline_group_count(frame)
        assertion = self._make_assertion(expected_groups=groups + 1, max_deviation=2)
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.PASS, result.message)

    def test_too_few_groups_fail(self):
        """Far fewer gap groups than expected → FAIL."""
        frame = _make_frame_with_scanlines(period=14, gap_width=4)
        assertion = self._make_assertion(expected_groups=239, max_deviation=2)
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.FAIL)
        self.assertIn("gap group count mismatch", result.message.lower())

    def test_too_many_groups_fail(self):
        """Far more gap groups than expected → FAIL."""
        frame = _make_frame_with_scanlines(period=5, gap_width=1)
        assertion = self._make_assertion(expected_groups=10, max_deviation=2)
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.FAIL)
        self.assertIn("gap group count mismatch", result.message.lower())


class TestQuantitativeGapWidthUniformity(unittest.TestCase):
    """Validate that non-uniform gap widths are caught."""

    def test_uniform_widths_pass(self):
        """All gap bands same width → PASS."""
        frame = _make_frame_with_scanlines(period=14, gap_width=4)
        assertion = ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
        })
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        self.assertEqual(result.status, AssertionStatus.PASS, result.message)

    def test_mixed_gap_widths_fail(self):
        """Gap bands with wildly different widths → FAIL."""
        frame = _make_frame_no_scanlines(content_lum=120)
        # Create gap bands with inconsistent widths: 3, 3, 3, 8, 3
        y = 0
        gap_widths = [3, 3, 3, 8, 3, 3, 3, 3, 8, 3, 3, 3, 3, 8]
        for gw in gap_widths:
            y += 10  # content rows
            for r in range(gw):
                if y + r < 1080:
                    frame[y + r, 96:1824, :] = 0
            y += gw
        assertion = ScanlineAssertion({
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
        })
        with patch.object(assertion, "_extract_quantitative_frame", return_value=(frame, 16.0)):
            result = assertion.verify(
                mp4_path=Path("/dev/null"),
                properties={},
                preset=_classic_crt_preset(),
                verbose=False,
            )
        # Should fail on either gap width uniformity or spacing inconsistency
        self.assertEqual(result.status, AssertionStatus.FAIL, result.message)


if __name__ == "__main__":
    unittest.main()
