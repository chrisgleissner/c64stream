#!/usr/bin/env python3
"""
C64 Stream - Record Network Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Verifies that the network.csv file was recorded correctly in the session folder.
The network.csv contains network packet events (video and audio UDP packets).
"""

import csv
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class RecordNetworkAssertion(EffectAssertion):
    """Verify network.csv recording exists and has valid content."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_video_packets": 100,
            "min_audio_packets": 50,
            "max_sequence_error_ratio": 0.05,  # 5% max sequence errors
        }
        super().__init__("Record Network", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        # Find network.csv in the session folder
        output_dir = mp4_path.parent
        network_csv = self._find_network_csv(output_dir)

        if network_csv is None:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="network.csv not found in session folder",
                details={"searched_dir": str(output_dir)},
            )

        try:
            video_packets = 0
            audio_packets = 0
            total_packets = 0
            max_frame_num = 0
            max_sequence_errors = 0
            total_jitter_us = 0
            jitter_count = 0

            with open(network_csv, "r", newline="") as f:
                reader = csv.DictReader(f)

                for row in reader:
                    packet_type = row.get("packet_type")
                    if not packet_type:
                        continue
                    packet_type = packet_type.strip()
                    total_packets += 1

                    if packet_type == "video":
                        video_packets += 1
                        frame_num_raw = row.get("frame_num")
                        if frame_num_raw is None or frame_num_raw == "":
                            frame_num_raw = 0
                        try:
                            frame_num = int(frame_num_raw)
                            max_frame_num = max(max_frame_num, frame_num)
                        except ValueError:
                            pass
                    elif packet_type == "audio":
                        audio_packets += 1

                    # Track sequence errors
                    seq_errors_raw = row.get("sequence_errors")
                    if seq_errors_raw is None or seq_errors_raw == "":
                        seq_errors_raw = 0
                    try:
                        seq_errors = int(seq_errors_raw)
                        max_sequence_errors = max(max_sequence_errors, seq_errors)
                    except ValueError:
                        pass

                    # Track jitter
                    jitter_raw = row.get("jitter_us")
                    if jitter_raw is None or jitter_raw == "":
                        jitter_raw = 0
                    try:
                        jitter = abs(int(jitter_raw))
                        total_jitter_us += jitter
                        jitter_count += 1
                    except ValueError:
                        pass

            avg_jitter_us = total_jitter_us / jitter_count if jitter_count > 0 else 0

            self.log(
                f"network.csv: {video_packets} video, {audio_packets} audio packets, "
                f"max frame {max_frame_num}, avg jitter {avg_jitter_us:.0f}us",
                verbose,
            )

            details = {
                "path": str(network_csv),
                "total_packets": total_packets,
                "video_packets": video_packets,
                "audio_packets": audio_packets,
                "max_frame_num": max_frame_num,
                "max_sequence_errors": max_sequence_errors,
                "avg_jitter_us": avg_jitter_us,
            }
            metrics = {
                "total_packets": float(total_packets),
                "video_packets": float(video_packets),
                "audio_packets": float(audio_packets),
                "max_frame_num": float(max_frame_num),
                "sequence_errors": float(max_sequence_errors),
                "avg_jitter_us": avg_jitter_us,
            }

            # Check minimum video packets
            min_video = int(self.thresholds["min_video_packets"])
            if video_packets < min_video:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Too few video packets: {video_packets} (min {min_video})",
                    details=details,
                    metrics=metrics,
                )

            # Check minimum audio packets
            min_audio = int(self.thresholds["min_audio_packets"])
            if audio_packets < min_audio:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Too few audio packets: {audio_packets} (min {min_audio})",
                    details=details,
                    metrics=metrics,
                )

            # Check sequence errors ratio
            max_error_ratio = float(self.thresholds["max_sequence_error_ratio"])
            if total_packets > 0:
                error_ratio = max_sequence_errors / total_packets
                if error_ratio > max_error_ratio:
                    return AssertionResult(
                        status=AssertionStatus.WARNING,
                        name=self.name,
                        message=f"High sequence error rate: {error_ratio*100:.1f}%",
                        details=details,
                        metrics=metrics,
                    )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"network.csv OK: {video_packets} video, {audio_packets} audio packets",
                details=details,
                metrics=metrics,
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Error reading network.csv: {e}",
                details={"path": str(network_csv)},
            )

    def _find_network_csv(self, output_dir: Path) -> Optional[Path]:
        """Find network.csv in output directory or session subdirectories."""
        # Check directly in output dir
        direct = output_dir / "network.csv"
        if direct.exists():
            return direct

        # Check in session_* subdirectories of output dir
        for subdir in output_dir.glob("session_*"):
            if subdir.is_dir():
                csv_file = subdir / "network.csv"
                if csv_file.exists():
                    return csv_file

        # Check in plugin's default recording folder
        plugin_recordings = Path.home() / "Documents" / "obs-studio" / "c64stream" / "recordings"
        if plugin_recordings.exists():
            # Find most recent session folder
            sessions = sorted(plugin_recordings.glob("session_*"), key=lambda p: p.stat().st_mtime, reverse=True)
            for session in sessions:
                csv_file = session / "network.csv"
                if csv_file.exists():
                    return csv_file

        return None
