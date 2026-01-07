#!/usr/bin/env python3
"""
C64 Stream - A/V Pop Delta Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Validates A/V pop timing deltas using obs.csv and network.csv debug columns.
"""

import csv
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class AvPopDeltaAssertion(EffectAssertion):
    """Verify that audio and video pops are closely aligned in CSV logs."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "max_delta_ms": 30.0,
            "min_pop_events": 2,
        }
        super().__init__("A/V Pop Delta", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        output_dir = mp4_path.parent
        obs_csv = self._find_csv(output_dir, "obs.csv")
        network_csv = self._find_csv(output_dir, "network.csv")

        if obs_csv is None or network_csv is None:
            missing = []
            if obs_csv is None:
                missing.append("obs.csv")
            if network_csv is None:
                missing.append("network.csv")
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Missing CSV(s): {', '.join(missing)}",
                details={"searched_dir": str(output_dir)},
            )

        net_result = self._analyze_network_csv(network_csv)
        if net_result is None:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="network.csv missing debug columns or pop events",
                details={"path": str(network_csv)},
            )

        obs_result = self._analyze_obs_csv(obs_csv)
        if obs_result is None:
            pop_frames = self._extract_network_pop_frames(network_csv)
            if pop_frames:
                obs_result = self._analyze_obs_csv_with_frames(obs_csv, pop_frames)

        if obs_result is None:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="obs.csv missing debug columns or pop events",
                details={"path": str(obs_csv)},
            )

        max_delta_ms = float(self.thresholds["max_delta_ms"])
        min_pop_events = int(self.thresholds["min_pop_events"])

        if obs_result["pop_count"] < min_pop_events or net_result["pop_count"] < min_pop_events:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Too few pop events (obs={obs_result['pop_count']}, network={net_result['pop_count']})",
                details={"obs": obs_result, "network": net_result},
            )

        if obs_result["max_delta_ms"] > max_delta_ms or net_result["max_delta_ms"] > max_delta_ms:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    f"Pop delta too large: obs={obs_result['max_delta_ms']:.2f}ms, "
                    f"network={net_result['max_delta_ms']:.2f}ms (max {max_delta_ms:.2f}ms)"
                ),
                details={"obs": obs_result, "network": net_result},
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=(
                f"Pop delta OK: obs={obs_result['max_delta_ms']:.2f}ms, "
                f"network={net_result['max_delta_ms']:.2f}ms"
            ),
            details={"obs": obs_result, "network": net_result},
            metrics={
                "obs_max_delta_ms": obs_result["max_delta_ms"],
                "network_max_delta_ms": net_result["max_delta_ms"],
            },
        )

    def _analyze_obs_csv(self, csv_path: Path) -> Optional[dict[str, float]]:
        try:
            with open(csv_path, "r", newline="") as f:
                reader = csv.DictReader(f)
                if not reader.fieldnames:
                    return None
                if "is_all_white" not in reader.fieldnames or "has_signal" not in reader.fieldnames:
                    return None

                video_times = []
                audio_times = []
                for row in reader:
                    event_type = (row.get("event_type") or "").strip()
                    elapsed_us = row.get("elapsed_us")
                    if not elapsed_us:
                        continue
                    try:
                        ts_us = int(float(elapsed_us))
                    except ValueError:
                        continue
                    if event_type == "video":
                        if (row.get("is_all_white") or "") == "1":
                            video_times.append(ts_us)
                    elif event_type == "audio" and (row.get("has_signal") or "") == "1":
                        audio_times.append(ts_us)

                audio_times = self._collapse_pop_times(audio_times, 100000)

            return self._compute_deltas(video_times, audio_times)
        except Exception:
            return None

    def _analyze_obs_csv_with_frames(self, csv_path: Path, pop_frames: set[int]) -> Optional[dict[str, float]]:
        try:
            with open(csv_path, "r", newline="") as f:
                reader = csv.DictReader(f)
                if not reader.fieldnames:
                    return None
                if "has_signal" not in reader.fieldnames:
                    return None

                video_rows = []
                audio_times = []
                for row in reader:
                    event_type = (row.get("event_type") or "").strip()
                    elapsed_us = row.get("elapsed_us")
                    if not elapsed_us:
                        continue
                    try:
                        ts_us = int(float(elapsed_us))
                    except ValueError:
                        continue
                    if event_type == "video":
                        frame_num = row.get("frame_num")
                        if not frame_num:
                            continue
                        try:
                            frame_id = int(frame_num)
                        except ValueError:
                            continue
                        video_rows.append((frame_id, ts_us))
                    elif event_type == "audio" and (row.get("has_signal") or "") == "1":
                        audio_times.append(ts_us)

                audio_times = self._collapse_pop_times(audio_times, 100000)

                obs_frames = {frame_id for frame_id, _ in video_rows}
                match_zero = sum(1 for frame_id in pop_frames if frame_id in obs_frames)
                match_one = sum(1 for frame_id in pop_frames if (frame_id + 1) in obs_frames)
                offset = 1 if match_one > match_zero else 0
                target_frames = {frame_id + offset for frame_id in pop_frames}
                video_times = [ts_us for frame_id, ts_us in video_rows if frame_id in target_frames]

            return self._compute_deltas(video_times, audio_times)
        except Exception:
            return None

    def _analyze_network_csv(self, csv_path: Path) -> Optional[dict[str, float]]:
        try:
            with open(csv_path, "r", newline="") as f:
                reader = csv.DictReader(f)
                if not reader.fieldnames:
                    return None
                if "is_all_white" not in reader.fieldnames or "has_signal" not in reader.fieldnames:
                    return None

                video_times = []
                video_times_by_frame = {}
                audio_times = []
                for row in reader:
                    packet_type = (row.get("packet_type") or "").strip()
                    elapsed_us = row.get("elapsed_us")
                    if not elapsed_us:
                        continue
                    try:
                        ts_us = int(float(elapsed_us))
                    except ValueError:
                        continue
                    if packet_type == "video":
                        if (row.get("is_all_white") or "") == "1":
                            frame_num = row.get("frame_num")
                            if frame_num:
                                try:
                                    frame_id = int(frame_num)
                                    existing = video_times_by_frame.get(frame_id)
                                    if existing is None or ts_us < existing:
                                        video_times_by_frame[frame_id] = ts_us
                                except ValueError:
                                    pass
                            else:
                                video_times.append(ts_us)
                    elif packet_type == "audio" and (row.get("has_signal") or "") == "1":
                        audio_times.append(ts_us)

                if video_times_by_frame:
                    video_times.extend(video_times_by_frame.values())
                audio_times = self._collapse_pop_times(audio_times, 100000)

            return self._compute_deltas(video_times, audio_times)
        except Exception:
            return None

    def _extract_network_pop_frames(self, csv_path: Path) -> set[int]:
        pop_frames = set()
        try:
            with open(csv_path, "r", newline="") as f:
                reader = csv.DictReader(f)
                if not reader.fieldnames:
                    return pop_frames
                if "is_all_white" not in reader.fieldnames:
                    return pop_frames

                for row in reader:
                    if (row.get("packet_type") or "").strip() != "video":
                        continue
                    if (row.get("is_all_white") or "") != "1":
                        continue
                    frame_num = row.get("frame_num")
                    if not frame_num:
                        continue
                    try:
                        pop_frames.add(int(frame_num))
                    except ValueError:
                        continue
        except Exception:
            return pop_frames

        return pop_frames

    def _compute_deltas(self, video_times: list[int], audio_times: list[int]) -> Optional[dict[str, float]]:
        if not video_times or not audio_times:
            return None

        video_times = sorted(video_times)
        audio_times = sorted(audio_times)
        if not video_times or not audio_times:
            return None

        deltas_ms = []
        vi = 0
        for at in audio_times:
            while vi + 1 < len(video_times):
                curr = abs(video_times[vi] - at)
                nxt = abs(video_times[vi + 1] - at)
                if nxt <= curr:
                    vi += 1
                else:
                    break
            deltas_ms.append(abs(video_times[vi] - at) / 1000.0)

        max_delta_ms = max(deltas_ms) if deltas_ms else 0.0
        avg_delta_ms = sum(deltas_ms) / len(deltas_ms) if deltas_ms else 0.0

        return {
            "pop_count": len(deltas_ms),
            "max_delta_ms": max_delta_ms,
            "avg_delta_ms": avg_delta_ms,
        }

    def _collapse_pop_times(self, times: list[int], min_gap_us: int) -> list[int]:
        if not times:
            return []

        times = sorted(times)
        clusters = [[times[0]]]
        for ts_us in times[1:]:
            if ts_us - clusters[-1][-1] <= min_gap_us:
                clusters[-1].append(ts_us)
            else:
                clusters.append([ts_us])

        return [cluster[-1] for cluster in clusters if cluster]

    def _find_csv(self, output_dir: Path, filename: str) -> Optional[Path]:
        for subdir in output_dir.glob("session_*"):
            if subdir.is_dir():
                csv_file = subdir / filename
                if csv_file.exists():
                    return csv_file

        plugin_recordings = Path.home() / "Documents" / "obs-studio" / "c64stream" / "recordings"
        if plugin_recordings.exists():
            sessions = sorted(plugin_recordings.glob("session_*"), key=lambda p: p.stat().st_mtime, reverse=True)
            for session in sessions:
                csv_file = session / filename
                if csv_file.exists():
                    return csv_file

        direct = output_dir / filename
        if direct.exists():
            return direct

        return None
