"""Follow device on a mixed fleet: every frame in its own device's palette.

Device A streams the solid pattern and reports its palette in the video
stream; device B streams the dot pattern and is polled. The border is VIC
colour 14 in both patterns, and the two palettes give colour 14 clearly
different colours, so each sampled frame shows which device sent it (by its
pattern) and which palette it was rendered in (by its border). A frame of
one device in the other device's palette is a failure; the default palette
is allowed (before a device's palette is known, or after PALETTE "Default").
"""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Any, Optional

import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig

PALETTES_DIR = Path(__file__).resolve().parents[3] / "data" / "palettes"


def _colour_14(name: str) -> tuple[int, int, int]:
    colours = []
    for line in (PALETTES_DIR / f"{name}.vpl").read_text().splitlines():
        fields = line.split("#", 1)[0].split()
        if len(fields) >= 3:
            colours.append(tuple(int(value, 16) for value in fields[:3]))
    return colours[14]


class PaletteDevicePairsAssertion(EffectAssertion):
    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {"sample_fps": 10.0, "max_colour_distance": 45.0, "min_frames_per_device": 20.0}
        super().__init__("Palette Device Pairs", {**defaults, **(thresholds or {})})

    def verify(self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig,
               verbose: bool = False) -> AssertionResult:
        if not mp4_path.exists():
            return AssertionResult(AssertionStatus.FAIL, self.name, "Recording file not found")
        palettes = {
            "a": _colour_14(str(properties.get("palette_device_a", "night"))),
            "b": _colour_14(str(properties.get("palette_device_b", "monochrome"))),
            "default": _colour_14("default"),
        }
        try:
            probe = subprocess.run(
                ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                 "-of", "csv=p=0", str(mp4_path)], capture_output=True, text=True, check=True
            ).stdout.strip()
            width, height = (int(value) for value in probe.split(","))
            frame_bytes = width * height * 3
            proc = subprocess.Popen(
                ["ffmpeg", "-v", "error", "-i", str(mp4_path), "-vf", f"fps={self.thresholds['sample_fps']}",
                 "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE
            )
            counts: dict[str, int] = {}
            wrong: list[str] = []
            index = 0
            try:
                while True:
                    frame = proc.stdout.read(frame_bytes)
                    if len(frame) != frame_bytes:
                        break
                    image = np.frombuffer(frame, dtype=np.uint8).reshape((height, width, 3))
                    index += 1
                    device = self._device(image)
                    palette = self._palette(image, palettes)
                    if device is None or palette is None:
                        continue
                    key = f"{device}:{palette}"
                    counts[key] = counts.get(key, 0) + 1
                    if palette not in (device, "default"):
                        wrong.append(f"{index / self.thresholds['sample_fps']:.1f}s {key}")
            finally:
                proc.stdout.close()
                proc.wait(timeout=10)
            details = {"pairs": counts, "wrong": wrong[:10]}
            self.log(f"device:palette pairs {counts}", verbose)
            minimum = int(self.thresholds["min_frames_per_device"])
            if wrong:
                return AssertionResult(AssertionStatus.FAIL, self.name,
                                       f"{len(wrong)} frames in the other device's palette: {wrong[:5]}",
                                       details=details)
            if counts.get("a:a", 0) < minimum or counts.get("b:b", 0) < minimum:
                return AssertionResult(AssertionStatus.FAIL, self.name,
                                       f"Too few frames of each device in its own palette: {counts}",
                                       details=details)
            return AssertionResult(AssertionStatus.PASS, self.name,
                                   f"Every frame in its own device's palette: {counts}", details=details)
        except Exception as exc:
            return AssertionResult(AssertionStatus.FAIL, self.name, f"Palette pair analysis failed: {exc}")

    @staticmethod
    def _device(image: np.ndarray) -> Optional[str]:
        height, width = image.shape[:2]
        center = image[height // 4:height * 3 // 4, width // 4:width * 3 // 4]
        bright_ratio = float(np.mean(np.max(center, axis=2) > 60))
        if bright_ratio > 0.70:
            return "a"  # solid field
        if 0.001 < bright_ratio < 0.12:
            return "b"  # sparse dots on black
        return None

    def _palette(self, image: np.ndarray, palettes: dict[str, tuple[int, int, int]]) -> Optional[str]:
        """Palette of the border: sampled left and right of the screen,
        midway down, away from the corner diagnostics."""
        height, width = image.shape[:2]
        rows = slice(height * 2 // 5, height * 3 // 5)
        # The output may be letterboxed: take the outermost non-black columns.
        band = image[rows]
        non_black = np.where(np.max(band, axis=(0, 2)) > 24)[0]
        if non_black.size < 20:
            return None
        left, right = int(non_black[0]), int(non_black[-1])
        border_width = max(2, (right - left) // 40)
        samples = np.concatenate([band[:, left + 1:left + 1 + border_width].reshape(-1, 3),
                                  band[:, right - border_width:right].reshape(-1, 3)])
        observed = np.median(samples, axis=0)
        best, distance = None, float("inf")
        for name, rgb in palettes.items():
            d = float(np.max(np.abs(observed - np.array(rgb))))
            if d < distance:
                best, distance = name, d
        return best if distance <= self.thresholds["max_colour_distance"] else None
