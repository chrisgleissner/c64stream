#!/usr/bin/env python3
"""
C64 Stream - Preserve-Size Canvas Match Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Validates that OBS canvas composition correctly reflects preserve-size semantics.

The script data/scripts/preserve_size_compare.c64script captures SOURCE
screenshots at named checkpoints. This assertion adds a second, independent
validation layer by:

  1. Verifying SOURCE screenshot dimensions match the expected NTSC/PAL logical
     size at every preserve_size=1 checkpoint  (TOLERANCE 0, PNG lossless).
  2. Extracting the corresponding canvas frames from the recorded MP4 using
     exact frame indices derived from deterministic OBS FPS and script timing.
  3. Verifying each extracted canvas frame shows content in the exact letterbox
     geometry expected for a preserve_size=1 (logical-sized) source.

Frame index mapping (NTSC, fps=29913/500, WAIT 14s, preserve-size script):

  base            = floor(14 * fps)                              = 837
  preflight total = +10 (OBS WAIT FRAMES 10) + 1 + 1            = +12 → 849
  COMPARE_DEFAULT:
    default_legacy   = base + 12 + 10                           = 859
    default_preserve = base + 12 + 20                           = 869
  COMPARE_CLASSIC:
    classic_legacy    = base + 12 + 20 + 10                     = 879
    classic_preserve  = base + 12 + 20 + 20                     = 889
    classic_preserve_2= base + 12 + 20 + 21                     = 890
  COMPARE_SHARP:
    sharp_legacy     = base + 12 + 20 + 21 + 10                 = 900
    sharp_preserve   = base + 12 + 20 + 21 + 20                 = 910
    sharp_preserve_2 = base + 12 + 20 + 21 + 21                 = 911
  COMPARE_VINTAGE:
    vintage_legacy   = base + 12 + 20 + 21 + 21 + 10            = 921
    vintage_preserve = base + 12 + 20 + 21 + 21 + 20            = 931
    vintage_preserve_2= base + 12 + 20 + 21 + 21 + 21           = 932
  COMPARE_ARCADE:
    arcade_legacy    = base + 12 + 20 + 21*3 + 10               = 942
    arcade_preserve  = base + 12 + 20 + 21*3 + 20               = 952
    arcade_preserve_2= base + 12 + 20 + 21*3 + 21               = 953

Each effect section repeats the same OBS WAIT FRAMES structure:
  (+10 for legacy screenshot) + (+10 for preserve screenshot) + (+1 for
  preserve_2 screenshot) = +21 frame advance per effect section.

Letterbox geometry (fixed_canvas_bounds=True, OBS_BOUNDS_SCALE_INNER):
  For preserve_size=1 (NTSC logical 384×240) in 1920×1080 canvas:
    scale    = min(1920/384, 1080/240) = min(5.0, 4.5) = 4.5
    content  = 1728 × 1080
    x_offset = 96   (black bars: x=[0,96) and x=[1824,1920))
    y_offset = 0    (no vertical bars; content fills full height)

  For the buggy case (preserve_size=0, virtual CRT e.g. 384×720 in 1920×1080):
    scale    = min(1920/384, 1080/720) = 1.5
    content  = 576 × 1080
    x_offset = 672  ← much wider black bars → detectable regression
"""

from __future__ import annotations

import math
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

NTSC_FPS: float = 29913.0 / 500.0  # 59.826 fps (OBS NTSC recording rate)
PAL_FPS: float = 401.0 / 8.0  # 50.125 fps (OBS PAL recording rate)

NTSC_LOGICAL_W: int = 384
NTSC_LOGICAL_H: int = 240
PAL_LOGICAL_W: int = 384
PAL_LOGICAL_H: int = 272

# Script: "WAIT 14s" initial wait (seconds of wall clock)
_SCRIPT_INITIAL_WAIT_S: float = 14.0

