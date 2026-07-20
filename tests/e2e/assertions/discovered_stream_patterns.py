"""Evidence that a discovery/switch recording contains both mock devices."""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Any, Optional

import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class DiscoveredStreamPatternsAssertion(EffectAssertion):
    """Require the solid C64U and sparse-dot U64 patterns in one recording."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {"sample_fps": 3.0, "min_solid_frames": 2.0, "min_dots_frames": 2.0}
        super().__init__("Discovered Stream Patterns", {**defaults, **(thresholds or {})})

    def verify(self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig,
               verbose: bool = False) -> AssertionResult:
        if not mp4_path.exists():
            return AssertionResult(AssertionStatus.FAIL, self.name, "Recording file not found")
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
            solid = dots = samples = 0
            try:
                while True:
                    frame = proc.stdout.read(frame_bytes)
                    if len(frame) != frame_bytes:
                        break
                    image = np.frombuffer(frame, dtype=np.uint8).reshape((height, width, 3))
                    # The central field excludes the fixed diagnostics in the corners.
                    center = image[height // 4:height * 3 // 4, width // 4:width * 3 // 4]
                    bright_ratio = float(np.mean(np.max(center, axis=2) > 110))
                    samples += 1
                    if bright_ratio > 0.70:
                        solid += 1
                    elif 0.001 < bright_ratio < 0.12:
                        dots += 1
            finally:
                proc.stdout.close()
                proc.wait(timeout=10)
            details = {"samples": samples, "solid_frames": solid, "dots_frames": dots}
            if solid < int(self.thresholds["min_solid_frames"]) or dots < int(self.thresholds["min_dots_frames"]):
                return AssertionResult(AssertionStatus.FAIL, self.name,
                                       "Recording did not contain both discovered-device patterns", details=details)
            return AssertionResult(AssertionStatus.PASS, self.name,
                                   "Recorded both C64U solid and U64 dot patterns", details=details)
        except Exception as exc:
            return AssertionResult(AssertionStatus.FAIL, self.name, f"Pattern analysis failed: {exc}")
