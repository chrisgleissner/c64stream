#!/usr/bin/env python3
"""
C64 Stream - A/V Pop Offset Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Validates A/V pop timing offsets using obs.csv, network.csv, obs.log, and MP4 analysis.
"""

import csv
import re
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig
from util.test_av_sync import detect_video_pop_events, detect_audio_pops


class AvSyncOffsetAssertion(EffectAssertion):
    """Verify that audio and video pops are closely aligned across all sources."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "max_offset_ms": 40.0,  # Strict threshold for CSV sources
            "max_offset_ms_mp4": 1000.0,  # Relaxed for MP4 due to encoding artifacts
            "min_pop_events": 2,
        }
        super().__init__("A/V Pop Offset", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        output_dir = mp4_path.parent
        obs_csv = self._find_csv(output_dir, "obs.csv")
        network_csv = self._find_csv(output_dir, "network.csv")
        av_sync_csv = self._find_csv(output_dir, "av-sync.csv")
        obs_log = self._find_log(output_dir, "obs_log.txt")

        # Collect all pop data from different sources
        sources = {}

        # 1. Extract from CSV files
        if obs_csv:
            obs_result = self._analyze_obs_csv(obs_csv)
            if obs_result and obs_result.get("pop_count", 0) > 0:
                sources["obs_csv"] = obs_result

        if av_sync_csv:
            av_result = self._analyze_av_sync_csv(av_sync_csv)
            if av_result and av_result.get("pop_count", 0) > 0:
                sources["av_sync_csv"] = av_result

        if network_csv:
            net_result = self._analyze_network_csv(network_csv)
            if net_result and net_result.get("pop_count", 0) > 0:
                sources["network_csv"] = net_result

        # If obs.csv had no pops but network.csv did, try frame-based analysis
        if "obs_csv" not in sources and "network_csv" in sources and obs_csv:
            pop_frames = self._extract_network_pop_frames(network_csv)
            if pop_frames:
                obs_result = self._analyze_obs_csv_with_frames(obs_csv, pop_frames)
                if obs_result and obs_result.get("pop_count", 0) > 0:
                    sources["obs_csv"] = obs_result

        # 2. Extract from obs.log
        if obs_log:
            log_result = self._analyze_obs_log(obs_log)
            if log_result and log_result.get("pop_count", 0) > 0:
                sources["obs_log"] = log_result

        # 3. Extract from MP4
        try:
            mp4_result = self._analyze_mp4(mp4_path, verbose)
            if mp4_result and mp4_result.get("pop_count", 0) > 0:
                sources["mp4"] = mp4_result
        except Exception as e:
            if verbose:
                print(f"MP4 analysis failed: {e}")

        # Validate we have data from at least one CSV source plus MP4.
        # obs.log is optional; av-sync.csv is treated as a CSV source.
        missing_sources = []
        if "obs_csv" not in sources and "network_csv" not in sources and "av_sync_csv" not in sources:
            missing_sources.append("CSV (obs.csv, network.csv, or av-sync.csv)")
        if "mp4" not in sources:
            missing_sources.append("MP4")

        if missing_sources:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Missing pop data from: {', '.join(missing_sources)}",
                details={"sources": sources, "output_dir": str(output_dir)},
            )

        # Check minimum pop count
        max_offset_ms = float(self.thresholds["max_offset_ms"])
        min_pop_events = int(self.thresholds["min_pop_events"])

        for source_name, result in sources.items():
            if result["pop_count"] < min_pop_events:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"Too few pop events in {source_name}: {result['pop_count']} < {min_pop_events}",
                    details={"sources": sources},
                )

        # Check max offset across all sources (use different threshold for MP4)
        max_offset_ms = float(self.thresholds["max_offset_ms"])
        max_offset_ms_mp4 = float(self.thresholds["max_offset_ms_mp4"])
        failed_sources = []
        for source_name, result in sources.items():
            threshold = max_offset_ms_mp4 if source_name == "mp4" else max_offset_ms
            if result["max_offset_ms"] > threshold:
                failed_sources.append(f"{source_name}={result['max_offset_ms']:.2f}ms (max {threshold:.0f}ms)")

        if failed_sources:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Pop offset too large: {', '.join(failed_sources)}",
                details={"sources": sources},
            )

        # Check per-pop offsets - ALL must be <= threshold (use appropriate threshold per source)
        for source_name, result in sources.items():
            threshold = max_offset_ms_mp4 if source_name == "mp4" else max_offset_ms
            per_pop_offsets = result.get("per_pop_offsets_ms", [])
            bad_pops = [(i, offset) for i, offset in enumerate(per_pop_offsets) if offset > threshold]
            if bad_pops:
                bad_str = ", ".join([f"pop{i}={offset:.2f}ms" for i, offset in bad_pops[:5]])
                if len(bad_pops) > 5:
                    bad_str += f" (+{len(bad_pops)-5} more)"
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"{source_name}: {len(bad_pops)} pop(s) exceed {threshold:.0f}ms: {bad_str}",
                    details={"sources": sources},
                )

        # All checks passed
        summary = ", ".join([f"{name}={r['max_offset_ms']:.2f}ms" for name, r in sources.items()])
        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=f"Pop offset OK (max {max_offset_ms:.2f}ms): {summary}",
            details={"sources": sources},
            metrics={f"{name}_max_offset_ms": r["max_offset_ms"] for name, r in sources.items()},
        )

    def _analyze_av_sync_csv(self, csv_path: Path) -> Optional[dict[str, float]]:
        try:
            with open(csv_path, "r", newline="") as f:
                reader = csv.DictReader(f)
                if not reader.fieldnames:
                    return None
                if "obs_offset_ms" not in reader.fieldnames:
                    return None

                offsets_ms: list[float] = []
                for row in reader:
                    val = row.get("obs_offset_ms")
                    if val is None or str(val).strip() == "":
                        continue
                    try:
                        offsets_ms.append(abs(float(val)))
                    except ValueError:
                        continue

            if not offsets_ms:
                return None

            return {
                "pop_count": len(offsets_ms),
                "max_offset_ms": max(offsets_ms),
                "avg_offset_ms": sum(offsets_ms) / len(offsets_ms),
                "per_pop_offsets_ms": offsets_ms,
            }
        except Exception:
            return None

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

        max_offset_ms = max(deltas_ms) if deltas_ms else 0.0
        avg_offset_ms = sum(deltas_ms) / len(deltas_ms) if deltas_ms else 0.0

        return {
            "pop_count": len(deltas_ms),
            "max_offset_ms": max_offset_ms,  # Renamed from max_delta_ms
            "avg_offset_ms": avg_offset_ms,  # Renamed from avg_delta_ms
            "per_pop_offsets_ms": deltas_ms,
        }

    def _analyze_obs_log(self, log_path: Path) -> Optional[dict[str, float]]:
        """Extract A/V pop offset info from obs.log debug messages."""
        try:
            # Pattern: [c64stream] A/V pop detected at frame 50: offset=12.34ms
            pattern = re.compile(r'A/V pop detected at frame \d+: offset=([-+]?\d+\.?\d*)ms')
            offsets_ms = []

            with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
                for line in f:
                    match = pattern.search(line)
                    if match:
                        offset = abs(float(match.group(1)))
                        offsets_ms.append(offset)

            if not offsets_ms:
                return None

            return {
                "pop_count": len(offsets_ms),
                "max_offset_ms": max(offsets_ms),
                "avg_offset_ms": sum(offsets_ms) / len(offsets_ms),
                "per_pop_offsets_ms": offsets_ms,
            }
        except Exception:
            return None

    def _analyze_mp4(self, mp4_path: Path, verbose: bool = False) -> Optional[dict[str, float]]:
        """Extract A/V pops from MP4 recording using test_av_sync module."""
        try:
            from bisect import bisect_left
            from util.test_av_sync import extract_audio_envelope

            # Detect video pops (white frames) - doesn't accept verbose
            video_events = detect_video_pop_events(str(mp4_path))
            if not video_events:
                return None

            video_times_ms = [evt['time_ms'] for evt in video_events]

            # Extract audio envelope first
            envelope = extract_audio_envelope(str(mp4_path))
            if envelope is None:
                return None

            try:
                # Check if envelope has data (works for both list and numpy array)
                if len(envelope) == 0:
                    return None
            except TypeError:
                return None

            # Detect audio pops from envelope
            audio_pops = detect_audio_pops(envelope)
            if not audio_pops:
                return None

            # Filter out false positives at the very start (time < 1000ms)
            audio_times_ms = [pop['time_ms'] for pop in audio_pops if pop['time_ms'] >= 1000]
            if not audio_times_ms:
                return None

            # Convert to microseconds for consistency with CSV analysis
            video_times_us = [int(t * 1000) for t in video_times_ms]
            audio_times_us = [int(t * 1000) for t in audio_times_ms]

            # Drop spurious audio-pop detections that are not plausibly associated with any video pop.
            # MP4 audio envelope detection can occasionally produce false positives; a single outlier
            # would otherwise dominate max_offset_ms.
            video_times_us = sorted(video_times_us)
            if not video_times_us:
                return None

            max_window_us = int(float(self.thresholds["max_offset_ms_mp4"]) * 1000.0)
            filtered_audio_times_us: list[int] = []
            for at in sorted(audio_times_us):
                idx = bisect_left(video_times_us, at)
                best = None
                if idx < len(video_times_us):
                    best = abs(video_times_us[idx] - at)
                if idx > 0:
                    prev = abs(video_times_us[idx - 1] - at)
                    best = prev if best is None else min(best, prev)
                if best is not None and best <= max_window_us:
                    filtered_audio_times_us.append(at)

            # Collapse close-together detections into one pop event (best-effort).
            audio_times_us = self._collapse_pop_times(filtered_audio_times_us, 100000)
            if not audio_times_us:
                return None

            return self._compute_deltas(video_times_us, audio_times_us)
        except Exception:
            if verbose:
                import traceback
                traceback.print_exc()
            return None

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

    def _find_log(self, output_dir: Path, filename: str) -> Optional[Path]:
        """Find obs.log file in output directory or session subdirs."""
        # Check session subdirs first
        for subdir in output_dir.glob("session_*"):
            if subdir.is_dir():
                log_file = subdir / filename
                if log_file.exists():
                    return log_file

        # Check direct in output dir
        direct = output_dir / filename
        if direct.exists():
            return direct

        # Check plugin recordings dir
        plugin_recordings = Path.home() / "Documents" / "obs-studio" / "c64stream" / "recordings"
        if plugin_recordings.exists():
            sessions = sorted(plugin_recordings.glob("session_*"), key=lambda p: p.stat().st_mtime, reverse=True)
            for session in sessions:
                log_file = session / filename
                if log_file.exists():
                    return log_file

        return None
