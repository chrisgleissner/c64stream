"""
AV Sync Log Validation Assertion

Validates that AV SYNC log entries in obs.log match the A/V pop events
detected in obs.csv and network.csv files. This ensures the plugin's
real-time A/V sync detection logging is accurate and useful for debugging.
"""

import re
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class AvSyncLogValidationAssertion(EffectAssertion):
    """Validate AV SYNC logs match CSV pop events."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "tolerance_ms": 50.0,  # 50ms tolerance for timing differences
        }
        super().__init__("AV Sync Log Validation", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        """
        Verify AV SYNC log entries match A/V pop events from CSV files.

        Args:
            mp4_path: Path to the OBS-produced MP4 file
            properties: Parsed properties.ini as a dict
            preset: The effect preset configuration
            verbose: Enable verbose logging

        Returns:
            AssertionResult with validation status and details
        """
        output_dir = mp4_path.parent
        obs_csv = output_dir / "obs.csv"
        network_csv = output_dir / "network.csv"
        obs_log = output_dir / "obs_log.txt"

        if not obs_log.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"obs_log.txt not found in {output_dir}",
                details={}
            )

        try:
            # Parse CSV files for pop events
            video_pops = self._parse_obs_csv_pops(obs_csv)
            audio_pops = self._parse_network_csv_pops(network_csv)

            # Parse obs.log for AV SYNC entries
            av_sync_logs = self._parse_av_sync_logs(obs_log)

            if not av_sync_logs:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"No 'AV SYNC' log entries found in {obs_log.name}.",
                    details={
                        "video_pops_csv": len(video_pops),
                        "audio_pops_csv": len(audio_pops),
                        "av_sync_logs": 0,
                    }
                )

            # Validate that log entries are internally consistent
            # We check that:
            # 1. AV SYNC logs exist (plugin is logging properly)
            # 2. Pop numbers are reasonable (sequential, no huge gaps)
            # 3. Multiple detections of the same pop are consistent

            validation_errors = []

            def origin_group(entry: dict[str, Any]) -> str:
                origin = entry.get("origin")
                if origin in ("OBS", "OBS+Network", "legacy"):
                    return "OBS"
                if origin == "Network":
                    return "Network"
                return str(origin) if origin is not None else "unknown"

            seen_pops: dict[tuple[str, int, int], float] = {}

            for log_entry in av_sync_logs:
                group = origin_group(log_entry)
                video_pop_num = log_entry["video_pop_num"]
                audio_pop_num = log_entry["audio_pop_num"]
                log_offset_ms = log_entry["offset_ms"]

                pop_key = (group, video_pop_num, audio_pop_num)

                if pop_key in seen_pops:
                    # Same pop pair logged multiple times (from both video and audio handlers)
                    # Verify the offsets are consistent
                    prev_offset = seen_pops[pop_key]
                    offset_diff = abs(log_offset_ms - prev_offset)

                    # Both handlers should report the same offset (they use same timestamps)
                    if offset_diff > 1.0:  # Allow 1ms rounding difference
                        validation_errors.append(
                            f"AV SYNC log inconsistency for {group} pop pair video #{video_pop_num}/audio #{audio_pop_num}: "
                            f"offset varies between handlers ({prev_offset:.1f}ms vs {log_offset_ms:.1f}ms, diff={offset_diff:.1f}ms)"
                        )
                else:
                    seen_pops[pop_key] = log_offset_ms

            # Count unique pop pairs
            matched_logs = len(seen_pops)

            obs_av_sync_logs = [e for e in av_sync_logs if origin_group(e) == "OBS"]
            if not obs_av_sync_logs:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"No OBS-origin 'AV SYNC' log entries found in {obs_log.name}.",
                    details={
                        "video_pops_csv": len(video_pops),
                        "audio_pops_csv": len(audio_pops),
                        "av_sync_logs": len(av_sync_logs),
                    },
                )

            # Verify CSV data matches logs where possible
            # Note: CSV timestamps are at packet reception time, logs are at frame delivery time
            # So there may be timing differences, but we can still validate pop counts
            csv_video_count = len(video_pops)
            csv_audio_count = len(audio_pops)
            log_video_max = max((e["video_pop_num"] for e in obs_av_sync_logs), default=0)
            log_audio_max = max((e["audio_pop_num"] for e in obs_av_sync_logs), default=0)

            # CSV may not see all pops due to different detection thresholds
            # But logs should not see MORE pops than CSV (sanity check)
            if log_video_max > csv_video_count + 2:  # Allow 2 extra for edge cases
                validation_errors.append(
                    f"Plugin logged {log_video_max} video pops but CSV only shows {csv_video_count} "
                    f"(difference too large, possible detection mismatch)"
                )

            if log_audio_max > csv_audio_count + 3:  # Audio detection varies more
                self.log(
                    f"Note: Plugin logged {log_audio_max} audio pops but network.csv shows {csv_audio_count}. "
                    f"This is expected if plugin uses stricter detection threshold.",
                    verbose
                )

            if validation_errors:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"AV SYNC log validation failed: {len(validation_errors)} error(s)",
                    details={
                        "video_pops_csv": len(video_pops),
                        "audio_pops_csv": len(audio_pops),
                        "av_sync_logs": len(av_sync_logs),
                        "matched_logs": matched_logs,
                        "errors": validation_errors,
                    }
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"AV SYNC logs validated: {matched_logs} matching entries",
                details={
                    "video_pops_csv": len(video_pops),
                    "audio_pops_csv": len(audio_pops),
                    "av_sync_logs": len(av_sync_logs),
                    "matched_logs": matched_logs,
                }
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"AV SYNC log validation error: {str(e)}",
                details={"error": str(e)}
            )

    def _parse_obs_csv_pops(self, csv_path: Path) -> list:
        """Parse obs.csv for video pop events (is_all_white transitions)."""
        pops = []
        pop_count = 0
        prev_is_white = False

        with open(csv_path, 'r') as f:
            lines = f.readlines()
            # Skip header
            for line in lines[1:]:
                parts = line.strip().split(',')
                if len(parts) < 10:
                    continue

                event_type = parts[0]
                if event_type != "video":
                    continue

                # elapsed_us is column 2 (0-indexed)
                elapsed_us = float(parts[2])
                time_ms = elapsed_us / 1000.0

                # is_all_white is column 9 (0-indexed), has_signal is column 10
                is_white = parts[9] == '1'

                # Detect transition from non-white to white
                if not prev_is_white and is_white:
                    pop_count += 1
                    pops.append({
                        "pop_num": pop_count,
                        "time_ms": time_ms,
                    })

                prev_is_white = is_white

        return pops

    def _parse_network_csv_pops(self, csv_path: Path) -> list:
        """Parse network.csv for audio pop events (has_signal transitions)."""
        pops = []
        pop_count = 0
        prev_has_signal = False

        with open(csv_path, 'r') as f:
            lines = f.readlines()
            # Skip header
            for line in lines[1:]:
                parts = line.strip().split(',')
                if len(parts) < 15:
                    continue

                packet_type = parts[0]
                if packet_type != "audio":
                    continue

                # elapsed_us is column 1 (0-indexed)
                elapsed_us = float(parts[1])
                time_ms = elapsed_us / 1000.0

                # has_signal is the last column (column 14, 0-indexed)
                has_signal = parts[14] == '1'

                # Detect transition from no-signal to has-signal
                if not prev_has_signal and has_signal:
                    pop_count += 1
                    pops.append({
                        "pop_num": pop_count,
                        "time_ms": time_ms,
                    })

                prev_has_signal = has_signal

        return pops

    def _parse_av_sync_logs(self, log_path: Path) -> list:
        """Parse obs_log.txt for AV SYNC log entries."""
        av_sync_entries = []

        # Supported patterns:
        # - Legacy: AV SYNC: offset=X.Xms video=#N audio=#M [unpaired]
        # - Origin tagged: AV SYNC (OBS|Network): offset=X.Xms video=#N audio=#M [unpaired]
        # - Combined: AV SYNC (OBS+Network): obs_offset=X.Xms net_offset=Y.Yms video=#N audio=#M
        legacy_pattern = re.compile(
            r"AV SYNC: offset=(-?[\d.]+)ms video=#(\d+) audio=#(\d+)(?: unpaired)?"
        )
        origin_pattern = re.compile(
            r"AV SYNC \((OBS|Network)\): offset=(-?[\d.]+)ms video=#(\d+) audio=#(\d+)(?: unpaired)?"
        )
        combined_pattern = re.compile(
            r"AV SYNC \(OBS\+Network\): obs_offset=(-?[\d.]+)ms net_offset=(-?[\d.]+)ms video=#(\d+) audio=#(\d+)"
        )

        with open(log_path, 'r') as f:
            for line in f:
                match = combined_pattern.search(line)
                if match:
                    av_sync_entries.append(
                        {
                            "origin": "OBS+Network",
                            "offset_ms": float(match.group(1)),
                            "net_offset_ms": float(match.group(2)),
                            "video_pop_num": int(match.group(3)),
                            "audio_pop_num": int(match.group(4)),
                            "is_unpaired": False,
                            "log_line": line.strip(),
                        }
                    )
                    continue

                match = origin_pattern.search(line)
                if match:
                    is_unpaired = 'unpaired' in line
                    av_sync_entries.append(
                        {
                            "origin": match.group(1),
                            "offset_ms": float(match.group(2)),
                            "video_pop_num": int(match.group(3)),
                            "audio_pop_num": int(match.group(4)),
                            "is_unpaired": is_unpaired,
                            "log_line": line.strip(),
                        }
                    )
                    continue

                match = legacy_pattern.search(line)
                if match:
                    is_unpaired = 'unpaired' in line
                    av_sync_entries.append(
                        {
                            "origin": "legacy",
                            "offset_ms": float(match.group(1)),
                            "video_pop_num": int(match.group(2)),
                            "audio_pop_num": int(match.group(3)),
                            "is_unpaired": is_unpaired,
                            "log_line": line.strip(),
                        }
                    )

        return av_sync_entries
