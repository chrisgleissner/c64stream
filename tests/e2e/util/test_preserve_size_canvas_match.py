#!/usr/bin/env python3
"""
C64 Stream - Unit tests for PreserveSizeCanvasMatch assertion.

These tests cover:
  - Frame index computation from script timing
  - Source screenshot dimension validation
  - Canvas frame geometry (letterbox) validation
  - Regression detection: wrong preserve-size gives different canvas geometry

Tests are designed to fail when the assertion logic is missing or broken.
They operate on synthetic in-memory data (no live OBS or real MP4 required).
"""

from __future__ import annotations

import importlib
import io
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


def _load_canvas_match():
    """Load the preserve_size_canvas_match assertion module via its package."""
    e2e_dir = Path(__file__).resolve().parents[1]
    if str(e2e_dir) not in sys.path:
        sys.path.insert(0, str(e2e_dir))
    # Fresh import so tests can be run standalone as well as via unittest discovery.
    import importlib as _il

    mod = _il.import_module("assertions.preserve_size_canvas_match")
    return mod


def _write_png(path: Path, width: int, height: int, rgb: tuple[int, int, int]) -> None:
    """Write a minimal solid-colour PNG for testing."""
    # PNG signature
    sig = b"\x89PNG\r\n\x1a\n"

    def chunk(name: bytes, data: bytes) -> bytes:
        length = struct.pack(">I", len(data))
        crc = struct.pack(">I", zlib.crc32(name + data) & 0xFFFFFFFF)
        return length + name + data + crc

    ihdr_data = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    ihdr = chunk(b"IHDR", ihdr_data)

    raw_rows = []
    row_bytes = bytes([rgb[0], rgb[1], rgb[2]] * width)
    for _ in range(height):
        raw_rows.append(b"\x00" + row_bytes)
    raw = b"".join(raw_rows)
    compressed = zlib.compress(raw)
    idat = chunk(b"IDAT", compressed)
    iend = chunk(b"IEND", b"")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(sig + ihdr + idat + iend)


def _write_ntsc_logical_png(path: Path) -> None:
    """Write a 384×240 source screenshot (correct NTSC logical size, preserve_size=1)."""
    _write_png(path, 384, 240, (100, 80, 200))


def _write_ntsc_virtual_crt_png(path: Path) -> None:
    """Write a 384×720 source screenshot (wrong – virtual CRT size, preserve_size=0 bug)."""
    _write_png(path, 384, 720, (100, 80, 200))


def _make_canvas_array(
    width: int,
    height: int,
    content_x0: int,
    content_x1: int,
    content_color: tuple[int, int, int] = (100, 80, 200),
) -> "np.ndarray":  # type: ignore[name-defined]
    """
    Create a synthetic canvas frame (H×W×3 uint8 numpy array).

    Pixels inside [content_x0, content_x1) get content_color; everything else
    is black (simulating OBS letterbox bars).

    Returns numpy array with shape (height, width, 3).
    """
    import numpy as np

    frame = np.zeros((height, width, 3), dtype=np.uint8)
    frame[:, content_x0:content_x1] = content_color
    return frame