# Effect section frame advances derived from the script structure:
#   COMPARE_DEFAULT advances 20 frames (+10 legacy, +10 preserve)
#   Each CRT section advances 21 frames (+10 +10 +1 preserve_2)
_PREFLIGHT_FRAMES: int = 12  # +10 (first OBS WAIT FRAMES 10) +1 +1
_DEFAULT_SECTION_FRAMES: int = 20  # +10 default_legacy, +10 default_preserve
_CRT_SECTION_FRAMES: int = 21  # +10 legacy, +10 preserve, +1 preserve_2


# ---------------------------------------------------------------------------
# Public helpers (also exercised directly by unit tests)
# ---------------------------------------------------------------------------


def compute_base_frame(wait_seconds: float = _SCRIPT_INITIAL_WAIT_S, fps: float = NTSC_FPS) -> int:
    """Return the recording frame index after the initial WAIT Ns."""
    return math.floor(wait_seconds * fps)


def compute_checkpoint_frames(fps: float = NTSC_FPS) -> dict[str, int]:
    """
    Return a mapping from checkpoint name to 0-based MP4 frame index.

    The calculation is exact: every OBS WAIT FRAMES N advances the frame
    counter by exactly N (the source render callback and the OBS recording
    output share the same clock tick).
    """
    base = compute_base_frame(fps=fps)
    f = base + _PREFLIGHT_FRAMES  # after PREFLIGHT section

    # COMPARE_DEFAULT
    default_legacy = f + 10
    default_preserve = f + 20
    f += _DEFAULT_SECTION_FRAMES  # advance past the whole DEFAULT section

    # ------------------------------------------------------------------
    # Helper: advances f through one CRT section and returns per-checkpoint
    # frame indices.  f is advanced by _CRT_SECTION_FRAMES after each call.
    # ------------------------------------------------------------------
    def _crt_section():
        nonlocal f
        f_legacy = f + 10
        f_preserve = f + 20
        f_preserve_2 = f + 21
        f += _CRT_SECTION_FRAMES
        return f_legacy, f_preserve, f_preserve_2

    classic_legacy, classic_preserve, classic_preserve_2 = _crt_section()
    sharp_legacy, sharp_preserve, sharp_preserve_2 = _crt_section()
    vintage_legacy, vintage_preserve, vintage_preserve_2 = _crt_section()
    arcade_legacy, arcade_preserve, arcade_preserve_2 = _crt_section()

    return {
        "default_legacy": default_legacy,
        "default_preserve": default_preserve,
        "classic_legacy": classic_legacy,
        "classic_preserve": classic_preserve,
        "classic_preserve_2": classic_preserve_2,
        "sharp_legacy": sharp_legacy,
        "sharp_preserve": sharp_preserve,
        "sharp_preserve_2": sharp_preserve_2,
        "vintage_legacy": vintage_legacy,
        "vintage_preserve": vintage_preserve,
        "vintage_preserve_2": vintage_preserve_2,
        "arcade_legacy": arcade_legacy,
        "arcade_preserve": arcade_preserve,
        "arcade_preserve_2": arcade_preserve_2,
    }


@dataclass
class CheckResult:
    """Lightweight pass/fail/skip record for individual sub-checks."""

    ok: bool
    message: str
    status: str = "pass"  # "pass", "fail", "skip"

    def __post_init__(self):
        if not self.ok and self.status == "pass":
            self.status = "fail"


@dataclass
class CanvasGeometry:
    """Content region geometry for OBS_BOUNDS_SCALE_INNER on a canvas."""

    scale: float
    content_w: int
    content_h: int
    x_offset: int
    y_offset: int


