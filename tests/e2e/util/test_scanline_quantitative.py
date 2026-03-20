#!/usr/bin/env python3
"""
C64 Stream - Unit tests for strict quantitative ScanlineAssertion validation.

These tests cover:
  - preserve-size checkpoint sampling (start/middle/end of each preserve section)
  - exact gap-group enforcement across all relevant frames
  - hard failure on the first violating frame
  - mandatory failure diagnostics
  - qualitative mode backward compatibility
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
from assertions.scanlines import ScanlineAssertion


def _default_preset() -> PresetConfig:
    return PresetConfig(name="Default", scan_line_distance=0.0, scan_line_strength=0.6)


def _classic_crt_preset() -> PresetConfig:
    return PresetConfig(name="Classic CRT", scan_line_distance=0.5, scan_line_strength=0.6)


def _make_group_count_frame(
    gap_groups: int,
    width: int = 1920,
    height: int = 1080,
    content_x: tuple[int, int] = (96, 1824),
    content_lum: int = 120,
) -> np.ndarray:
    """Create a frame with an exact number of black gap groups across full content height."""
    frame = np.zeros((height, width, 3), dtype=np.uint8)
    x0, x1 = content_x

    content_groups = gap_groups + 1
    black_rows = gap_groups
    content_rows = height - black_rows
    if content_rows < content_groups:
        raise ValueError("Not enough rows to allocate content bands")

    base_width = content_rows // content_groups
    remainder = content_rows % content_groups
    y = 0
    for index in range(content_groups):
        band_height = base_width + (1 if index < remainder else 0)
        frame[y : y + band_height, x0:x1, :] = content_lum
        y += band_height
        if index < gap_groups:
            y += 1

    return frame


def _frame_info(frame: np.ndarray, label: str, frame_index: int) -> dict[str, object]:
    return {
        "label": label,
        "frame_index": frame_index,
        "frame_time_offset_s": frame_index / 59.826,
        "frame": frame,
    }


class TestQuantitativePreserveWindowSampling(unittest.TestCase):
    def test_preserve_compare_samples_three_frames_per_section(self):
        assertion = ScanlineAssertion({"mode": "quantitative", "expected_gap_groups": 239})
        captured_indices: list[int] = []

        def _fake_extract(_mp4_path, frame_index, width, height):
            self.assertEqual((width, height), (1920, 1080))
            captured_indices.append(frame_index)
            return _make_group_count_frame(239)

        properties = {
            "script_file": "/tmp/preserve_size_compare.c64script",
            "video_format": "NTSC",
            "expected_width": 1920,
            "expected_height": 1080,
        }
        with patch.object(assertion, "_extract_frame_by_index", side_effect=_fake_extract):
            frames = assertion._iter_preserve_compare_frames(Path("/dev/null"), properties, verbose=False)

        self.assertEqual(
            captured_indices,
            [889, 894, 899, 910, 915, 920, 931, 936, 941, 952, 957, 963],
        )
        self.assertEqual(len(frames), 12)
        self.assertTrue(all(frame["frame_index"] not in (859, 879, 900, 921, 942) for frame in frames))


class TestQuantitativeExactGapGroups(unittest.TestCase):
    def _assertion(self, **extra_thresholds) -> ScanlineAssertion:
        thresholds = {
            "mode": "quantitative",
            "scan_line_distance_override": 0.5,
            "expected_gap_groups": 239,
        }
        thresholds.update(extra_thresholds)
        return ScanlineAssertion(thresholds)

    def test_all_relevant_frames_must_match_exact_group_count(self):
        assertion = self._assertion()
        frames = [
            _frame_info(_make_group_count_frame(239), f"section_{index}", 889 + index)
            for index in range(12)
        ]
        with patch.object(assertion, "_iter_quantitative_frames", return_value=frames):
            result = assertion.verify(Path("/dev/null"), {}, _classic_crt_preset(), verbose=False)

        self.assertEqual(result.status, AssertionStatus.PASS, result.message)
        self.assertEqual(len(result.details["validated_frames"]), 12)
        self.assertEqual(result.details["expected_gap_groups"], 239)

    def test_first_mismatching_frame_fails_immediately(self):
        assertion = self._assertion()
        frames = [
            _frame_info(_make_group_count_frame(239), "classic:start", 889),
            _frame_info(_make_group_count_frame(239), "classic:middle", 894),
            _frame_info(_make_group_count_frame(80), "sharp:start", 910),
            _frame_info(_make_group_count_frame(239), "sharp:middle", 915),
        ]
        with patch.object(assertion, "_iter_quantitative_frames", return_value=frames):
            result = assertion.verify(Path("/dev/null"), {}, _classic_crt_preset(), verbose=False)

        self.assertEqual(result.status, AssertionStatus.FAIL)
        self.assertIn("Frame 910", result.message)
        self.assertIn("observed=80", result.message)
        self.assertIn("expected=239", result.message)
        self.assertEqual(result.details["frame_index"], 910)
        self.assertEqual(result.details["observed_gap_groups"], 80)
        self.assertEqual(result.details["expected_gap_groups"], 239)
        self.assertEqual(len(result.details["first_20_classifications"]), 20)
        self.assertIn("groups=80", result.details["pattern_summary"])

    def test_gap_group_tolerance_is_ignored(self):
        assertion = self._assertion(max_gap_group_deviation=999)
        frames = [_frame_info(_make_group_count_frame(238), "classic:start", 889)]
        with patch.object(assertion, "_iter_quantitative_frames", return_value=frames):
            result = assertion.verify(Path("/dev/null"), {}, _classic_crt_preset(), verbose=False)

        self.assertEqual(result.status, AssertionStatus.FAIL)
        self.assertIn("observed=238", result.message)
        self.assertIn("expected=239", result.message)


class TestQuantitativeFallbackAndErrors(unittest.TestCase):
    def test_missing_distance_fails(self):
        assertion = ScanlineAssertion({"mode": "quantitative"})
        result = assertion.verify(Path("/dev/null"), {}, _default_preset(), verbose=False)
        self.assertEqual(result.status, AssertionStatus.FAIL)
        self.assertIn("scan_line_distance", result.message)

    def test_no_frames_skips(self):
        assertion = ScanlineAssertion(
            {"mode": "quantitative", "scan_line_distance_override": 0.5, "expected_gap_groups": 239}
        )
        with patch.object(assertion, "_iter_quantitative_frames", return_value=[]):
            result = assertion.verify(Path("/dev/null"), {}, _classic_crt_preset(), verbose=False)
        self.assertEqual(result.status, AssertionStatus.SKIP)

    def test_best_frame_selector_removed(self):
        self.assertFalse(hasattr(ScanlineAssertion, "_scanline_group_count"))
        self.assertFalse(hasattr(ScanlineAssertion, "_extract_quantitative_frame"))


class TestQualitativeBackwardCompat(unittest.TestCase):
    def test_default_mode_skip_for_non_scanline_preset(self):
        assertion = ScanlineAssertion()
        result = assertion.verify(Path("/dev/null"), {}, _default_preset(), verbose=False)
        self.assertEqual(result.status, AssertionStatus.SKIP)
        self.assertIn("not enabled", result.message)


if __name__ == "__main__":
    unittest.main()