class TestFrameIndexComputation(unittest.TestCase):
    """Verify that checkpoint frame indices are computed correctly from FPS and script timing."""

    def setUp(self):
        self.mod = _load_canvas_match()

    def test_ntsc_fps_constant(self):
        """NTSC FPS must be 29913/500 = 59.826."""
        fps = self.mod.NTSC_FPS
        self.assertAlmostEqual(fps, 29913 / 500, places=5)

    def test_ntsc_initial_wait_frame(self):
        """After WAIT 14s at NTSC fps the base frame should be floor(14 * fps)."""
        fps = self.mod.NTSC_FPS
        expected = int(14 * fps)  # 837
        self.assertEqual(expected, self.mod.compute_base_frame(wait_seconds=14, fps=fps))

    def test_checkpoint_default_preserve_frame_ntsc(self):
        """default_source_preserve frame index must be deterministic."""
        indices = self.mod.compute_checkpoint_frames(fps=self.mod.NTSC_FPS)
        # base=837, +12 preflight, +10 default legacy, +10 default preserve = 837+32=869
        self.assertEqual(indices["default_preserve"], 837 + 12 + 10 + 10)

    def test_checkpoint_classic_preserve_frame_ntsc(self):
        """classic_source_preserve frame index is base + preflight + default + classic."""
        indices = self.mod.compute_checkpoint_frames(fps=self.mod.NTSC_FPS)
        # 837 + 12 + 20 + 10 + 10 = 889
        self.assertEqual(indices["classic_preserve"], 837 + 12 + 20 + 10 + 10)

    def test_checkpoint_classic_preserve_2_frame_ntsc(self):
        """classic_source_preserve_2 is one frame after classic_source_preserve."""
        indices = self.mod.compute_checkpoint_frames(fps=self.mod.NTSC_FPS)
        self.assertEqual(indices["classic_preserve_2"], indices["classic_preserve"] + 1)

    def test_checkpoint_arcade_preserve_frame_ntsc(self):
        """arcade_source_preserve is the last preserve checkpoint."""
        indices = self.mod.compute_checkpoint_frames(fps=self.mod.NTSC_FPS)
        # Verify it comes after all other checkpoints
        self.assertGreater(indices["arcade_preserve"], indices["vintage_preserve"])
        self.assertGreater(indices["arcade_preserve"], indices["sharp_preserve"])

    def test_all_checkpoint_keys_present(self):
        """All expected checkpoint keys must be present."""
        indices = self.mod.compute_checkpoint_frames(fps=self.mod.NTSC_FPS)
        required = {
            "default_preserve",
            "classic_preserve",
            "classic_preserve_2",
            "sharp_preserve",
            "sharp_preserve_2",
            "vintage_preserve",
            "vintage_preserve_2",
            "arcade_preserve",
            "arcade_preserve_2",
        }
        for key in required:
            with self.subTest(key=key):
                self.assertIn(key, indices, msg=f"Missing checkpoint frame key: {key!r}")


class TestSourceDimensionValidation(unittest.TestCase):
    """Verify that source screenshot dimension checks catch preserve-size bugs."""

    def setUp(self):
        self.mod = _load_canvas_match()

    def test_correct_ntsc_logical_size_passes(self):
        """Source screenshot at 384×240 (NTSC logical) must pass validation."""
        with tempfile.TemporaryDirectory() as td:
            png_path = Path(td) / "source.png"
            _write_ntsc_logical_png(png_path)
            result = self.mod.validate_source_dimensions(
                png_path, expected_w=384, expected_h=240
            )
            if result.status == "skip":
                self.skipTest(result.message)
            self.assertTrue(result.ok, f"Expected pass but got: {result.message}")

    def test_virtual_crt_size_fails(self):
        """Source screenshot at 384×720 (virtual CRT, broken preserve_size=0) must fail."""
        with tempfile.TemporaryDirectory() as td:
            png_path = Path(td) / "source.png"
            _write_ntsc_virtual_crt_png(png_path)
            result = self.mod.validate_source_dimensions(
                png_path, expected_w=384, expected_h=240
            )
            if result.status == "skip":
                self.skipTest(result.message)
            self.assertFalse(result.ok, "Expected FAIL for wrong source dimensions")
            self.assertIn("384", result.message)
            self.assertIn("720", result.message)

    def test_missing_file_fails(self):
        """Missing source screenshot must produce a clear failure message."""
        result = self.mod.validate_source_dimensions(
            Path("/nonexistent/source.png"), expected_w=384, expected_h=240
        )
        self.assertFalse(result.ok)
        self.assertIn("not found", result.message.lower())

    def test_failure_message_contains_effect_name(self):
        """Failure message for wrong dims must identify the checkpoint/effect."""
        with tempfile.TemporaryDirectory() as td:
            png_path = Path(td) / "source.png"
            _write_ntsc_virtual_crt_png(png_path)
            result = self.mod.validate_source_dimensions(
                png_path,
                expected_w=384,
                expected_h=240,
                checkpoint_name="classic_preserve",
            )
            if result.status == "skip":
                self.skipTest(result.message)
            self.assertIn("classic_preserve", result.message)