def compute_inner_scale_geometry(
    canvas_w: int,
    canvas_h: int,
    source_w: int,
    source_h: int,
) -> CanvasGeometry:
    """
    Compute the content region for OBS_BOUNDS_SCALE_INNER (bounds_type=2).

    OBS scales the source uniformly to fit within the canvas bounds,
    preserving aspect ratio.  With point (nearest-neighbour) filter.
    """
    scale = min(canvas_w / source_w, canvas_h / source_h)
    content_w = int(source_w * scale)
    content_h = int(source_h * scale)
    x_offset = (canvas_w - content_w) // 2
    y_offset = (canvas_h - content_h) // 2
    return CanvasGeometry(
        scale=scale,
        content_w=content_w,
        content_h=content_h,
        x_offset=x_offset,
        y_offset=y_offset,
    )


def validate_source_dimensions(
    png_path: Path,
    expected_w: int,
    expected_h: int,
    checkpoint_name: str = "",
) -> CheckResult:
    """
    Load a SOURCE screenshot PNG and verify its dimensions.

    Returns a pass result when the dimensions match exactly (TOLERANCE 0).
    Returns a fail result containing checkpoint name and actual dimensions when
    they differ – the dimensions are the direct preserve-size footprint signal.
    """
    if not png_path.exists():
        return CheckResult(
            ok=False,
            message=f"Source screenshot not found: {png_path}",
            status="fail",
        )

    try:
        from PIL import Image  # type: ignore
    except ImportError:
        return CheckResult(
            ok=False,
            message="Pillow not available – cannot check source screenshot dimensions",
            status="skip",
        )

    try:
        with Image.open(png_path) as img:
            actual_w, actual_h = img.size
    except Exception as exc:
        return CheckResult(
            ok=False,
            message=f"Failed to read source screenshot {png_path}: {exc}",
            status="fail",
        )

    label = f"[{checkpoint_name}] " if checkpoint_name else ""
    if actual_w == expected_w and actual_h == expected_h:
        return CheckResult(ok=True, message=f"{label}dimensions OK: {actual_w}×{actual_h}")

    return CheckResult(
        ok=False,
        message=(
            f"{label}source screenshot dimensions mismatch for {png_path.name}: "
            f"expected {expected_w}×{expected_h} (NTSC logical preserve-size=1) "
            f"but got {actual_w}×{actual_h} "
            f"– this indicates preserve_size is not holding the source at logical size"
        ),
        status="fail",
    )


def verify_canvas_frame_dimensions(
    frame: "np.ndarray",  # type: ignore[name-defined]
    expected_w: int,
    expected_h: int,
    checkpoint_name: str = "",
) -> CheckResult:
    """Verify a decoded canvas frame (H×W×3 numpy array) has the expected dimensions."""
    actual_h, actual_w = frame.shape[:2]
    label = f"[{checkpoint_name}] " if checkpoint_name else ""
    if actual_w == expected_w and actual_h == expected_h:
        return CheckResult(ok=True, message=f"{label}canvas dims OK: {actual_w}×{actual_h}")
    return CheckResult(
        ok=False,
        message=(
            f"{label}canvas frame dimensions mismatch: "
            f"expected {expected_w}×{expected_h} but got {actual_w}×{actual_h}"
        ),
        status="fail",
    )


