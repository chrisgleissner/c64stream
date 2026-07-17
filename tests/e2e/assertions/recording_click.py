#!/usr/bin/env python3
"""
C64 Stream - OBS Recording Click Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Click analysis of the audio track inside the OBS recording (C64CLK-006).
Audio is extracted with the already-required ffmpeg as raw s16le PCM and
scanned with the same detector as the plugin-WAV assertion. OBS resamples
47940/47983 Hz -> 48000 Hz and the AAC codec rings slightly at real
discontinuities, so the default threshold is higher than the WAV one.
"""

import subprocess
import tempfile
from pathlib import Path
from typing import Any, Optional

from .audio_pcm import deinterleave, scan_clicks
from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig

EXTRACT_SAMPLE_RATE = 48000
EXTRACT_CHANNELS = 2


class RecordingClickAssertion(EffectAssertion):
    """Verify the OBS recording's audio track is free of clicks."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "click_threshold": 8000,
            "max_clicks": 0,
            "trim_ms": 100.0,
        }
        super().__init__("Recording Click", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        if not mp4_path.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"OBS recording not found: {mp4_path}",
            )

        try:
            with tempfile.NamedTemporaryFile(suffix=".pcm") as tmp:
                cmd = [
                    "ffmpeg", "-y", "-v", "error",
                    "-i", str(mp4_path),
                    "-vn",
                    "-f", "s16le",
                    "-acodec", "pcm_s16le",
                    "-ar", str(EXTRACT_SAMPLE_RATE),
                    "-ac", str(EXTRACT_CHANNELS),
                    tmp.name,
                ]
                subprocess.run(cmd, check=True, capture_output=True, text=True)
                raw = Path(tmp.name).read_bytes()
        except subprocess.CalledProcessError as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"ffmpeg audio extraction failed: {e.stderr}",
                details={"path": str(mp4_path)},
            )

        if not raw:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="OBS recording contains no audio samples",
                details={"path": str(mp4_path)},
            )

        channels = deinterleave(raw, EXTRACT_CHANNELS)
        report = scan_clicks(
            channels,
            float(EXTRACT_SAMPLE_RATE),
            click_threshold=int(self.thresholds["click_threshold"]),
            trim_ms=float(self.thresholds["trim_ms"]),
        )

        details: dict[str, Any] = {
            "path": str(mp4_path),
            "duration_seconds": round(report.duration_seconds, 3),
            "active_start_s": round(report.active_start_s, 3),
            "active_end_s": round(report.active_end_s, 3),
            "click_count": report.click_count,
            "max_delta": report.max_delta,
            "click_times_s": report.click_times_s,
        }
        metrics = {
            "click_count": float(report.click_count),
            "max_delta": float(report.max_delta),
        }

        self.log(
            f"clicks={report.click_count} max_delta={report.max_delta} "
            f"active={report.active_start_s:.2f}s..{report.active_end_s:.2f}s",
            verbose,
        )

        if report.active_end_s <= report.active_start_s:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="No active audio region found in OBS recording",
                details=details,
                metrics=metrics,
            )

        max_clicks = int(self.thresholds["max_clicks"])
        if report.click_count > max_clicks:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    f"{report.click_count} click(s) in OBS recording audio "
                    f"(budget {max_clicks}, max |delta|={report.max_delta})"
                ),
                details=details,
                metrics=metrics,
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=(
                f"OBS recording audio clean: {report.click_count} clicks "
                f"(max |delta|={report.max_delta})"
            ),
            details=details,
            metrics=metrics,
        )