class TestCanvasGeometryValidation(unittest.TestCase):
    """Verify canvas letterbox geometry checks catch preserve-size canvas regressions."""

    def setUp(self):
        self.mod = _load_canvas_match()

    def _correct_canvas(self) -> "np.ndarray":  # type: ignore[name-defined]
        """Canvas showing preserve_size=1 NTSC logical source (384×240) at INNER scale 4.5."""
        # Content width = floor(384 * 4.5) = 1728, x_offset = (1920-1728)//2 = 96
        return _make_canvas_array(
            width=1920,
            height=1080,
            content_x0=96,
            content_x1=1824,
            content_color=(100, 80, 200),
        )

    def _bug_canvas(self) -> "np.ndarray":  # type: ignore[name-defined]
        """Canvas showing buggy preserve_size=0 CRT virtual source (384×720) at INNER scale 1.5."""
        # scale = min(1920/384, 1080/720) = 1.5
        # content_w = floor(384 * 1.5) = 576, x_offset = (1920-576)//2 = 672
        return _make_canvas_array(
            width=1920,
            height=1080,
            content_x0=672,
            content_x1=1248,
            content_color=(100, 80, 200),
        )

    def test_correct_canvas_passes(self):
        """Canvas with correct NTSC preserve geometry must pass the letterbox check."""
        frame = self._correct_canvas()
        result = self.mod.validate_canvas_letterbox(
            frame=frame,
            canvas_w=1920,
            canvas_h=1080,
            source_w=384,
            source_h=240,
            checkpoint_name="default_preserve",
        )
        self.assertTrue(result.ok, f"Expected pass but got: {result.message}")

    def test_bug_canvas_fails(self):
        """Canvas with wrong (virtual CRT) geometry must fail the letterbox check.

        This is the primary regression: preserve_size=1 supposed to give 384×240
        logical source, but canvas shows 384×720 virtual scaling instead.
        """
        frame = self._bug_canvas()
        result = self.mod.validate_canvas_letterbox(
            frame=frame,
            canvas_w=1920,
            canvas_h=1080,
            source_w=384,
            source_h=240,
            checkpoint_name="classic_preserve",
        )
        self.assertFalse(result.ok, "Expected FAIL for canvas with bug geometry")

    def test_bug_canvas_failure_message_is_diagnostic(self):
        """Failure message must identify effect, expected/actual info."""
        frame = self._bug_canvas()
        result = self.mod.validate_canvas_letterbox(
            frame=frame,
            canvas_w=1920,
            canvas_h=1080,
            source_w=384,
            source_h=240,
            checkpoint_name="classic_preserve",
        )
        self.assertIn("classic_preserve", result.message)

    def test_all_black_canvas_fails_content_check(self):
        """A completely black canvas (no content) must fail because content area is empty."""
        import numpy as np

        frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
        result = self.mod.validate_canvas_letterbox(
            frame=frame,
            canvas_w=1920,
            canvas_h=1080,
            source_w=384,
            source_h=240,
            checkpoint_name="default_preserve",
        )
        self.assertFalse(result.ok, "All-black canvas should fail (no visible content)")

    def test_canvas_with_correct_black_bars(self):
        """Pixels in expected letterbox region MUST be exactly black (R=G=B=0)."""
        import numpy as np

        frame = self._correct_canvas()
        # Verify test data is correct for this sub-test
        # Left bar (x=[0,95]) should be black
        left_bar = frame[:, 0:96, :]
        self.assertTrue(np.all(left_bar == 0), "Synthetic left bar should be black")
        # Content area (x=[96,1824]) should be non-black
        content = frame[:, 96:1824, :]
        self.assertFalse(np.all(content == 0), "Synthetic content should be non-black")

    def test_expected_letterbox_geometry_ntsc_logical(self):
        """compute_inner_scale_geometry must return correct values for NTSC preserve_size=1."""
        geom = self.mod.compute_inner_scale_geometry(
            canvas_w=1920, canvas_h=1080, source_w=384, source_h=240
        )
        # scale = min(1920/384, 1080/240) = min(5.0, 4.5) = 4.5
        self.assertAlmostEqual(geom.scale, 4.5, places=5)
        self.assertEqual(geom.content_w, 1728)
        self.assertEqual(geom.content_h, 1080)
        self.assertEqual(geom.x_offset, 96)
        self.assertEqual(geom.y_offset, 0)

    def test_expected_letterbox_geometry_ntsc_virtual_crt(self):
        """compute_inner_scale_geometry for virtual CRT (384×720) gives narrow content."""
        geom = self.mod.compute_inner_scale_geometry(
            canvas_w=1920, canvas_h=1080, source_w=384, source_h=720
        )
        # scale = min(1920/384, 1080/720) = min(5.0, 1.5) = 1.5
        self.assertAlmostEqual(geom.scale, 1.5, places=5)
        self.assertEqual(geom.content_w, 576)
        self.assertEqual(geom.content_h, 1080)
        self.assertEqual(geom.x_offset, 672)

    def test_black_bar_check_with_non_black_pixels_fails(self):
        """If expected-black region has non-black pixels the check must fail."""
        import numpy as np

        # Build a canvas where the "left black bar" (x<96) contains a bright pixel
        frame = self._correct_canvas()
        frame[540, 50] = [255, 0, 0]  # Red pixel in expected black region
        result = self.mod.validate_canvas_letterbox(
            frame=frame,
            canvas_w=1920,
            canvas_h=1080,
            source_w=384,
            source_h=240,
            checkpoint_name="default_preserve",
        )
        self.assertFalse(result.ok, "Non-black pixel in letterbox bar should cause failure")