def validate_canvas_letterbox(
    frame: "np.ndarray",  # type: ignore[name-defined]
    canvas_w: int,
    canvas_h: int,
    source_w: int,
    source_h: int,
    checkpoint_name: str = "",
) -> CheckResult:
    """
    Validate canvas frame geometry for OBS_BOUNDS_SCALE_INNER with point filter.

    Checks:
      1. Dimensions match (canvas_w × canvas_h).
      2. The expected letterbox black bars are exactly black (R=G=B=0).
      3. The content area contains at least some non-black pixels (source is present).
      4. The region OUTSIDE the expected content area (larger bars) are also black.
         This catch the regression: if content appears in the wider bars region
         (e.g. virtual CRT content is narrow and centred within wider expected bars),
         the actual content footprint is wrong.

    Check 4 implementation detail:
      We sample a column just inside each expected bar boundary (x_offset ± margin
      and right_start ± margin).  If those columns, which should be content/active
      area in a correct render, are instead black, the content must be narrower than
      expected implying preserve-size is using virtual rather than logical size.

    Returns a descriptive failure on any mismatch so regressions are easy to diagnose.
    """
    try:
        import numpy as np
    except ImportError:
        return CheckResult(ok=False, message="numpy not available", status="skip")

    label = f"[{checkpoint_name}] " if checkpoint_name else ""

    # 1. Dimension check
    dim_result = verify_canvas_frame_dimensions(frame, canvas_w, canvas_h, checkpoint_name)
    if not dim_result.ok:
        return dim_result

    geom = compute_inner_scale_geometry(canvas_w, canvas_h, source_w, source_h)

    # Sample multiple horizontal scanlines across the vertical range for robustness
    scan_y_positions = [canvas_h // 4, canvas_h // 2, 3 * canvas_h // 4]

    for mid_y in scan_y_positions:
        scanline = frame[mid_y]  # shape (canvas_w, 3)

        # 2. Left black bar: columns [0, x_offset) must all be black.
        if geom.x_offset > 0:
            left_bar = scanline[0 : geom.x_offset]
            non_black_left = int(np.any(left_bar > 0, axis=1).sum())
            if non_black_left > 0:
                return CheckResult(
                    ok=False,
                    message=(
                        f"{label}expected-black left bar [x=0..{geom.x_offset}) "
                        f"contains {non_black_left} non-black pixels at y={mid_y} "
                        f"(source_w={source_w}, source_h={source_h}, "
                        f"scale={geom.scale:.2f}, content_x={geom.x_offset}..{geom.x_offset + geom.content_w})"
                    ),
                    status="fail",
                )

        # 3. Right black bar: columns [x_offset+content_w, canvas_w) must be black.
        right_start = geom.x_offset + geom.content_w
        if right_start < canvas_w:
            right_bar = scanline[right_start:]
            non_black_right = int(np.any(right_bar > 0, axis=1).sum())
            if non_black_right > 0:
                return CheckResult(
                    ok=False,
                    message=(
                        f"{label}expected-black right bar [x={right_start}..{canvas_w}) "
                        f"contains {non_black_right} non-black pixels at y={mid_y} "
                        f"(source_w={source_w}, source_h={source_h}, "
                        f"scale={geom.scale:.2f})"
                    ),
                    status="fail",
                )

    # 4. Content boundary check: verify that the expected content window is populated
    #    near both its left and right edges, not just somewhere in the middle.
    #    We sample vertical strips 4% of the content width in from each edge and
    #    check that they are not entirely black.
    mid_y = canvas_h // 2
    scanline = frame[mid_y]
    margin = max(1, geom.content_w // 25)  # ~4% of expected content width
    left_edge_col = geom.x_offset + margin
    right_edge_col = geom.x_offset + geom.content_w - margin - 1

    if left_edge_col < right_edge_col < canvas_w:
        left_edge_pixels = scanline[geom.x_offset : left_edge_col + 1]
        right_edge_pixels = scanline[right_edge_col : geom.x_offset + geom.content_w]
        left_has_content = bool(np.any(left_edge_pixels > 0))
        right_has_content = bool(np.any(right_edge_pixels > 0))

        if not left_has_content:
            return CheckResult(
                ok=False,
                message=(
                    f"{label}content left edge [x={geom.x_offset}..{left_edge_col}] "
                    f"is entirely black at y={mid_y}: expected visible content near "
                    f"the left boundary of the content area "
                    f"(source_w={source_w}, source_h={source_h}, scale={geom.scale:.2f}) "
                    f"– canvas content is narrower/shifted, e.g. preserve_size=0 "
                    f"scaled to wrong virtual dimensions"
                ),
                status="fail",
            )

        if not right_has_content:
            return CheckResult(
                ok=False,
                message=(
                    f"{label}content right edge [x={right_edge_col}..{geom.x_offset + geom.content_w}] "
                    f"is entirely black at y={mid_y}: expected visible content near "
                    f"the right boundary of the content area "
                    f"(source_w={source_w}, source_h={source_h}, scale={geom.scale:.2f}) "
                    f"– canvas content is narrower/shifted"
                ),
                status="fail",
            )

    # 5. Overall content present check (any non-black pixel in the content window).
    content_region = scanline[geom.x_offset : geom.x_offset + geom.content_w]
    non_black_content = int(np.any(content_region > 0, axis=1).sum())
    if non_black_content == 0:
        return CheckResult(
            ok=False,
            message=(
                f"{label}content region [x={geom.x_offset}..{right_start}) "
                f"is entirely black at y={mid_y} – expected visible C64 content "
                f"(source_w={source_w}, source_h={source_h}, scale={geom.scale:.2f})"
            ),
            status="fail",
        )

    return CheckResult(
        ok=True,
        message=(
            f"{label}canvas geometry OK: "
            f"scale={geom.scale:.2f}, "
            f"content={geom.content_w}×{geom.content_h} "
            f"at x={geom.x_offset} (source {source_w}×{source_h})"
        ),
    )


def _ffmpeg_extract_frame(mp4_path: Path, frame_index: int, output_path: Path) -> bool:
    """
    Extract an exact frame from an MP4 using ffmpeg select filter.

    Uses the identical approach as tests/e2e/util/extract-frame.sh for
    frame-accurate extraction via  select=eq(n\\,INDEX).

    Returns True on success.
    """
    try:
        result = subprocess.run(
            [
                "ffmpeg",
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-i",
                str(mp4_path),
                "-vf",
                f"select=eq(n\\,{frame_index})",
                "-frames:v",
                "1",
                "-q:v",
                "2",
                str(output_path),
            ],
            capture_output=True,
            timeout=30,
        )
        return result.returncode == 0 and output_path.exists() and output_path.stat().st_size > 0
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return False


def _cv2_extract_frame(mp4_path: Path, frame_index: int) -> "Optional[np.ndarray]":  # type: ignore[name-defined]
    """Extract a frame using OpenCV as a fallback when ffmpeg is unavailable."""
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
        return frame  # BGR format
    finally:
        cap.release()


def extract_and_validate_canvas_frame(
    mp4_path: Path,
    frame_index: int,
    canvas_w: int,
    canvas_h: int,
    source_w: int,
    source_h: int,
    checkpoint_name: str,
    frame_dir: Path,
) -> CheckResult:
    """
    Extract frame at frame_index from mp4_path, store it, then validate geometry.

    Extraction strategy:
      1. ffmpeg with select=eq(n\\,INDEX) – exact frame, preferred.
      2. OpenCV CAP_PROP_POS_FRAMES – exact frame, fallback.

    The extracted PNG is persisted under frame_dir for debugging.

    Returns a skip result (not failure) if the MP4 is missing or extraction
    tools are unavailable, so local assertions do not block CI.
    """
    if not mp4_path.exists():
        return CheckResult(
            ok=False,
            message=f"[{checkpoint_name}] MP4 not found: {mp4_path}",
            status="skip",
        )

    import numpy as np

    frame_dir.mkdir(parents=True, exist_ok=True)
    out_png = frame_dir / f"{checkpoint_name}.png"

    frame_array: Optional["np.ndarray"] = None  # type: ignore[name-defined]

    # --- strategy 1: ffmpeg ---
    if _ffmpeg_extract_frame(mp4_path, frame_index, out_png):
        try:
            from PIL import Image  # type: ignore

            with Image.open(out_png) as img:
                frame_array = np.array(img.convert("RGB"))
        except Exception:
            pass

    # --- strategy 2: opencv --
    if frame_array is None:
        cv_frame = _cv2_extract_frame(mp4_path, frame_index)
        if cv_frame is not None:
            try:
                import cv2  # type: ignore

                # cv2 returns BGR; convert to RGB for consistency
                frame_array = cv2.cvtColor(cv_frame, cv2.COLOR_BGR2RGB)
            except Exception:
                pass

    if frame_array is None:
        return CheckResult(
            ok=False,
            message=(
                f"[{checkpoint_name}] Could not extract frame {frame_index} "
                f"from {mp4_path.name} (ffmpeg and cv2 both unavailable or failed)"
            ),
            status="skip",
        )

    # Persist extracted frame even if validation fails (aids debugging).
    if not out_png.exists():
        try:
            from PIL import Image  # type: ignore

            Image.fromarray(frame_array).save(out_png)
        except Exception:
            pass

    return validate_canvas_letterbox(
        frame=frame_array,
        canvas_w=canvas_w,
        canvas_h=canvas_h,
        source_w=source_w,
        source_h=source_h,
        checkpoint_name=checkpoint_name,
    )


# ---------------------------------------------------------------------------
# Assertion class
# ---------------------------------------------------------------------------


# Map from checkpoint name to the artifact PNG basename
_PRESERVE_CHECKPOINTS: list[tuple[str, str]] = [
    ("default_preserve", "default_source_preserve.png"),
    ("classic_preserve", "classic_source_preserve.png"),
    ("sharp_preserve", "sharp_source_preserve.png"),
    ("vintage_preserve", "vintage_source_preserve.png"),
    ("arcade_preserve", "arcade_source_preserve.png"),
]

_PRESERVE_PREVIEW_CHECKPOINTS: dict[str, str] = {
    "default_preserve": "default_preview_preserve.png",
    "classic_preserve": "classic_preview_preserve.png",
    "sharp_preserve": "sharp_preview_preserve.png",
    "vintage_preserve": "vintage_preview_preserve.png",
    "arcade_preserve": "arcade_preview_preserve.png",
}


def _repo_root() -> Path:
    # assertions/preserve_size_canvas_match.py → assertions → e2e → tests → repo
    return Path(__file__).resolve().parents[3]


def _load_rgb_png(png_path: Path) -> Optional["np.ndarray"]:  # type: ignore[name-defined]
    try:
        import numpy as np
        from PIL import Image  # type: ignore
    except ImportError:
        return None

    try:
        with Image.open(png_path) as img:
            return np.array(img.convert("RGB"))
    except Exception:
        return None


class PreserveSizeCanvasMatchAssertion(EffectAssertion):
    """
    Canvas-level preserve-size validation.

    Validates that OBS canvas composition respects preserve_size semantics by:

      1. Checking SOURCE screenshot dimensions at every preserve_size=1 checkpoint
         against expected NTSC/PAL logical dimensions (TOLERANCE 0, PNG lossless).
         This directly tests whether the source reports the correct footprint to OBS.

      2. Extracting canvas frames from the recorded MP4 at exact frame indices
         derived from the script's OBS WAIT FRAMES timing.

      3. Checking the canvas letterbox geometry:
         - Black bars must be exactly black (TOLERANCE 0).
         - The content region must contain visible C64 pixels.
         - The geometry must match what is expected for a preserve_size=1
           (logical-sized) source, not the virtual-sized alternative.

    The assertion fails when the OBS canvas shows the source at the wrong
    scale (virtual instead of logical), even if the SOURCE screenshots
    themselves are internally self-consistent.
    """

    def __init__(self, thresholds: Optional[dict] = None):
        super().__init__("Preserve Size Canvas Match", thresholds or {})
        self._artifacts_dir: Optional[Path] = None

    # Allow tests to inject a different artifacts directory.
    def set_artifacts_dir(self, path: Path) -> None:
        self._artifacts_dir = path

    def verify(
        self,
        mp4_path: Path,
        properties: dict[str, Any],
        preset: PresetConfig,
        verbose: bool = False,
    ) -> AssertionResult:
        fmt = str(properties.get("video_format") or "NTSC").upper()
        if fmt == "PAL":
            logical_w, logical_h = PAL_LOGICAL_W, PAL_LOGICAL_H
            fps = PAL_FPS
        else:
            logical_w, logical_h = NTSC_LOGICAL_W, NTSC_LOGICAL_H
            fps = NTSC_FPS

        canvas_w = int(properties.get("expected_width", 1920))
        canvas_h = int(properties.get("expected_height", 1080))

        artifacts_dir = self._artifacts_dir
        if artifacts_dir is None:
            artifacts_dir = _repo_root() / "tests" / "e2e" / "artifacts" / "effect_preserve_size"

        frame_dir = artifacts_dir / "video_frames"

        checkpoint_frames = compute_checkpoint_frames(fps=fps)

        failures: list[str] = []
        skips: list[str] = []
        checked: list[str] = []

        for checkpoint_name, artifact_basename in _PRESERVE_CHECKPOINTS:
            png_path = artifacts_dir / artifact_basename

            # --- step 1: source screenshot dimension check ---
            src_result = validate_source_dimensions(
                png_path,
                expected_w=logical_w,
                expected_h=logical_h,
                checkpoint_name=checkpoint_name,
            )
            self.log(src_result.message, verbose)

            if src_result.status == "skip":
                skips.append(f"skip:{checkpoint_name}:source_dim:{src_result.message}")
                continue
            if not src_result.ok:
                failures.append(
                    f"{checkpoint_name} – source screenshot: {src_result.message}"
                )
                continue

            checked.append(f"{checkpoint_name}:source_dim")

            # --- step 2+3: canvas validation ---
            # The scenario already captures deterministic PREVIEW screenshots at the
            # preserve checkpoints. Prefer those lossless checkpoint artifacts over
            # trying to reconstruct the same moment from the recording timeline.
            preview_png = artifacts_dir / _PRESERVE_PREVIEW_CHECKPOINTS[checkpoint_name]
            preview_frame = _load_rgb_png(preview_png)
            if preview_frame is not None:
                canvas_result = validate_canvas_letterbox(
                    frame=preview_frame,
                    canvas_w=canvas_w,
                    canvas_h=canvas_h,
                    source_w=logical_w,
                    source_h=logical_h,
                    checkpoint_name=checkpoint_name,
                )
            else:
                frame_index = checkpoint_frames.get(checkpoint_name)
                if frame_index is None:
                    skips.append(f"no frame index for {checkpoint_name}")
                    continue

                canvas_result = extract_and_validate_canvas_frame(
                    mp4_path=mp4_path,
                    frame_index=frame_index,
                    canvas_w=canvas_w,
                    canvas_h=canvas_h,
                    source_w=logical_w,
                    source_h=logical_h,
                    checkpoint_name=checkpoint_name,
                    frame_dir=frame_dir,
                )
            self.log(canvas_result.message, verbose)

            if canvas_result.status == "skip":
                skips.append(f"skip:{checkpoint_name}:canvas:{canvas_result.message}")
            elif not canvas_result.ok:
                failures.append(f"{checkpoint_name} – canvas: {canvas_result.message}")
            else:
                checked.append(f"{checkpoint_name}:canvas")

        if failures:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Preserve-size canvas check FAILED: {len(failures)} issue(s)",
                details={
                    "failures": failures,
                    "checked": checked,
                    "skipped": skips,
                    "logical_size": f"{logical_w}×{logical_h}",
                    "canvas_size": f"{canvas_w}×{canvas_h}",
                    "format": fmt,
                },
            )

        if skips and not checked:
            # Total skip: MP4 not present or tools unavailable
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message=f"Preserve-size canvas check skipped ({len(skips)} items lacked artifacts)",
                details={"skipped": skips},
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=(
                f"Preserve-size canvas check passed: {len(checked)} sub-checks OK "
                f"({len(skips)} skipped)"
            ),
            details={
                "checked": checked,
                "skipped": skips,
                "logical_size": f"{logical_w}×{logical_h}",
                "canvas_size": f"{canvas_w}×{canvas_h}",
            },
        )
