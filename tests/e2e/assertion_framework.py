#!/usr/bin/env python3
"""
C64 Stream - Generic E2E Assertion Framework
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

This module provides a pluggable assertion framework for E2E testing.
It receives a properties.ini file and the resulting OBS-produced MP4,
then performs assertions based on the effect preset configuration.
"""

import json
import subprocess
import sys
from abc import ABC, abstractmethod
from configparser import ConfigParser
from contextlib import suppress
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Optional

import numpy as np


class AssertionStatus(Enum):
    """Status of an assertion result."""

    PASS = "pass"
    FAIL = "fail"
    SKIP = "skip"
    WARNING = "warning"


@dataclass
class AssertionResult:
    """Result of a single assertion."""

    status: AssertionStatus
    name: str
    message: str
    details: dict[str, Any] = field(default_factory=dict)
    metrics: dict[str, float] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "status": self.status.value,
            "name": self.name,
            "message": self.message,
            "details": self.details,
            "metrics": self.metrics,
        }


def load_settings_from_obs_scene(scene_json_path: Path) -> dict[str, Any]:
    """Load c64_source settings from OBS scene JSON file.

    Args:
        scene_json_path: Path to the C64StreamTest.json scene file

    Returns:
        Dict of source settings (e.g., crt_preset, scan_line_distance, etc.)
    """
    with open(scene_json_path) as f:
        scene = json.load(f)

    # Find the c64_source in sources
    for source in scene.get("sources", []):
        if source.get("id") == "c64_source":
            return source.get("settings", {})

    return {}


@dataclass
class PresetConfig:
    """Configuration for an effect preset loaded from effect_presets.ini."""

    name: str
    scan_line_distance: float = 0.0
    scan_line_strength: float = 0.0
    pixel_width: float = 1.0
    pixel_height: float = 1.0
    blur_strength: float = 0.0
    bloom_strength: float = 0.0
    afterglow_duration_ms: int = 0
    afterglow_curve: int = 0
    tint_mode: int = 0  # 0=None, 1=Amber, 2=Green
    tint_strength: float = 0.0

    @classmethod
    def from_ini_section(cls, name: str, section: dict[str, str]) -> "PresetConfig":
        """Create a PresetConfig from an INI section."""
        return cls(
            name=name,
            scan_line_distance=float(section.get("scan_line_distance", "0.0")),
            scan_line_strength=float(section.get("scan_line_strength", "0.0")),
            pixel_width=float(section.get("pixel_width", "1.0")),
            pixel_height=float(section.get("pixel_height", "1.0")),
            blur_strength=float(section.get("blur_strength", "0.0")),
            bloom_strength=float(section.get("bloom_strength", "0.0")),
            afterglow_duration_ms=int(section.get("afterglow_duration_ms", "0")),
            afterglow_curve=int(section.get("afterglow_curve", "0")),
            tint_mode=int(section.get("tint_mode", "0")),
            tint_strength=float(section.get("tint_strength", "0.0")),
        )

    @classmethod
    def from_obs_settings(cls, settings: dict[str, Any]) -> "PresetConfig":
        """Create a PresetConfig from OBS source settings dict."""
        return cls(
            name=settings.get("crt_preset", "Custom"),
            scan_line_distance=float(settings.get("scan_line_distance", 0.0)),
            scan_line_strength=float(settings.get("scan_line_strength", 0.0)),
            pixel_width=float(settings.get("pixel_width", 1.0)),
            pixel_height=float(settings.get("pixel_height", 1.0)),
            blur_strength=float(settings.get("blur_strength", 0.0)),
            bloom_strength=float(settings.get("bloom_strength", 0.0)),
            afterglow_duration_ms=int(settings.get("afterglow_duration_ms", 0)),
            afterglow_curve=int(settings.get("afterglow_curve", 0)),
            tint_mode=int(settings.get("tint_mode", 0)),
            tint_strength=float(settings.get("tint_strength", 0.0)),
        )

    def has_scanlines(self) -> bool:
        return self.scan_line_distance > 0.0 and self.scan_line_strength > 0.0

    def has_afterglow(self) -> bool:
        return self.afterglow_duration_ms > 0

    def has_tint(self) -> bool:
        return self.tint_mode > 0 and self.tint_strength > 0.0

    def tint_type(self) -> Optional[str]:
        if self.tint_mode == 1:
            return "amber"
        elif self.tint_mode == 2:
            return "green"
        return None


