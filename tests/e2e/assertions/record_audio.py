#!/usr/bin/env python3
"""
C64 Stream - Record Audio Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Verifies that the audio.wav file was recorded correctly in the session folder.
"""

import json
import subprocess
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class RecordAudioAssertion(EffectAssertion):
    """Verify audio.wav recording exists and has valid content."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_duration_seconds": 1.0,
            "expected_sample_rate": 48000,
            "expected_channels": 2,
        }
        super().__init__("Record Audio", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        # Find audio.wav in the session folder (same directory as output or parent)
        output_dir = mp4_path.parent
        audio_wav = self._find_audio_wav(output_dir)

        if audio_wav is None:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="audio.wav not found in session folder",
                details={"searched_dir": str(output_dir)},
            )

        try:
            # Analyze audio file with ffprobe
            cmd = [
                "ffprobe",
                "-v",
                "error",
                "-show_entries",
                "format=duration:stream=sample_rate,channels,codec_name",
                "-of",
                "json",
                str(audio_wav),
            ]
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            data = json.loads(result.stdout)

            if not data.get("streams"):
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="No audio stream found in audio.wav",
                    details={"path": str(audio_wav)},
                )

            stream = data["streams"][0]
            fmt = data.get("format", {})

            sample_rate = int(stream.get("sample_rate", 0))
            channels = int(stream.get("channels", 0))
            codec = stream.get("codec_name", "unknown")
            duration = float(fmt.get("duration", 0))

            self.log(f"audio.wav: {duration:.2f}s, {sample_rate}Hz, {channels}ch, {codec}", verbose)

            details = {
                "path": str(audio_wav),
                "duration_seconds": duration,
                "sample_rate": sample_rate,
                "channels": channels,
                "codec": codec,
            }
            metrics = {
                "duration_seconds": duration,
                "sample_rate": float(sample_rate),
                "channels": float(channels),
            }

            # Check minimum duration
            min_duration = float(self.thresholds["min_duration_seconds"])
            if duration < min_duration:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"audio.wav too short: {duration:.2f}s (min {min_duration}s)",
                    details=details,
                    metrics=metrics,
                )

            # Check sample rate
            expected_rate = int(self.thresholds["expected_sample_rate"])
            if sample_rate != expected_rate:
                return AssertionResult(
                    status=AssertionStatus.WARNING,
                    name=self.name,
                    message=f"Unexpected sample rate: {sample_rate}Hz (expected {expected_rate}Hz)",
                    details=details,
                    metrics=metrics,
                )

            # Check channels
            expected_channels = int(self.thresholds["expected_channels"])
            if channels != expected_channels:
                return AssertionResult(
                    status=AssertionStatus.WARNING,
                    name=self.name,
                    message=f"Unexpected channels: {channels} (expected {expected_channels})",
                    details=details,
                    metrics=metrics,
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"audio.wav OK: {duration:.2f}s, {sample_rate}Hz, {channels}ch",
                details=details,
                metrics=metrics,
            )

        except subprocess.CalledProcessError as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Failed to analyze audio.wav: {e}",
                details={"path": str(audio_wav)},
            )
        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Error verifying audio.wav: {e}",
                details={"path": str(audio_wav)},
            )

    def _find_audio_wav(self, output_dir: Path) -> Optional[Path]:
        """Find audio.wav in output directory or session subdirectories."""
        # Check directly in output dir
        direct = output_dir / "audio.wav"
        if direct.exists():
            return direct

        # Check in session_* subdirectories of output dir
        for subdir in output_dir.glob("session_*"):
            if subdir.is_dir():
                wav = subdir / "audio.wav"
                if wav.exists():
                    return wav

        # Check in plugin's default recording folder
        plugin_recordings = Path.home() / "Documents" / "obs-studio" / "c64stream" / "recordings"
        if plugin_recordings.exists():
            # A shared plugin recording folder can contain WAVs from earlier
            # scenarios.  When the current replay manifest exists, reject any
            # session that predates it rather than letting a stale good WAV
            # make the current recording look healthy.
            manifest = output_dir / "audio_manifest.csv"
            earliest_current_wav_mtime = (
                manifest.stat().st_mtime - 5.0 if manifest.exists() else None
            )

            # Find most recent session folder created by this run.
            sessions = sorted(plugin_recordings.glob("session_*"), key=lambda p: p.stat().st_mtime, reverse=True)
            for session in sessions:
                wav = session / "audio.wav"
                if wav.exists() and (
                    earliest_current_wav_mtime is None
                    or wav.stat().st_mtime >= earliest_current_wav_mtime
                ):
                    return wav

        return None
