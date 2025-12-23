#!/usr/bin/env python3
"""
C64 Stream - Audio Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import json
import subprocess
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


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