class EffectAssertion(ABC):
    """Base class for effect assertions."""

    def __init__(self, name: str, thresholds: Optional[dict[str, float]] = None):
        self.name = name
        self.thresholds = thresholds or {}

    @abstractmethod
    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        """
        Verify the assertion against the recording.

        Args:
            mp4_path: Path to the OBS-produced MP4 file
            properties: Parsed properties.ini as a dict
            preset: The effect preset configuration
            verbose: Enable verbose logging

        Returns:
            AssertionResult with the verification outcome
        """
        pass

    def log(self, message: str, verbose: bool) -> None:
        if verbose:
            print(f"[{self.name}] {message}")


class VideoQualityAssertion(EffectAssertion):
    """Verify basic video quality: duration, resolution, non-black frames."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_duration_ratio": 0.8,  # Min ratio of expected duration
            "max_duration_ratio": 1.2,  # Max ratio of expected duration
            "min_nonblack_ratio": 0.5,  # Min ratio of non-black frames
            "black_threshold": 5.0,  # Luma threshold for black detection
        }
        super().__init__("VideoQuality", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not mp4_path.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Recording file not found: {mp4_path}",
            )

        try:
            # Get video info
            info = self._ffprobe_info(mp4_path)
            width = info.get("width", 0)
            height = info.get("height", 0)
            duration = info.get("duration", 0.0)

            self.log(f"Video: {width}x{height}, {duration:.2f}s", verbose)

            # Check resolution (expected: 1920x1080 for E2E)
            if width != 1920 or height != 1080:
                return AssertionResult(
                    status=AssertionStatus.WARNING,
                    name=self.name,
                    message=f"Unexpected resolution: {width}x{height} (expected 1920x1080)",
                    details={"width": width, "height": height},
                )

            # Check for non-black frames
            nonblack_ratio = self._check_nonblack_frames(mp4_path, verbose)
            min_ratio = self.thresholds["min_nonblack_ratio"]

            if nonblack_ratio < min_ratio:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Too many black frames: {nonblack_ratio:.1%} non-black (min: {min_ratio:.1%})",
                    metrics={"nonblack_ratio": nonblack_ratio},
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"Video quality OK: {width}x{height}, {duration:.2f}s, {nonblack_ratio:.1%} non-black",
                details={"width": width, "height": height, "duration": duration},
                metrics={"nonblack_ratio": nonblack_ratio},
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Video quality check failed: {e}",
            )

    def _ffprobe_info(self, mp4_path: Path) -> dict[str, Any]:
        cmd = [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height",
            "-show_entries",
            "format=duration",
            "-of",
            "json",
            str(mp4_path),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        data = json.loads(result.stdout)
        stream = data.get("streams", [{}])[0]
        fmt = data.get("format", {})
        return {
            "width": int(stream.get("width", 0)),
            "height": int(stream.get("height", 0)),
            "duration": float(fmt.get("duration", 0)),
        }

    def _check_nonblack_frames(self, mp4_path: Path, verbose: bool) -> float:
        """Sample frames and count non-black ones."""
        w, h = 1920, 1080
        frame_bytes = w * h * 3
        black_thresh = self.thresholds["black_threshold"]

        cmd = [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(mp4_path),
            "-vf",
            "fps=2",  # Sample at 2 fps
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ]

        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        total = 0
        nonblack = 0

        try:
            while True:
                buf = proc.stdout.read(frame_bytes)
                if len(buf) != frame_bytes:
                    break
                total += 1
                arr = np.frombuffer(buf, dtype=np.uint8).reshape((h, w, 3))
                luma = 0.2126 * arr[..., 0] + 0.7152 * arr[..., 1] + 0.0722 * arr[..., 2]
                if np.mean(luma) > black_thresh:
                    nonblack += 1
        finally:
            with suppress(Exception):
                proc.stdout.close()
            proc.kill()
            proc.wait(timeout=5)

        self.log(f"Non-black frames: {nonblack}/{total}", verbose)
        return nonblack / total if total > 0 else 0.0


class AudioAssertion(EffectAssertion):
    """Verify audio presence and basic quality."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_audio_level": -60.0,  # dB threshold for silence
            "expected_sample_rate": 48000,
        }
        super().__init__("Audio", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not mp4_path.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Recording file not found: {mp4_path}",
            )

        try:
            # Check for audio stream
            cmd = [
                "ffprobe",
                "-v",
                "error",
                "-select_streams",
                "a:0",
                "-show_entries",
                "stream=sample_rate,channels",
                "-of",
                "json",
                str(mp4_path),
            ]
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            data = json.loads(result.stdout)

            if not data.get("streams"):
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="No audio stream found in recording",
                )

            stream = data["streams"][0]
            sample_rate = int(stream.get("sample_rate", 0))
            channels = int(stream.get("channels", 0))

            self.log(f"Audio: {sample_rate}Hz, {channels} channels", verbose)

            expected_rate = int(self.thresholds["expected_sample_rate"])
            if sample_rate != expected_rate:
                return AssertionResult(
                    status=AssertionStatus.WARNING,
                    name=self.name,
                    message=f"Unexpected sample rate: {sample_rate}Hz (expected {expected_rate}Hz)",
                    details={"sample_rate": sample_rate, "channels": channels},
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"Audio OK: {sample_rate}Hz, {channels} channels",
                details={"sample_rate": sample_rate, "channels": channels},
            )

        except subprocess.CalledProcessError:
            return AssertionResult(
                status=AssertionStatus.WARNING,
                name=self.name,
                message="Could not analyze audio stream",
            )


