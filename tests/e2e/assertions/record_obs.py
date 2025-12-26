#!/usr/bin/env python3
"""
C64 Stream - Record OBS Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Verifies that the obs.csv file was recorded correctly in the session folder.
The obs.csv contains OBS timing events (video and audio processing events).
"""

import csv
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class RecordObsAssertion(EffectAssertion):
    """Verify obs.csv recording exists and has valid content."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_video_events": 30,
            "min_audio_events": 10,
        }
        super().__init__("Record OBS", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        # Find obs.csv in the session folder
        output_dir = mp4_path.parent
        obs_csv = self._find_obs_csv(output_dir)

        if obs_csv is None:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="obs.csv not found in session folder",
                details={"searched_dir": str(output_dir)},
            )

        try:
            video_events = 0
            audio_events = 0
            total_events = 0
            max_frame_num = 0
            total_data_size = 0

            with open(obs_csv, "r", newline="") as f:
                reader = csv.DictReader(f)

                for row in reader:
                    total_events += 1
                    event_type = row.get("event_type", "").strip()

                    if event_type == "video":
                        video_events += 1
                        try:
                            frame_num = int(row.get("frame_num", 0))
                            max_frame_num = max(max_frame_num, frame_num)
                        except ValueError:
                            pass
                    elif event_type == "audio":
                        audio_events += 1

                    try:
                        total_data_size += int(row.get("data_size_bytes", 0))
                    except ValueError:
                        pass

            self.log(f"obs.csv: {video_events} video, {audio_events} audio events, max frame {max_frame_num}", verbose)

            details = {
                "path": str(obs_csv),
                "total_events": total_events,
                "video_events": video_events,
                "audio_events": audio_events,
                "max_frame_num": max_frame_num,
                "total_data_size_bytes": total_data_size,
            }
            metrics = {
                "total_events": float(total_events),
                "video_events": float(video_events),
                "audio_events": float(audio_events),
                "max_frame_num": float(max_frame_num),
            }

            # Check minimum video events
            min_video = int(self.thresholds["min_video_events"])
            if video_events < min_video:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Too few video events: {video_events} (min {min_video})",
                    details=details,
                    metrics=metrics,
                )

            # Check minimum audio events
            min_audio = int(self.thresholds["min_audio_events"])
            if audio_events < min_audio:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Too few audio events: {audio_events} (min {min_audio})",
                    details=details,
                    metrics=metrics,
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"obs.csv OK: {video_events} video, {audio_events} audio events",
                details=details,
                metrics=metrics,
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Error reading obs.csv: {e}",
                details={"path": str(obs_csv)},
            )

    def _find_obs_csv(self, output_dir: Path) -> Optional[Path]:
        """Find obs.csv in output directory or session subdirectories."""
        # Check directly in output dir
        direct = output_dir / "obs.csv"
        if direct.exists():
            return direct

        # Check in session_* subdirectories of output dir
        for subdir in output_dir.glob("session_*"):
            if subdir.is_dir():
                csv_file = subdir / "obs.csv"
                if csv_file.exists():
                    return csv_file

        # Check in plugin's default recording folder
        plugin_recordings = Path.home() / "Documents" / "obs-studio" / "c64stream" / "recordings"
        if plugin_recordings.exists():
            # Find most recent session folder
            sessions = sorted(plugin_recordings.glob("session_*"), key=lambda p: p.stat().st_mtime, reverse=True)
            for session in sessions:
                csv_file = session / "obs.csv"
                if csv_file.exists():
                    return csv_file

        return None
