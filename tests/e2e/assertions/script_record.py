#!/usr/bin/env python3
"""
C64 Stream - Script Recording Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import json
import shutil
import subprocess
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class ScriptRecordAssertion(EffectAssertion):
    """Verify script-driven recording start/stop produces valid recordings."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_recordings": 2,
            "min_duration_seconds": 0.5,
            "min_size_bytes": 10240,
            "recording_time_window_s": 180.0,
        }
        super().__init__("Script Record", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        ffprobe = shutil.which("ffprobe")
        if not ffprobe:
            return AssertionResult(
                status=AssertionStatus.SKIP,
                name=self.name,
                message="ffprobe not available",
            )

        output_dir = mp4_path.parent
        recordings = self._find_recordings(output_dir, mp4_path)
        if not recordings:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="No recordings found in output directory",
                details={"output_dir": str(output_dir)},
            )

        min_recordings = int(self.thresholds.get("min_recordings", 1))
        min_duration = float(self.thresholds.get("min_duration_seconds", 0.5))
        min_size = int(self.thresholds.get("min_size_bytes", 10240))

        valid = []
        failures = []

        for path in recordings:
            try:
                size = path.stat().st_size
                if size < min_size:
                    failures.append({"path": str(path), "reason": "too_small", "size": size})
                    continue

                info = self._probe(ffprobe, path)
                if not info.get("has_video"):
                    failures.append({"path": str(path), "reason": "missing_video_stream"})
                    continue

                duration = info.get("duration", 0.0)
                if duration < min_duration:
                    failures.append({"path": str(path), "reason": "too_short", "duration": duration})
                    continue

                valid.append({"path": str(path), "duration": duration, "size": size})
            except Exception as exc:
                failures.append({"path": str(path), "reason": "probe_failed", "error": str(exc)})

        if len(valid) < min_recordings:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Expected at least {min_recordings} recordings, found {len(valid)}",
                details={"valid": valid, "failures": failures, "found": [str(p) for p in recordings]},
                metrics={"valid_recordings": float(len(valid))},
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=f"Found {len(valid)} valid recordings",
            details={"valid": valid, "failures": failures},
            metrics={"valid_recordings": float(len(valid))},
        )

    def _find_recordings(self, output_dir: Path, mp4_path: Path) -> list[Path]:
        candidates = []
        for ext in ("*.mp4", "*.hybrid_mp4"):
            candidates.extend(output_dir.glob(ext))

        if mp4_path.exists() and mp4_path not in candidates:
            candidates.append(mp4_path)

        if not candidates:
            return []

        reference_time = max(p.stat().st_mtime for p in candidates)
        window = float(self.thresholds.get("recording_time_window_s", 180.0))
        recent = [p for p in candidates if p.stat().st_mtime >= reference_time - window]
        if recent:
            candidates = recent

        return sorted(set(candidates), key=lambda p: p.stat().st_mtime, reverse=True)

    def _probe(self, ffprobe: str, path: Path) -> dict[str, Any]:
        cmd = [
            ffprobe,
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=codec_type,duration,nb_frames,avg_frame_rate",
            "-show_entries",
            "format=duration",
            "-of",
            "json",
            str(path),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        data = json.loads(result.stdout)

        duration = 0.0
        has_video = False

        streams = data.get("streams", [])
        if streams:
            stream = streams[0]
            has_video = stream.get("codec_type") == "video"
            if "duration" in stream:
                try:
                    duration = float(stream.get("duration") or 0.0)
                except (TypeError, ValueError):
                    duration = 0.0

        fmt = data.get("format", {})
        if duration <= 0.0:
            try:
                duration = float(fmt.get("duration") or 0.0)
            except (TypeError, ValueError):
                duration = 0.0

        return {"duration": duration, "has_video": has_video}