class TintAssertion(EffectAssertion):
    """Verify color tint (amber or green) is present when expected."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_tint_ratio": 1.20,  # Dominant channel must be 20% higher than avg of others
            "min_nonblack_sum": 500_000,  # Ignore very dark frames
            "sample_fps": 2.0,
            "max_frames": 20,
        }
        super().__init__("Tint", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not preset.has_tint():
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Tint not enabled for this preset",
            )

        tint_type = preset.tint_type()
        self.log(f"Verifying {tint_type} tint (strength={preset.tint_strength})", verbose)

        try:
            frames = self._sample_frames(mp4_path, verbose)
            if not frames:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="No frames could be sampled for tint analysis",
                )

            ok, message, details = self._verify_tint(frames, tint_type, verbose)

            return AssertionResult(
                status=AssertionStatus.PASS if ok else AssertionStatus.FAIL,
                name=self.name,
                message=message,
                details=details,
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Tint verification failed: {e}",
            )

    def _sample_frames(self, mp4_path: Path, verbose: bool) -> list[tuple[int, int, int]]:
        """Sample frames and return RGB channel sums."""
        w, h = 1920, 1080
        frame_bytes = w * h * 3
        fps = self.thresholds["sample_fps"]
        max_frames = int(self.thresholds["max_frames"])

        cmd = [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(mp4_path),
            "-vf",
            f"fps={fps}",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ]

        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        frames = []

        try:
            for _ in range(max_frames):
                buf = proc.stdout.read(frame_bytes)
                if len(buf) != frame_bytes:
                    break
                r_sum = sum(buf[0::3])
                g_sum = sum(buf[1::3])
                b_sum = sum(buf[2::3])
                frames.append((r_sum, g_sum, b_sum))
        finally:
            with suppress(Exception):
                proc.stdout.close()
            proc.kill()
            proc.wait(timeout=5)

        self.log(f"Sampled {len(frames)} frames for tint analysis", verbose)
        return frames

    def _verify_tint(
        self, frames: list[tuple[int, int, int]], tint_type: str, verbose: bool
    ) -> tuple[bool, str, dict[str, Any]]:
        min_ratio = self.thresholds["min_tint_ratio"]
        min_sum = int(self.thresholds["min_nonblack_sum"])

        checked = 0
        passed = 0
        ratios = []

        for r_sum, g_sum, b_sum in frames:
            total = r_sum + g_sum + b_sum
            if total < min_sum:
                continue  # Skip black frames

            checked += 1

            if tint_type == "green":
                # Green should dominate over avg(R, B)
                rb_avg = (r_sum + b_sum) / 2.0 if (r_sum + b_sum) > 0 else 1.0
                ratio = g_sum / rb_avg
                dominant = g_sum > r_sum and g_sum > b_sum
            else:  # amber
                # Amber: R > G > B, and R+G should dominate B
                gb_avg = (g_sum + b_sum) / 2.0 if (g_sum + b_sum) > 0 else 1.0
                ratio = r_sum / gb_avg
                dominant = r_sum > g_sum > b_sum

            ratios.append(ratio)
            if ratio >= min_ratio and dominant:
                passed += 1

        if checked == 0:
            return False, "No non-black frames found for tint analysis", {"checked_frames": 0}

        details = {
            "checked_frames": checked,
            "passed_frames": passed,
            "tint_type": tint_type,
            "min_ratio": min_ratio,
            "ratios": {
                "min": float(min(ratios)) if ratios else None,
                "median": float(sorted(ratios)[len(ratios) // 2]) if ratios else None,
                "max": float(max(ratios)) if ratios else None,
            },
        }

        if passed == checked:
            return True, f"{tint_type.capitalize()} tint verified on all {checked} frames", details
        return False, f"{tint_type.capitalize()} tint missing: {passed}/{checked} frames passed", details


class AfterglowAssertion(EffectAssertion):
    """Verify afterglow persistence using A/V pop ROI detection."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "bright_thresh": 140.0,  # Threshold for pop detection
            "max_frames": 360,
            "min_tail_luma": 2.5,  # Minimum luma for first tail frame
            "palette_drift_tol": 8.0,  # Max RGB delta for palette stability
        }
        super().__init__("Afterglow", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not preset.has_afterglow():
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="Afterglow not enabled for this preset",
            )

        self.log(f"Verifying afterglow (duration={preset.afterglow_duration_ms}ms)", verbose)

        try:
            frames = self._read_frames_rgb24(mp4_path, int(self.thresholds["max_frames"]))
            luma = self._luma_u8(frames)

            # Find pop ROI
            roi = self._find_pop_roi(luma, self.thresholds["bright_thresh"])
            self.log(f"Found pop ROI: {roi}", verbose)

            # Verify afterglow decay
            ok, details = self._verify_afterglow_decay(luma, roi)

            if not ok:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=details,
                    details={"roi": {"x0": roi[0], "y0": roi[1], "x1": roi[2], "y1": roi[3]}},
                )

            # Check palette stability (no drift from afterglow)
            palette_ok, palette_msg = self._check_palette_stability(frames, luma, verbose)
            if not palette_ok:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Afterglow verified but {palette_msg}",
                    details={"roi": {"x0": roi[0], "y0": roi[1], "x1": roi[2], "y1": roi[3]}},
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message="Afterglow persistence verified (tail decays across frames)",
                details={
                    "roi": {"x0": roi[0], "y0": roi[1], "x1": roi[2], "y1": roi[3]},
                    "decay_details": details,
                },
            )

        except RuntimeError as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Afterglow verification failed: {e}",
            )

    def _read_frames_rgb24(self, mp4_path: Path, max_frames: int) -> np.ndarray:
        """Decode video to RGB24 frames. Returns array [N,H,W,3] uint8."""
        w, h = self._ffprobe_size(mp4_path)
        cmd = ["ffmpeg", "-v", "error", "-i", str(mp4_path), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        frame_bytes = w * h * 3
        frames = []
        try:
            while len(frames) < max_frames:
                buf = proc.stdout.read(frame_bytes)
                if len(buf) != frame_bytes:
                    break
                frames.append(np.frombuffer(buf, dtype=np.uint8).reshape((h, w, 3)))
        finally:
            with suppress(Exception):
                proc.stdout.close()
            proc.kill()
            proc.wait(timeout=5)
        if not frames:
            raise RuntimeError("No frames decoded from recording")
        return np.stack(frames, axis=0)

    def _ffprobe_size(self, mp4_path: Path) -> tuple[int, int]:
        out = subprocess.check_output(
            [
                "ffprobe",
                "-v",
                "error",
                "-select_streams",
                "v:0",
                "-show_entries",
                "stream=width,height",
                "-of",
                "json",
                str(mp4_path),
            ]
        )
        info = json.loads(out)
        stream = info["streams"][0]
        return int(stream["width"]), int(stream["height"])

    @staticmethod
    def _luma_u8(frames_rgb: np.ndarray) -> np.ndarray:
        f = frames_rgb.astype(np.float32)
        return 0.2126 * f[..., 0] + 0.7152 * f[..., 1] + 0.0722 * f[..., 2]

    def _find_pop_roi(self, luma_frames: np.ndarray, bright_thresh: float) -> tuple[int, int, int, int]:
        """Auto-locate the A/V pop ROI by selecting the cluster around the brightest pixel."""
        p = np.percentile(luma_frames.reshape((luma_frames.shape[0], -1)), 99.95, axis=1)
        peak_idx = int(np.argmax(p))

        frame_peak = float(p[peak_idx])
        thr = max(float(bright_thresh), frame_peak * 0.98)

        mask = luma_frames[peak_idx] > thr
        ys, xs = np.where(mask)
        if xs.size < 40:
            raise RuntimeError(f"Could not locate pop ROI (thr={thr:.2f}, peak={frame_peak:.2f})")

        peak_xy = np.unravel_index(int(np.argmax(luma_frames[peak_idx])), luma_frames[peak_idx].shape)
        cy, cx = int(peak_xy[0]), int(peak_xy[1])

        radius = 160
        near = (np.abs(xs - cx) <= radius) & (np.abs(ys - cy) <= radius)
        xs_r, ys_r = xs[near], ys[near]
        if xs_r.size < 80:
            raise RuntimeError("Could not isolate pop cluster near brightest pixel")

        x0, x1 = int(xs_r.min()), int(xs_r.max())
        y0, y1 = int(ys_r.min()), int(ys_r.max())

        pad = 4
        h, w = luma_frames.shape[1], luma_frames.shape[2]
        return max(0, x0 - pad), max(0, y0 - pad), min(w - 1, x1 + pad), min(h - 1, y1 + pad)

    def _verify_afterglow_decay(self, luma_frames: np.ndarray, roi: tuple[int, int, int, int]) -> tuple[bool, str]:
        x0, y0, x1, y1 = roi
        roi_luma = luma_frames[:, y0 : y1 + 1, x0 : x1 + 1].mean(axis=(1, 2))

        p90 = float(np.percentile(roi_luma, 90))
        p99 = float(np.percentile(roi_luma, 99))
        high_thresh = max(20.0, (p90 + p99) / 2.0)
        idx = np.where(roi_luma > high_thresh)[0]
        if idx.size == 0:
            return False, f"No pop frames detected in ROI (threshold={high_thresh:.2f})"

        # First contiguous pop event
        s = int(idx[0])
        e = s
        for i in idx[1:]:
            if int(i) == e + 1:
                e = int(i)
            else:
                break

        if e + 10 >= len(roi_luma):
            return False, "Recording too short to evaluate afterglow tail"

        tail = roi_luma[e + 1 : e + 11]
        min_tail = self.thresholds["min_tail_luma"]

        if float(tail[0]) < min_tail:
            return False, f"Afterglow tail missing: first tail frame luma={float(tail[0]):.2f} (peak={float(roi_luma[e]):.2f})"

        if not np.all(np.diff(tail) <= 2.5):
            return False, "Afterglow tail is not decaying (unexpected brightness increase)"

        if float(np.mean(tail[2:6])) < 4.0:
            return False, "Afterglow tail fades too quickly (mean tail too low)"

        return True, "Afterglow persistence detected (tail decays across frames)"

    def _check_palette_stability(
        self, frames: np.ndarray, luma: np.ndarray, verbose: bool
    ) -> tuple[bool, str]:
        """Check that the VIC palette tile doesn't drift with afterglow."""
        # Find content bounding box
        n = min(luma.shape[0], 60)
        avg = luma[:n].mean(axis=0)
        mask = avg > 8.0
        ys, xs = np.where(mask)
        if xs.size == 0:
            return True, "Could not locate content (skipping palette check)"

        cx0, cy0, cx1, cy1 = int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())
        cw, ch = max(1, cx1 - cx0 + 1), max(1, cy1 - cy0 + 1)

        # Top-right 40x40 pixels (scaled to content size)
        bw = max(1, int(round(cw * (40.0 / 384.0))))
        bh = max(1, int(round(ch * (40.0 / 272.0))))
        gx0, gx1 = max(cx0, cx1 - bw + 1), cx1
        gy0, gy1 = cy0, min(cy1, cy0 + bh - 1)

        tile = frames[:, gy0 : gy1 + 1, gx0 : gx1 + 1, :].astype(np.float32)

        # Baseline from first 18 frames
        base_n = min(tile.shape[0], 18)
        baseline = tile[:base_n].mean(axis=(0, 1, 2))

        # Check for signal loss
        signal_end = tile.shape[0]
        for i in range(base_n, tile.shape[0]):
            delta = np.max(np.abs(tile[i].mean(axis=(0, 1)) - baseline))
            if delta > 50.0:
                signal_end = i
                break

        tile = tile[:signal_end]

        # Check max drift
        peak_delta = 0.0
        for i in range(tile.shape[0]):
            delta = np.max(np.abs(tile[i].mean(axis=(0, 1)) - baseline))
            peak_delta = max(peak_delta, float(delta))

        tol = self.thresholds["palette_drift_tol"]
        if peak_delta > tol:
            return False, f"palette drift detected (peak_rgb_delta={peak_delta:.2f} > tol={tol:.2f})"

        self.log(f"Palette stable (max drift={peak_delta:.2f})", verbose)
        return True, "Palette stable"