class TestCanvasDimensionCheck(unittest.TestCase):
    """verify_canvas_frame_dimensions must detect wrong-size frames immediately."""

    def setUp(self):
        self.mod = _load_canvas_match()

    def test_correct_dimensions_pass(self):
        import numpy as np

        frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
        result = self.mod.verify_canvas_frame_dimensions(
            frame=frame, expected_w=1920, expected_h=1080, checkpoint_name="test"
        )
        self.assertTrue(result.ok, result.message)

    def test_wrong_height_fails(self):
        import numpy as np

        frame = np.zeros((720, 1920, 3), dtype=np.uint8)
        result = self.mod.verify_canvas_frame_dimensions(
            frame=frame, expected_w=1920, expected_h=1080, checkpoint_name="test"
        )
        self.assertFalse(result.ok)
        self.assertIn("720", result.message)
        self.assertIn("1080", result.message)

    def test_wrong_width_fails(self):
        import numpy as np

        frame = np.zeros((1080, 1280, 3), dtype=np.uint8)
        result = self.mod.verify_canvas_frame_dimensions(
            frame=frame, expected_w=1920, expected_h=1080, checkpoint_name="test"
        )
        self.assertFalse(result.ok)
        self.assertIn("1280", result.message)
        self.assertIn("1920", result.message)


class TestMp4FrameExtraction(unittest.TestCase):
    """validate_canvas_frame_at_index must skip gracefully when cv2/MP4 is unavailable."""

    def setUp(self):
        self.mod = _load_canvas_match()

    def test_missing_mp4_returns_skip(self):
        """If the MP4 file does not exist, fall back to skip (not crash)."""
        result = self.mod.extract_and_validate_canvas_frame(
            mp4_path=Path("/nonexistent/recording.mp4"),
            frame_index=100,
            canvas_w=1920,
            canvas_h=1080,
            source_w=384,
            source_h=240,
            checkpoint_name="default_preserve",
            frame_dir=Path("/tmp"),
        )
        # Must not raise; allowed to skip or fail with a meaningful message
        self.assertIn(result.status, ("skip", "fail"))


if __name__ == "__main__":
    unittest.main()