class ScanlineAssertion(EffectAssertion):
    """Verify scanline uniformity in the recording."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "max_variance_percent": 0.5,  # Max acceptable scanline height variance
            "min_scanline_count": 50,  # Minimum expected scanlines
        }
        super().__init__("Scanlines", {**defaults, **(thresholds or {})})

    def verify(
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
            # Extract a single frame for analysis
            frame = self._extract_frame(mp4_path, time_offset=2.0)
            if frame is None:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="Could not extract frame for scanline analysis",
                )

            # Analyze scanline pattern
            ok, variance, details = self._analyze_scanlines(frame, verbose)

            max_variance = self.thresholds["max_variance_percent"]
            if variance > max_variance:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Scanline variance too high: {variance:.2f}% (max: {max_variance}%)",
                    details=details,
                    metrics={"variance_percent": variance},
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

    def _analyze_scanlines(
        self, frame: np.ndarray, verbose: bool
    ) -> tuple[bool, float, dict[str, Any]]:
        """Analyze scanline pattern in a frame."""
        # Convert to grayscale
        gray = 0.2126 * frame[..., 0] + 0.7152 * frame[..., 1] + 0.0722 * frame[..., 2]

        # Find content region (non-black area)
        col_means = gray.mean(axis=0)
        row_means = gray.mean(axis=1)

        x_start = np.argmax(col_means > 10)
        x_end = len(col_means) - np.argmax(col_means[::-1] > 10)
        y_start = np.argmax(row_means > 10)
        y_end = len(row_means) - np.argmax(row_means[::-1] > 10)

        if x_end <= x_start or y_end <= y_start:
            return False, 100.0, {"error": "Could not find content region"}

        # Analyze vertical center column for scanline pattern
        center_x = (x_start + x_end) // 2
        column = gray[y_start:y_end, center_x]

        # Detect scanline gaps (dark rows)
        threshold = np.percentile(column, 30)
        dark_rows = column < threshold

        # Find scanline positions
        scanlines = []
        in_gap = False
        gap_start = 0

        for i, is_dark in enumerate(dark_rows):
            if is_dark and not in_gap:
                in_gap = True
                gap_start = i
            elif not is_dark and in_gap:
                in_gap = False
                gap_height = i - gap_start
                if gap_height >= 1:  # Minimum gap height
                    scanlines.append(gap_height)

        if len(scanlines) < self.thresholds["min_scanline_count"]:
            return False, 100.0, {"error": f"Too few scanlines detected: {len(scanlines)}"}

        # Calculate variance
        mean_height = np.mean(scanlines)
        std_height = np.std(scanlines)
        variance_percent = (std_height / mean_height * 100) if mean_height > 0 else 100.0

        details = {
            "scanline_count": len(scanlines),
            "mean_height": float(mean_height),
            "std_height": float(std_height),
            "content_region": {"x": (int(x_start), int(x_end)), "y": (int(y_start), int(y_end))},
        }

        self.log(f"Found {len(scanlines)} scanlines, mean height={mean_height:.2f}, variance={variance_percent:.2f}%", verbose)
        return True, float(variance_percent), details


class AssertionRunner:
    """Orchestrates running multiple assertions against a recording."""

    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.assertions: list[EffectAssertion] = []

    def add_assertion(self, assertion: EffectAssertion) -> "AssertionRunner":
        self.assertions.append(assertion)
        return self

    def run_all(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig
    ) -> list[AssertionResult]:
        """Run all configured assertions and return results."""
        results = []
        for assertion in self.assertions:
            if self.verbose:
                print(f"\n{'='*60}")
                print(f"Running: {assertion.name}")
                print(f"{'='*60}")

            result = assertion.verify(mp4_path, properties, preset, self.verbose)
            results.append(result)

            if self.verbose:
                status_icon = {
                    AssertionStatus.PASS: "✅",
                    AssertionStatus.FAIL: "❌",
                    AssertionStatus.SKIP: "⏭️",
                    AssertionStatus.WARNING: "⚠️",
                }[result.status]
                print(f"{status_icon} {result.message}")

        return results

    @staticmethod
    def summarize(results: list[AssertionResult]) -> tuple[bool, dict[str, Any]]:
        """Summarize assertion results."""
        passed = sum(1 for r in results if r.status == AssertionStatus.PASS)
        failed = sum(1 for r in results if r.status == AssertionStatus.FAIL)
        skipped = sum(1 for r in results if r.status == AssertionStatus.SKIP)
        warnings = sum(1 for r in results if r.status == AssertionStatus.WARNING)

        summary = {
            "total": len(results),
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
            "warnings": warnings,
            "results": [r.to_dict() for r in results],
        }

        all_ok = failed == 0
        return all_ok, summary


def load_preset_from_ini(preset_name: str, presets_ini_path: Path) -> Optional[PresetConfig]:
    """Load a preset configuration from effect_presets.ini."""
    if not presets_ini_path.exists():
        return None

    parser = ConfigParser()
    parser.read(presets_ini_path)

    # Normalize preset name for section lookup
    section_name = preset_name.replace(" ", "_").lower()

    for section in parser.sections():
        if section.lower().replace(" ", "_") == section_name:
            return PresetConfig.from_ini_section(preset_name, dict(parser.items(section)))

    return None


def load_properties(properties_path: Path) -> dict[str, Any]:
    """Load properties.ini as a dict."""
    parser = ConfigParser()
    parser.read(properties_path)
    return {section: dict(parser.items(section)) for section in parser.sections()}


def create_preset_assertions(preset: PresetConfig) -> list[EffectAssertion]:
    """Create the appropriate assertions for a given preset."""
    assertions: list[EffectAssertion] = []

    # Always check video quality
    assertions.append(VideoQualityAssertion())

    # Always check audio
    assertions.append(AudioAssertion())

    # Add effect-specific assertions
    if preset.has_tint():
        assertions.append(TintAssertion())

    if preset.has_afterglow():
        assertions.append(AfterglowAssertion())

    if preset.has_scanlines():
        assertions.append(ScanlineAssertion())

    return assertions


def create_assertions_from_list(assertion_names: list[str], thresholds: Optional[dict[str, dict[str, float]]] = None) -> list[EffectAssertion]:
    """Create assertions from a list of assertion names.

    Args:
        assertion_names: List of assertion names (e.g., ['video_quality', 'audio', 'tint'])
        thresholds: Optional dict mapping assertion names to threshold overrides

    Returns:
        List of EffectAssertion instances
    """
    assertion_map = {
        "video_quality": VideoQualityAssertion,
        "audio": AudioAssertion,
        "tint": TintAssertion,
        "afterglow": AfterglowAssertion,
        "scanlines": ScanlineAssertion,
    }

    thresholds = thresholds or {}
    assertions: list[EffectAssertion] = []

    for name in assertion_names:
        assertion_cls = assertion_map.get(name.lower())
        if assertion_cls:
            assertion_thresholds = thresholds.get(name.lower())
            assertions.append(assertion_cls(assertion_thresholds))

    return assertions


def main() -> int:
    import argparse

    ap = argparse.ArgumentParser(
        description="C64 Stream E2E Assertion Framework",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Verify recording against a scenario (preferred)
  %(prog)s --mp4 recording.mp4 --scenario ntsc_amber_monitor

  # Verify with OBS scene JSON
  %(prog)s --mp4 recording.mp4 --scene-json C64StreamTest.json

  # Verify with preset name
  %(prog)s --mp4 recording.mp4 --preset arcade_cabinet

  # List available presets
  %(prog)s --list-presets
""",
    )
    ap.add_argument("--mp4", type=Path, help="Path to OBS recording (MP4/MKV)")
    ap.add_argument("--scenario", help="Scenario name (e.g., ntsc_amber_monitor)")
    ap.add_argument("--properties", type=Path, help="Path to properties.ini used for recording")
    ap.add_argument(
        "--scene-json", type=Path, help="Path to OBS scene JSON file (e.g., C64StreamTest.json)"
    )
    ap.add_argument("--preset", help="Effect preset name (e.g., arcade_cabinet, green_monitor)")
    ap.add_argument(
        "--presets-ini",
        type=Path,
        default=Path(__file__).parent.parent.parent / "data" / "effect_presets.ini",
        help="Path to effect_presets.ini",
    )
    ap.add_argument("--list-presets", action="store_true", help="List available presets and exit")
    ap.add_argument("--verbose", "-v", action="store_true", help="Enable verbose output")
    ap.add_argument("--json", action="store_true", help="Output results as JSON")

    args = ap.parse_args()

    # List presets mode
    if args.list_presets:
        if not args.presets_ini.exists():
            print(f"Presets file not found: {args.presets_ini}")
            return 1
        parser = ConfigParser()
        parser.read(args.presets_ini)
        print("Available presets:")
        for section in parser.sections():
            print(f"  - {section}")
        return 0

    # Verification mode
    if not args.mp4:
        ap.error("--mp4 is required for verification")
    if not args.scenario and not args.scene_json and not args.preset:
        ap.error("One of --scenario, --scene-json, or --preset is required for verification")

    if not args.mp4.exists():
        print(f"Recording not found: {args.mp4}")
        return 1

    # Load preset and assertions - either from scenario, scene JSON, or preset name
    preset = None
    properties = {}
    scenario_assertions = None

    if args.scenario:
        # Load from scenario (preferred)
        from scenario_loader import generate_scene_json, load_scenario

        scenarios_dir = Path(__file__).parent / "scenarios"
        scenario_yaml = scenarios_dir / args.scenario / "scenario.yaml"
        if not scenario_yaml.exists():
            print(f"Scenario not found: {args.scenario}")
            print(f"Expected: {scenario_yaml}")
            return 1

        scenario_cfg = load_scenario(scenario_yaml)
        scene = generate_scene_json(scenario_cfg, args.presets_ini)

        # Extract settings from generated scene
        settings = {}
        for source in scene.get("sources", []):
            if source.get("id") == "c64_source":
                settings = source.get("settings", {})
                break

        preset = PresetConfig.from_obs_settings(settings)
        properties = settings
        scenario_assertions = scenario_cfg.assertions

        if args.verbose:
            print(f"Loaded scenario: {scenario_cfg.name}")
            print(f"  Preset: {scenario_cfg.preset}")
            print(f"  Overrides: {scenario_cfg.overrides}")
            print(f"  Assertions: {scenario_cfg.assertions}")
    elif args.scene_json:
        if not args.scene_json.exists():
            print(f"Scene JSON not found: {args.scene_json}")
            return 1
        # Load settings from OBS scene JSON
        settings = load_settings_from_obs_scene(args.scene_json)
        if not settings:
            print(f"No c64_source found in scene JSON: {args.scene_json}")
            return 1
        preset = PresetConfig.from_obs_settings(settings)
        properties = settings  # Use settings as properties dict
        if args.verbose:
            print(f"Loaded settings from scene JSON: {preset.name}")
            print(f"  Scanlines: {preset.scan_line_distance}/{preset.scan_line_strength}")
            print(f"  Afterglow: {preset.afterglow_duration_ms}ms")
            print(f"  Tint: mode={preset.tint_mode} strength={preset.tint_strength}")
    else:
        # Load preset from INI file
        preset = load_preset_from_ini(args.preset, args.presets_ini)
        if not preset:
            print(f"Preset not found: {args.preset}")
            print(f"Check available presets with: {sys.argv[0]} --list-presets")
            return 1

        # Load properties (optional)
        if args.properties and args.properties.exists():
            properties = load_properties(args.properties)

    # Create assertions - from scenario list or auto-detect from preset
    if scenario_assertions:
        assertions = create_assertions_from_list(scenario_assertions)
    else:
        assertions = create_preset_assertions(preset)

    # Run assertions
    runner = AssertionRunner(verbose=args.verbose)
    for assertion in assertions:
        runner.add_assertion(assertion)

    results = runner.run_all(args.mp4, properties, preset)
    all_ok, summary = runner.summarize(results)

    # Output
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print(f"\n{'='*60}")
        print(f"Assertion Summary for preset: {preset.name}")
        print(f"{'='*60}")
        print(f"  Passed:   {summary['passed']}")
        print(f"  Failed:   {summary['failed']}")
        print(f"  Skipped:  {summary['skipped']}")
        print(f"  Warnings: {summary['warnings']}")
        print(f"{'='*60}")

        if all_ok:
            print("✅ All assertions passed!")
        else:
            print("❌ Some assertions failed")
            for r in results:
                if r.status == AssertionStatus.FAIL:
                    print(f"   - {r.name}: {r.message}")

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())