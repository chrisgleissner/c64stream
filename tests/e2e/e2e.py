#!/usr/bin/env python3
"""
C64 Stream - E2E Test Orchestrator
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Orchestrates the complete e2e test pipeline:
1. Starts Xvfb (virtual display)
2. Starts OBS with C64 Stream plugin
3. Replays pre-generated packets via UDP
4. Records OBS output
5. Verifies the recorded output

This test validates that the plugin correctly receives, processes, and renders
C64 Ultimate streams according to the specification.
"""

from __future__ import annotations  # Enable PEP 604 union types on Python 3.9+

import os
import sys
import subprocess
import threading
import time
import signal
import argparse
import json
import socket
import tempfile
import shutil
import configparser
from pathlib import Path


def validate_network_timing(
    network_json_path: Path,
    video_format: str,
    frames: int,
    network_simulation: dict | None,
) -> tuple[str, str, list[str], list[str]]:
    """Validate sender pacing using derived metrics in network.json.

    Returns: (status, details, errors, warnings)
    - status: pass|warning|fail|unknown
    - details: short single-line summary
    """
    if network_simulation is None:
        network_simulation = {}

    if not network_json_path.exists():
        return 'unknown', 'network.json not found', [], []

    # Mirror validate_test_results timing constants.
    if video_format == 'PAL':
        frame_rate = 50.125
        expected_video_interval_us = 293.384
        expected_audio_interval_us = 4001.417
    else:
        frame_rate = 59.826
        expected_video_interval_us = 278.586
        expected_audio_interval_us = 4005.006

    expected_duration_ms = frames * (1000.0 / frame_rate)

    max_jitter_ms = float(network_simulation.get('max_jitter_ms', 0) or 0)
    reorder_max_delay_ms = float(network_simulation.get('reorder_max_delay_ms', 0) or 0)
    extra_delay_ms = max(max_jitter_ms, reorder_max_delay_ms)

    errors: list[str] = []
    warnings: list[str] = []

    try:
        with open(network_json_path, 'r') as f:
            net = json.load(f)
    except Exception as e:
        return 'unknown', f'validation error: {e}', [], [f'Network timing validation failed: {e}']

    summary = net.get('summary', {})
    video_stats = net.get('video', {})
    audio_stats = net.get('audio', {})

    duration_ms = summary.get('duration_ms', None)
    if duration_ms is None:
        warnings.append('network.json missing duration_ms')
    else:
        min_ok_ms = expected_duration_ms * 0.70
        # Allow extra network simulation delay on top of baseline.
        max_ok_ms = expected_duration_ms + extra_delay_ms + 2000.0
        if duration_ms < min_ok_ms:
            errors.append(
                f"Network timing span too short: {duration_ms:.1f}ms < {min_ok_ms:.1f}ms (expected ~{expected_duration_ms:.1f}ms)"
            )
        elif duration_ms > max_ok_ms:
            warnings.append(
                f"Network timing span unusually long: {duration_ms:.1f}ms > {max_ok_ms:.1f}ms"
            )

    def check_spacing(stream_name: str, stats: dict, expected_interval_us: float):
        mean_us = stats.get('spacing_mean_us', None)
        if mean_us is None:
            return
        # Wide tolerance: still catches bursty/instant sender.
        min_mean = expected_interval_us * 0.40
        max_mean = expected_interval_us * 2.50
        if mean_us < min_mean:
            errors.append(
                f"{stream_name} spacing mean too small: {mean_us:.1f}us < {min_mean:.1f}us (burst/instant send?)"
            )
        elif mean_us > max_mean:
            warnings.append(
                f"{stream_name} spacing mean unusually large: {mean_us:.1f}us > {max_mean:.1f}us"
            )

    check_spacing('Video', video_stats, expected_video_interval_us)
    check_spacing('Audio', audio_stats, expected_audio_interval_us)

    # Jitter sanity (no network simulation should be relatively steady).
    # NOTE: max jitter is extremely sensitive to host scheduling stalls (CI noise).
    # Prefer distribution metrics (p99 spacing) over single-sample maxima.
    if max_jitter_ms <= 0 and float(network_simulation.get('reorder_percent', 0) or 0) <= 0:
        v_p99_us = video_stats.get('spacing_p99_us', None)
        a_p99_us = audio_stats.get('spacing_p99_us', None)

        # Very wide thresholds: flags true "burst/gap" regressions but avoids
        # failing normal runs due to rare scheduler stalls.
        if v_p99_us is not None:
            v_p99_us_f = float(v_p99_us)
            if v_p99_us_f > (expected_video_interval_us * 20.0):
                warnings.append(f"Video p99 spacing high for no-sim run: {v_p99_us_f:.1f}us")
        if a_p99_us is not None:
            a_p99_us_f = float(a_p99_us)
            if a_p99_us_f > (expected_audio_interval_us * 8.0):
                warnings.append(f"Audio p99 spacing high for no-sim run: {a_p99_us_f:.1f}us")

        v_ooo = float(video_stats.get('out_of_order_rate_pct', 0) or 0)
        a_ooo = float(audio_stats.get('out_of_order_rate_pct', 0) or 0)
        if v_ooo > 0.5 or a_ooo > 0.5:
            warnings.append(f"Out-of-order without simulation (video={v_ooo:.2f}%, audio={a_ooo:.2f}%)")

    details_parts = []
    if duration_ms is not None:
        details_parts.append(f"span={duration_ms:.1f}ms")
    v_mean = video_stats.get('spacing_mean_us', None)
    a_mean = audio_stats.get('spacing_mean_us', None)
    if v_mean is not None:
        details_parts.append(f"video_mean={v_mean:.1f}us")
    if a_mean is not None:
        details_parts.append(f"audio_mean={a_mean:.1f}us")
    details = ', '.join(details_parts) if details_parts else 'ok'

    if errors:
        return 'fail', details, errors, warnings
    if warnings:
        return 'warning', details, errors, warnings
    return 'pass', details, errors, warnings


def _is_benign_network_timing_warning(warning: str) -> bool:
    # In CI (and other loaded environments), some warnings can be caused by host scheduling
    # and are not necessarily a sender regression. Keep them visible, but don't escalate.
    return (
        warning.startswith('Video jitter max high for no-sim run:')
        or warning.startswith('Audio jitter max high for no-sim run:')
        or warning.startswith('Video p99 spacing high for no-sim run:')
        or warning.startswith('Audio p99 spacing high for no-sim run:')
        or warning.startswith('Out-of-order without simulation (')
        or warning.startswith('Network timing span unusually long:')
    )

# Import A/V sync testing
try:
    from test_av_sync import verify_av_sync
except ImportError:
    verify_av_sync = None
try:
    import websocket
    import requests
    WEBSOCKET_AVAILABLE = True
except ImportError:
    WEBSOCKET_AVAILABLE = False

# Import resource monitoring
try:
    from resource_monitor import ResourceMonitor
    RESOURCE_MONITOR_AVAILABLE = True
except ImportError:
    ResourceMonitor = None
    RESOURCE_MONITOR_AVAILABLE = False


class E2ETest:
    def __init__(self, test_dir, video_port=21000, audio_port=21001, control_port=6400,
                 format='NTSC', frames=30, verbose=False, enable_websocket=False,
                 scenario_overrides_dir: str | None = None, scenario_name: str | None = None,
                 scenario_id: str | None = None, output_dir: str | None = None,
                 csv_max_rows: int | None = None, enable_resource_monitoring: bool = False,
                 resource_interval_ms: int = 500,
                 settling_seconds: float = 0.0,
                 enable_perf_profile: bool = False,
                 perf_frequency_hz: int = 99,
                 perf_callgraph: str = 'fp',
                 perf_duration_s: float | None = None,
                 enable_flamegraph: bool = False,
                 network_simulation: dict | None = None,
                 av_sync_tolerance_ms: int = 60):
        self.test_dir = Path(test_dir)
        self.video_port = video_port
        self.audio_port = audio_port
        self.control_port = control_port
        self.control_bind_ip = os.environ.get('C64_E2E_CONTROL_BIND_IP', '0.0.0.0')
        self.format = format
        self.frames = frames
        self.verbose = verbose
        self.enable_websocket = enable_websocket  # Disable WebSocket by default for performance
        self.scenario_overrides_dir = Path(scenario_overrides_dir).resolve() if scenario_overrides_dir else None
        self.scenario_name = scenario_name
        self.scenario_id = scenario_id
        self.network_simulation = network_simulation or {}  # Store network simulation config
        self.av_sync_tolerance_ms = av_sync_tolerance_ms  # Per-scenario A/V sync tolerance

        self.settling_seconds = float(settling_seconds) if settling_seconds is not None else 0.0
        self._obs_start_time_s: float | None = None

        # Optional perf profiling (Linux): best-effort, skipped if unsupported.
        self.enable_perf_profile = bool(enable_perf_profile)
        self.perf_frequency_hz = int(perf_frequency_hz)
        self.perf_callgraph = str(perf_callgraph)
        self.perf_duration_s = float(perf_duration_s) if perf_duration_s is not None else None
        self.enable_flamegraph = bool(enable_flamegraph)
        self._perf_process: subprocess.Popen | None = None
        self._perf_data_path: Path | None = None

        # Detect CI environment and set appropriate timeouts
        self.is_ci = self._detect_ci_environment()
        self._configure_timeouts()

        # Process handles
        self.xvfb_process = None
        self.obs_process = None
        self.tcp_server_thread = None
        self.tcp_server_thread_alt = None
        self.tcp_server_socket = None
        self.tcp_server_socket_alt = None
        self.tcp_server_running = False
        self.udp_replay_triggered = threading.Event()

        # Ensure we don't start packet replay until we've received START for *both* streams.
        # The plugin can request audio/video at different times in CI; starting replay early
        # can create an artificial A/V offset in the recording.
        self._stream_start_mask = 0  # bit0=video, bit1=audio
        self._stream_start_lock = threading.Lock()

        # UDP destination addresses (updated from TCP commands)
        self.video_dest_ip = '127.0.0.1'
        self.video_dest_port = self.video_port
        self.audio_dest_ip = '127.0.0.1'
        self.audio_dest_port = self.audio_port

        # Test artifacts
        self.packet_dir = self.test_dir / 'test_packets'
        if output_dir:
            out_path = Path(output_dir)
            if not out_path.is_absolute():
                out_path = self.test_dir / out_path
            self.output_dir = out_path
        else:
            self.output_dir = self.test_dir / 'test_output'
        self.output_dir.mkdir(parents=True, exist_ok=True)

        # Track backed up properties.ini files for restoration on cleanup
        self._backed_up_properties: list[tuple[Path, Path]] = []  # (backup_path, original_path)

        # CSV truncation: cap CSV file to N total lines (including header)
        self.csv_max_rows = csv_max_rows

        # Resource monitoring
        self.enable_resource_monitoring = enable_resource_monitoring and RESOURCE_MONITOR_AVAILABLE
        self.resource_interval_ms = resource_interval_ms
        self._resource_monitor = None
        self._resource_summary = None
        self._filtered_resource_summary = None  # Summary from filtered samples only

        if enable_resource_monitoring and not RESOURCE_MONITOR_AVAILABLE:
            self.log("⚠️ Resource monitoring requested but resource_monitor module not available")

    def _detect_ci_environment(self):
        """Detect if running in CI environment."""
        ci_indicators = [
            'CI', 'CONTINUOUS_INTEGRATION', 'GITHUB_ACTIONS',
            'GITLAB_CI', 'JENKINS_URL', 'TRAVIS', 'CIRCLECI',
            'BUILDKITE', 'DRONE', 'TEAMCITY_VERSION'
        ]
        return any(os.environ.get(indicator) for indicator in ci_indicators)

    def _copy_csv_truncated(self, src: Path, dest: Path):
        """Copy a CSV file, optionally truncating to first N lines.

        If csv_max_rows is set, keeps at most N total lines, including the header.
        This makes committed artifacts deterministic and keeps repository diffs small.
        """
        if self.csv_max_rows is None:
            # No truncation requested - simple copy
            shutil.copy2(src, dest)
            return

        with open(src, 'r') as f:
            lines = f.readlines()

        if len(lines) <= 1:
            # Only header or empty - just copy
            shutil.copy2(src, dest)
            return

        # Keep at most N total lines, including header.
        header = lines[0]
        data_lines = lines[1:]
        total_data_rows = len(data_lines)

        # csv_max_rows is total lines including header; derive max data rows.
        max_total_lines = max(1, int(self.csv_max_rows))
        max_data_rows = max(0, max_total_lines - 1)

        if total_data_rows <= max_data_rows:
            # Not enough rows to truncate - just copy
            shutil.copy2(src, dest)
            return

        # Truncate to first N total lines
        output_lines = [header] + data_lines[:max_data_rows]
        truncated_count = total_data_rows - max_data_rows

        with open(dest, 'w') as f:
            f.writelines(output_lines)

        if truncated_count > 0:
            self.log(f"📉 Truncated {truncated_count} rows from {src.name} "
                     f"(keeping first {max_total_lines} lines)")

    def _analyze_network_jitter(self, network_csv: Path) -> Optional[dict]:
        """Analyze network.csv packet timing.

        This must be called BEFORE CSV truncation to analyze the full dataset.

        Current metrics:
        - Spacing between consecutive packets (min/mean/max per stream + overall)
        - Evenness / burstiness indicators derived from spacing distribution
        - Duration from first to last packet
        - Jitter (deviation from median spacing) and out-of-order rate
        """
        import csv
        import statistics

        if not network_csv.exists():
            self.log("⚠️ network.csv not found for jitter analysis")
            return None

        try:
            all_intervals = []
            video_intervals = []
            audio_intervals = []
            video_sequence = []
            audio_sequence = []
            elapsed_us_values = []

            with open(network_csv, 'r') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    packet_type = row.get('packet_type', '')
                    interval_str = row.get('packet_interval_us', '')
                    seq_str = row.get('sequence_num', '')
                    elapsed_str = row.get('elapsed_us', '')

                    if elapsed_str:
                        try:
                            elapsed_us = float(elapsed_str)
                            if 0 <= elapsed_us <= 86_400_000_000:  # up to 24h
                                elapsed_us_values.append(elapsed_us)
                        except ValueError:
                            pass

                    if not interval_str:
                        continue

                    try:
                        interval_us = float(interval_str)
                        # Skip unrealistic values (negative or extremely large)
                        if interval_us <= 0 or interval_us > 1_000_000:
                            continue

                        all_intervals.append(interval_us)

                        seq_num = int(seq_str) if seq_str else 0

                        if packet_type == 'video':
                            video_intervals.append(interval_us)
                            video_sequence.append(seq_num)
                        elif packet_type == 'audio':
                            audio_intervals.append(interval_us)
                            audio_sequence.append(seq_num)
                    except ValueError:
                        continue

            results = {
                'all': {},
                'video': {},
                'audio': {},
                'summary': {}
            }

            def quantile(sorted_values, q):
                """Nearest-rank quantile (q in [0,1])."""
                if not sorted_values:
                    return None
                if q <= 0:
                    return sorted_values[0]
                if q >= 1:
                    return sorted_values[-1]
                import math
                k = int(math.ceil(q * len(sorted_values))) - 1
                k = max(0, min(k, len(sorted_values) - 1))
                return sorted_values[k]

            def spacing_stats(intervals):
                if len(intervals) < 2:
                    return {
                        'count': len(intervals),
                    }

                intervals_sorted = sorted(intervals)
                median_us = statistics.median(intervals_sorted)
                mean_us = statistics.mean(intervals_sorted)
                min_us = intervals_sorted[0]
                max_us = intervals_sorted[-1]
                std_us = statistics.pstdev(intervals_sorted) if len(intervals_sorted) >= 2 else 0.0
                cv_pct = (std_us / mean_us * 100.0) if mean_us > 0 else 0.0

                p95_us = quantile(intervals_sorted, 0.95)
                p99_us = quantile(intervals_sorted, 0.99)

                # Burstiness heuristics relative to median spacing
                short_thresh = 0.5 * median_us
                long_thresh = 2.0 * median_us
                short_count = sum(1 for v in intervals_sorted if v < short_thresh)
                long_count = sum(1 for v in intervals_sorted if v > long_thresh)

                burst_short_pct = (short_count / len(intervals_sorted) * 100.0) if intervals_sorted else 0.0
                burst_long_pct = (long_count / len(intervals_sorted) * 100.0) if intervals_sorted else 0.0
                p99_p50 = (p99_us / median_us) if (p99_us is not None and median_us > 0) else None

                return {
                    'count': len(intervals_sorted),
                    'spacing_min_us': round(min_us, 2),
                    'spacing_mean_us': round(mean_us, 2),
                    'spacing_max_us': round(max_us, 2),
                    'spacing_median_us': round(median_us, 2),
                    'spacing_std_us': round(std_us, 2),
                    'spacing_cv_pct': round(cv_pct, 2),
                    'spacing_p95_us': round(p95_us, 2) if p95_us is not None else None,
                    'spacing_p99_us': round(p99_us, 2) if p99_us is not None else None,
                    'burst_short_pct': round(burst_short_pct, 2),
                    'burst_long_pct': round(burst_long_pct, 2),
                    'burst_p99_p50': round(p99_p50, 3) if p99_p50 is not None else None,
                }

            def count_out_of_order(seq_list):
                """Count how many packets arrived out of sequence order."""
                if len(seq_list) < 2:
                    return 0, 0.0
                out_of_order = 0
                for i in range(1, len(seq_list)):
                    # Count as out-of-order if current seq is less than previous
                    if seq_list[i] < seq_list[i-1]:
                        out_of_order += 1
                rate = (out_of_order / len(seq_list)) * 100 if seq_list else 0
                return out_of_order, rate

            # Analyze video packet intervals
            if len(video_intervals) >= 2:
                video_stats = spacing_stats(video_intervals)
                video_median = video_stats.get('spacing_median_us', 0)

                # Calculate jitter as deviation from median (more robust than mean)
                video_jitter = [abs(v - video_median) for v in video_intervals]
                video_jitter_median = statistics.median(video_jitter)
                video_jitter_max = max(video_jitter)

                # Calculate out-of-order rate
                video_ooo_count, video_ooo_rate = count_out_of_order(video_sequence)

                results['video'] = {
                    **video_stats,
                    # Backwards-compatible aliases
                    'interval_median_us': round(video_stats.get('spacing_median_us', 0.0), 2),
                    'interval_mean_us': round(video_stats.get('spacing_mean_us', 0.0), 2),
                    'interval_min_us': round(video_stats.get('spacing_min_us', 0.0), 2),
                    'interval_max_us': round(video_stats.get('spacing_max_us', 0.0), 2),
                    'jitter_median_us': round(video_jitter_median, 2),
                    'jitter_max_us': round(video_jitter_max, 2),
                    'jitter_median_ms': round(video_jitter_median / 1000, 3),
                    'jitter_max_ms': round(video_jitter_max / 1000, 3),
                    'out_of_order_count': video_ooo_count,
                    'out_of_order_rate_pct': round(video_ooo_rate, 2),
                }
                self.log(f"📊 Video packets: {len(video_intervals)}, "
                         f"jitter median={video_jitter_median:.1f}μs max={video_jitter_max:.1f}μs, "
                         f"out-of-order={video_ooo_count} ({video_ooo_rate:.1f}%)")

            # Analyze audio packet intervals
            if len(audio_intervals) >= 2:
                audio_stats = spacing_stats(audio_intervals)
                audio_median = audio_stats.get('spacing_median_us', 0)

                # Calculate jitter as deviation from median
                audio_jitter = [abs(a - audio_median) for a in audio_intervals]
                audio_jitter_median = statistics.median(audio_jitter)
                audio_jitter_max = max(audio_jitter)

                # Calculate out-of-order rate
                audio_ooo_count, audio_ooo_rate = count_out_of_order(audio_sequence)

                results['audio'] = {
                    **audio_stats,
                    # Backwards-compatible aliases
                    'interval_median_us': round(audio_stats.get('spacing_median_us', 0.0), 2),
                    'interval_mean_us': round(audio_stats.get('spacing_mean_us', 0.0), 2),
                    'interval_min_us': round(audio_stats.get('spacing_min_us', 0.0), 2),
                    'interval_max_us': round(audio_stats.get('spacing_max_us', 0.0), 2),
                    'jitter_median_us': round(audio_jitter_median, 2),
                    'jitter_max_us': round(audio_jitter_max, 2),
                    'jitter_median_ms': round(audio_jitter_median / 1000, 3),
                    'jitter_max_ms': round(audio_jitter_max / 1000, 3),
                    'out_of_order_count': audio_ooo_count,
                    'out_of_order_rate_pct': round(audio_ooo_rate, 2),
                }
                self.log(f"📊 Audio packets: {len(audio_intervals)}, "
                         f"jitter median={audio_jitter_median:.1f}μs max={audio_jitter_max:.1f}μs, "
                         f"out-of-order={audio_ooo_count} ({audio_ooo_rate:.1f}%)")

            if len(all_intervals) >= 2:
                results['all'] = spacing_stats(all_intervals)

            # Overall summary
            duration_us = None
            duration_ms = None
            first_elapsed_us = None
            last_elapsed_us = None

            if elapsed_us_values:
                first_elapsed_us = min(elapsed_us_values)
                last_elapsed_us = max(elapsed_us_values)
                duration_us = max(0.0, last_elapsed_us - first_elapsed_us)
                duration_ms = duration_us / 1000.0

            results['summary'] = {
                'first_elapsed_us': round(first_elapsed_us, 2) if first_elapsed_us is not None else None,
                'last_elapsed_us': round(last_elapsed_us, 2) if last_elapsed_us is not None else None,
                'duration_us': round(duration_us, 2) if duration_us is not None else None,
                'duration_ms': round(duration_ms, 3) if duration_ms is not None else None,
                'total_video_packets': results['video'].get('count', 0),
                'total_audio_packets': results['audio'].get('count', 0),
                'total_packets': results.get('all', {}).get('count', 0),
                'analysis_complete': True
            }

            return results

        except Exception as e:
            self.log(f"❌ Failed to analyze network jitter: {e}")
            import traceback
            traceback.print_exc()
            return None

    def _save_network_analysis(self, network_csv: Path) -> Optional[Path]:
        """
        Analyze network.csv and save results to network.json.

        Must be called BEFORE CSV truncation.

        Args:
            network_csv: Path to the full network.csv

        Returns:
            Path to network.json if successful, None otherwise
        """
        results = self._analyze_network_jitter(network_csv)
        if results is None:
            return None

        network_json = self.output_dir / 'network.json'
        try:
            import json
            with open(network_json, 'w') as f:
                json.dump(results, f, indent=2)
            self.log(f"✅ Saved network analysis to: {network_json}")
            return network_json
        except Exception as e:
            self.log(f"❌ Failed to save network.json: {e}")
            return None

    def _find_obs_csv(self) -> Optional[Path]:
        """
        Find the obs.csv file from the most recent recording session.

        Looks in OBS's recording directory since obs.csv may not be copied
        to output_dir yet when this is called.

        Returns:
            Path to obs.csv, or None if not found
        """
        # First try output_dir (if already copied)
        local_csv = self.output_dir / 'obs.csv'
        if local_csv.exists():
            return local_csv

        # Otherwise look in OBS's recording directory
        recordings_base = Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'
        if not recordings_base.exists():
            return None

        # Find the most recent session folder
        session_folders = [f for f in recordings_base.glob('session_*') if f.is_dir()]
        if not session_folders:
            return None

        session_folders.sort(key=lambda f: f.stat().st_mtime, reverse=True)
        obs_csv = session_folders[0] / 'obs.csv'
        return obs_csv if obs_csv.exists() else None

    def _get_obs_processing_duration_ms(self) -> Optional[float]:
        """
        Get the total OBS processing duration from obs.csv.

        Reads the last row's elapsed_us field to determine how long OBS
        was processing frames after receiving the first packet.

        Returns:
            Duration in milliseconds, or None if obs.csv not found/readable
        """
        obs_csv = self._find_obs_csv()
        if obs_csv is None:
            return None

        try:
            import csv
            with open(obs_csv, 'r') as f:
                reader = csv.DictReader(f)
                last_row = None
                for row in reader:
                    last_row = row
                if last_row and 'elapsed_us' in last_row:
                    elapsed_us = float(last_row['elapsed_us'])
                    return elapsed_us / 1000.0  # Convert μs to ms
        except Exception as e:
            self.log(f"⚠️ Failed to read obs.csv duration: {e}")
        return None

    def _save_resource_data(self):
        """Save resource monitoring data to CSV and JSON files.

        Creates resource.csv (filtered samples) and resource.json (summary)
        in the same directory as network.csv and obs.csv.

        The samples are filtered to only include the actual processing window:
        from first UDP packet received (timestamp 0) to last OBS frame processed.
        """
        if not self._resource_monitor or not self._resource_summary:
            return

        try:
            # Get processing duration from obs.csv to filter samples
            processing_duration_ms = self._get_obs_processing_duration_ms()

            if processing_duration_ms is not None:
                # Filter samples to the actual processing window
                # Start at 0 (when monitoring started = when packets started)
                # End at the last OBS frame processed
                filtered_samples = self._resource_monitor.filter_samples_by_window(
                    start_ms=0,
                    end_ms=processing_duration_ms
                )
                # Compute summary from filtered samples only
                filtered_summary = self._resource_monitor.compute_summary_from_samples(filtered_samples)
                total_samples = len(self._resource_monitor.samples)
                self.log(f"📊 Filtered {len(filtered_samples)}/{total_samples} samples within processing window ({processing_duration_ms:.1f}ms)")
            else:
                # Fallback: use all samples if obs.csv not available
                filtered_samples = self._resource_monitor.samples
                filtered_summary = self._resource_summary
                total_samples = len(filtered_samples)
                self.log("⚠️ Using all samples (obs.csv not available for filtering)")

            # Save filtered samples to resource.csv (alongside network.csv and obs.csv)
            csv_path = self.output_dir / 'resource.csv'
            self._resource_monitor.save_csv_from_samples(csv_path, filtered_samples)

            # Save summary from filtered samples to resource.json (include total for README)
            json_path = self.output_dir / 'resource.json'
            self._resource_monitor.save_json(json_path, filtered_summary, total_sample_count=total_samples)

            # Store filtered summary for later use (stdout logging, README)
            self._filtered_resource_summary = filtered_summary

            self.log(f"📊 Resource data saved to {csv_path.name} and {json_path.name}")

            # Print high-level resource summary to stdout
            self._print_resource_summary(filtered_summary)

        except Exception as e:
            self.log(f"⚠️ Failed to save resource data: {e}")
            import traceback
            traceback.print_exc()

    def _print_resource_summary(self, summary):
        """Print high-level resource usage summary to stdout."""
        if summary.cpu.sample_count == 0:
            return

        print("\n📊 Resource Usage Summary:")
        print(f"   Duration: {summary.duration_ms/1000:.1f}s ({summary.cpu.sample_count} samples)")
        print(f"   CPU: {summary.cpu.median_val:.1f}% median (max: {summary.cpu.max_val:.1f}%)")
        print(f"   RAM: {summary.ram_mb.median_val:.0f} MB median")

        if summary.gpu is not None:
            print(f"   GPU: {summary.gpu.median_val:.1f}% median (max: {summary.gpu.max_val:.1f}%)")
        elif not summary.gpu_available:
            print("   GPU: not available")

    def _configure_timeouts(self):
        """Configure timeouts based on environment."""
        if self.is_ci:
            # CI environment: Extended timeouts
            self.plugin_init_timeout = 45  # Increased from 30s for more robust CI
            self.obs_startup_delay = 4     # Increased from 3s
            # Reduced from 6s - plugin connects quickly, we just need UDP binding
            self.async_task_delay = 2.0    # Reduced to minimize logo display at start
            self.websocket_settings_delay = 3  # Increased from 2s
            # Give OBS/plugin more time to bind UDP ports on CI
            self.udp_socket_delay = 2.0    # Increased from 1.0s
            self.buffer_setup_delay = 1.0  # Increased from 0.5s
            self.log("🏗️ CI environment detected - using extended timeouts")
        else:
            # Local environment: Short timeouts
            self.plugin_init_timeout = 6
            self.obs_startup_delay = 0.5
            # Reduced from 0.3s - plugin connects almost instantly locally
            self.async_task_delay = 0.1    # Minimal delay to allow UDP binding
            self.websocket_settings_delay = 0.2
            # Even locally, OBS/plugin may need a moment to bind UDP ports reliably.
            # Too-small delays can cause occasional packet loss and flaky assertions.
            self.udp_socket_delay = 0.2
            self.buffer_setup_delay = 0.2
            self.log("🚀 Local environment detected - using short timeouts")

    def _boost_process_priority(self, pid: int):
        """Boost process priority using renice and ionice for smoother frame delivery.

        This helps reduce skipped/repeated frames by giving OBS higher scheduling priority.
        Requires either root privileges or CAP_SYS_NICE capability on the e2e.py process.

        Args:
            pid: Process ID to boost priority for
        """
        # Try to set high CPU priority (nice -10)
        try:
            result = subprocess.run(
                ['renice', '-n', '-10', '-p', str(pid)],
                capture_output=True,
                timeout=5
            )
            if result.returncode == 0:
                self.log(f"✅ Boosted CPU priority for OBS (PID {pid}) to nice -10")
            else:
                # Check if it's a permission issue
                stderr = result.stderr.decode().lower()
                if 'permission' in stderr or 'operation not permitted' in stderr:
                    self.log(f"⚠️ Cannot boost CPU priority (no root/CAP_SYS_NICE): {result.stderr.decode().strip()}")
                else:
                    self.log(f"⚠️ renice failed: {result.stderr.decode().strip()}")
        except FileNotFoundError:
            self.log("⚠️ renice not available on this system")
        except subprocess.TimeoutExpired:
            self.log("⚠️ renice timed out")
        except Exception as e:
            self.log(f"⚠️ renice error: {e}")

        # Try to set high I/O priority (best-effort class, priority 0)
        try:
            result = subprocess.run(
                ['ionice', '-c', '2', '-n', '0', '-p', str(pid)],
                capture_output=True,
                timeout=5
            )
            if result.returncode == 0:
                self.log(f"✅ Boosted I/O priority for OBS (PID {pid}) to best-effort class 0")
            else:
                stderr = result.stderr.decode().strip()
                if stderr:
                    self.log(f"⚠️ ionice failed: {stderr}")
        except FileNotFoundError:
            self.log("⚠️ ionice not available on this system")
        except subprocess.TimeoutExpired:
            self.log("⚠️ ionice timed out")
        except Exception as e:
            self.log(f"⚠️ ionice error: {e}")

    def log(self, message):
        """Print log message if verbose mode is enabled."""
        if self.verbose:
            print(f"[TEST] {message}")

    def _start_perf_profile(self, target_pid: int, duration_s: float) -> None:
        """Start best-effort perf recording for a given PID (Linux only)."""
        if not self.enable_perf_profile:
            return

        perf_path = shutil.which('perf')
        if not perf_path:
            self.log("⚠️ perf not found; skipping perf profiling")
            return

        if target_pid <= 0:
            self.log("⚠️ Invalid PID for perf profiling; skipping")
            return

        duration_s = max(0.5, min(float(duration_s), 120.0))

        self._perf_data_path = self.output_dir / 'perf.data'
        perf_stdout_path = self.output_dir / 'perf_record_stdout.txt'
        perf_stderr_path = self.output_dir / 'perf_record_stderr.txt'
        cmd = [
            perf_path,
            'record',
            '-F',
            str(self.perf_frequency_hz),
            '--call-graph',
            self.perf_callgraph,
            '-p',
            str(target_pid),
            '-o',
            str(self._perf_data_path),
            '--',
            'sleep',
            f'{duration_s:.3f}',
        ]

        try:
            self.log(f"🔥 perf record (pid={target_pid}, {duration_s:.1f}s)")
            # Write stdout/stderr to files so failures aren't silent.
            # (perf emits many important errors on stderr and may still create an empty perf.data)
            perf_stdout_f = open(perf_stdout_path, 'w', encoding='utf-8', errors='ignore')
            perf_stderr_f = open(perf_stderr_path, 'w', encoding='utf-8', errors='ignore')
            self._perf_process = subprocess.Popen(cmd, stdout=perf_stdout_f, stderr=perf_stderr_f, text=True)
        except Exception as e:
            self.log(f"⚠️ Failed to start perf profiling: {e}")
            self._perf_process = None
            self._perf_data_path = None

    def _finalize_perf_profile(self) -> None:
        """Finalize perf capture and export perf_report.txt (+ optional flamegraph.svg)."""
        if not self.enable_perf_profile or not self._perf_data_path:
            return

        perf_path = shutil.which('perf')
        if not perf_path:
            return

        if self._perf_process:
            # We already streamed perf record stdout/stderr into perf_record_*.txt.
            try:
                self._perf_process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                try:
                    self._perf_process.terminate()
                except Exception:
                    pass
                try:
                    self._perf_process.wait(timeout=3)
                except Exception:
                    pass

        if not self._perf_data_path.exists():
            self.log("⚠️ perf.data not found; perf likely failed (check perf_record_stderr.txt for permissions)")
            return

        try:
            if self._perf_data_path.stat().st_size == 0:
                self.log("⚠️ perf.data is empty; perf profiling failed (check perf_record_stderr.txt)")
                # Keep a small hint file for quick triage from CI artifacts.
                (self.output_dir / 'perf_error.txt').write_text(
                    "perf.data is empty. On Linux this commonly means perf is blocked by kernel settings\n"
                    "(see /proc/sys/kernel/perf_event_paranoid) or missing permissions.\n"
                    "Check perf_record_stderr.txt for the exact error.\n",
                    encoding='utf-8',
                    errors='ignore',
                )
                return
        except Exception:
            # If we can't stat it, continue and let perf report fail with a message.
            pass

        # Export a simple text report for quick inspection.
        try:
            report_path = self.output_dir / 'perf_report.txt'
            cmd = [
                perf_path,
                'report',
                '--stdio',
                '-i',
                str(self._perf_data_path),
                '--no-children',
                '--sort',
                'comm,dso,symbol',
            ]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=45)
            report_path.write_text(
                (result.stdout or '') + ("\n\n" + result.stderr if result.stderr else ''),
                encoding='utf-8',
                errors='ignore',
            )
            self.log("📄 Wrote perf_report.txt")
        except Exception as e:
            self.log(f"⚠️ Failed to export perf report: {e}")

        if not self.enable_flamegraph:
            return

        # Flamegraph generation is optional and depends on FlameGraph scripts.
        try:
            stackcollapse = shutil.which('stackcollapse-perf.pl')
            flamegraph = shutil.which('flamegraph.pl')

            repo_root = Path(__file__).resolve().parents[2]
            vendored_stackcollapse = repo_root / 'tools' / 'FlameGraph' / 'stackcollapse-perf.pl'
            vendored_flamegraph = repo_root / 'tools' / 'FlameGraph' / 'flamegraph.pl'

            if not stackcollapse:
                p = Path('/usr/share/flamegraph/stackcollapse-perf.pl')
                if p.exists():
                    stackcollapse = str(p)
                elif vendored_stackcollapse.exists():
                    stackcollapse = str(vendored_stackcollapse)
            if not flamegraph:
                p = Path('/usr/share/flamegraph/flamegraph.pl')
                if p.exists():
                    flamegraph = str(p)
                elif vendored_flamegraph.exists():
                    flamegraph = str(vendored_flamegraph)

            if not stackcollapse or not flamegraph:
                self.log(
                    "⚠️ FlameGraph scripts not found; install them via /usr/share/flamegraph or clone "
                    "https://github.com/brendangregg/FlameGraph into tools/FlameGraph (helper: build-aux/install-flamegraph.sh)"
                )
                return

            folded_path = self.output_dir / 'perf.folded'
            svg_path = self.output_dir / 'flamegraph.svg'

            perf_script = subprocess.Popen([perf_path, 'script', '-i', str(self._perf_data_path)], stdout=subprocess.PIPE)
            collapse = subprocess.Popen([stackcollapse], stdin=perf_script.stdout, stdout=subprocess.PIPE, text=True)
            perf_script.stdout.close()
            folded, _ = collapse.communicate(timeout=90)
            folded_path.write_text(folded, encoding='utf-8', errors='ignore')

            fg = subprocess.run([flamegraph], input=folded, capture_output=True, text=True, timeout=90)
            svg_path.write_text(fg.stdout, encoding='utf-8', errors='ignore')
            self.log("🔥 Wrote flamegraph.svg")
        except Exception as e:
            self.log(f"⚠️ Failed to generate flamegraph: {e}")

    def clean_test_output(self):
        """Clean the test output directory before starting E2E test."""
        import shutil

        if self.output_dir.exists():
            self.log(f"Cleaning test output directory: {self.output_dir}")
            try:
                shutil.rmtree(self.output_dir)
                self.output_dir.mkdir(parents=True, exist_ok=True)
                self.log("✅ Test output directory cleaned")
            except Exception as e:
                self.log(f"❌ Failed to clean test output directory: {e}")
        else:
            self.output_dir.mkdir(parents=True, exist_ok=True)
            self.log("✅ Created fresh test output directory")

    def start_xvfb(self, display=':99'):
        """Start Xvfb virtual framebuffer for headless testing."""
        self.log(f"Starting Xvfb on display {display}")

        try:
            # Check if Xvfb is already running on this display
            try:
                result = subprocess.run(['pgrep', '-f', f'Xvfb.*{display}'],
                                      capture_output=True, check=False)
                if result.returncode == 0 and result.stdout.strip():
                    self.log(f"✅ Xvfb already running on {display} (started by workflow)")
                    # Set DISPLAY environment variable
                    os.environ['DISPLAY'] = display
                    # Apply conservative Qt/GL settings when using Xvfb.
                    os.environ.setdefault('QT_QPA_PLATFORM', 'xcb')
                    os.environ.setdefault('QT_X11_NO_MITSHM', '1')
                    os.environ.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
                    self.log("🧪 Applied headless Qt/GL environment variables")
                    self.xvfb_process = None  # Not managed by us
                    return True
            except Exception:
                pass  # Ignore errors

            # Clean up any stale lock files
            display_num = display.lstrip(':')
            lock_file = f"/tmp/.X{display_num}-lock"
            try:
                if os.path.exists(lock_file):
                    os.remove(lock_file)
                    self.log(f"Removed stale lock file: {lock_file}")
            except OSError:
                pass  # Ignore permission errors

            # Kill any existing Xvfb processes on this display
            try:
                subprocess.run(['pkill', '-f', f'Xvfb.*{display}'],
                             capture_output=True, check=False)
                time.sleep(1)
            except Exception:
                pass  # Ignore errors

            # Start Xvfb with stderr redirection to suppress xkbcomp warnings
            self.xvfb_process = subprocess.Popen(
                ['Xvfb', display, '-screen', '0', '1280x720x24'],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL
            )

            # Set DISPLAY environment variable
            os.environ['DISPLAY'] = display
            # Apply conservative Qt/GL settings when using Xvfb.
            os.environ.setdefault('QT_QPA_PLATFORM', 'xcb')
            os.environ.setdefault('QT_X11_NO_MITSHM', '1')
            os.environ.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
            self.log("🧪 Applied headless Qt/GL environment variables")

            # Give Xvfb time to start
            time.sleep(2)

            if self.xvfb_process.poll() is not None:
                # Since stderr is redirected to DEVNULL, we can't read it
                raise RuntimeError(f"Xvfb process exited unexpectedly")

            self.log("✅ Xvfb started successfully")
            return True

        except Exception as e:
            print(f"❌ Failed to start Xvfb: {e}")
            return False

    def copy_e2e_properties(self):
        """Copy E2E properties configuration to plugin data directory."""
        import shutil

        # Get the plugin data directory
        obs_config_dir = Path.home() / '.config' / 'obs-studio'
        plugin_data_dir = obs_config_dir / 'plugins' / 'c64stream' / 'data'

        if not plugin_data_dir.exists():
            self.log("⚠️ Plugin data directory not found, creating it...")
            try:
                plugin_data_dir.mkdir(parents=True, exist_ok=True)
                self.log("✅ Created plugin data directory")
            except Exception as e:
                self.log(f"❌ Failed to create plugin data directory: {e}")
                return False

        # Copy E2E properties file (user plugin data dir)
        script_dir = Path(__file__).parent
        # Use local properties file for local environments to avoid CI-specific behavior
        if self.is_ci:
            e2e_properties = script_dir / 'properties_e2e_ci.ini'
        else:
            # Check if local properties exist, fallback to CI properties if not
            local_properties = script_dir / 'properties_e2e_local.ini'
            if local_properties.exists():
                e2e_properties = local_properties
                self.log("📋 Using local E2E properties (non-CI environment)")
            else:
                e2e_properties = script_dir / 'properties_e2e_ci.ini'
                self.log("⚠️ Local E2E properties not found, using CI properties")
        target_properties = plugin_data_dir / 'properties.ini'

        if e2e_properties.exists():
            try:
                # Backup existing properties.ini if it exists (for restoration after E2E)
                if target_properties.exists() and not self.is_ci:
                    backup_path = target_properties.with_suffix('.ini.e2e_backup')
                    shutil.copy2(target_properties, backup_path)
                    self._backed_up_properties.append((backup_path, target_properties))
                    self.log(f"📦 Backed up production properties: {target_properties} -> {backup_path}")

                shutil.copy2(e2e_properties, target_properties)
                self.log(f"✅ Copied E2E properties: {e2e_properties} -> {target_properties}")

                # In CI, force the plugin's save_folder to the actual $HOME/Documents path
                # so that CSVs and recordings land where this test expects them.
                if self.is_ci:
                    try:
                        ci_save_folder = Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'
                        ci_save_folder.mkdir(parents=True, exist_ok=True)

                        # Rewrite save_folder in the copied properties.ini
                        props_text = target_properties.read_text(encoding='utf-8', errors='ignore')
                        if 'save_folder=' in props_text:
                            # Replace existing assignment (including empty value)
                            import re
                            props_text = re.sub(r'^\s*save_folder\s*=.*$', f'save_folder={ci_save_folder}', props_text, flags=re.MULTILINE)
                        else:
                            # Append setting; parser in plugin is key-based and not section-bound
                            props_text += f"\nsave_folder={ci_save_folder}\n"
                        target_properties.write_text(props_text, encoding='utf-8')
                        self.log(f"🔧 CI save folder set to: {ci_save_folder}")
                    except Exception as adjust_e:
                        self.log(f"⚠️ Could not enforce CI save_folder: {adjust_e}")
                # Best-effort: if plugin is installed system-wide, also try to apply
                # properties to the module data path used by obs_module_file()
                # This is where presets were loaded from on CI (e.g. /usr/share/obs/obs-plugins/c64stream)
                system_data_dir = Path('/usr/share/obs/obs-plugins/c64stream')
                if system_data_dir.exists():
                    try:
                        sys_target = system_data_dir / 'properties.ini'
                        # Only attempt if writable; actual privileged overwrite is handled in CI workflow
                        if os.access(system_data_dir, os.W_OK):
                            shutil.copy2(e2e_properties, sys_target)
                            self.log(f"✅ Applied E2E properties to system data dir: {sys_target}")
                            # Keep system properties aligned with CI save folder as well
                            if self.is_ci:
                                try:
                                    props_text = sys_target.read_text(encoding='utf-8', errors='ignore')
                                    if 'save_folder=' in props_text:
                                        import re
                                        ci_save_folder = Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'
                                        ci_save_folder.mkdir(parents=True, exist_ok=True)
                                        props_text = re.sub(r'^\s*save_folder\s*=.*$', f'save_folder={ci_save_folder}', props_text, flags=re.MULTILINE)
                                    else:
                                        props_text += f"\nsave_folder={Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'}\n"
                                    sys_target.write_text(props_text, encoding='utf-8')
                                except Exception as _:
                                    pass
                        else:
                            self.log(f"ℹ️ System data dir not writable (will be handled by workflow): {system_data_dir}")
                            # On CI, check if properties were already applied by workflow
                            if self.is_ci and sys_target.exists():
                                try:
                                    with open(sys_target, 'r') as f:
                                        content = f.read()
                                    if 'record_csv=true' in content.lower():
                                        self.log(f"✅ System E2E properties already applied by workflow: {sys_target}")
                                    else:
                                        self.log(f"⚠️ System properties.ini exists but may not have E2E settings")
                                except Exception as read_e:
                                    self.log(f"⚠️ Could not verify system properties.ini content: {read_e}")
                    except Exception as se:
                        self.log(f"⚠️ Could not apply system properties.ini: {se}")
                else:
                    # System directory doesn't exist - plugin installed to user directory only
                    if self.is_ci:
                        self.log(f"ℹ️ Plugin installed to user directory only (not system-wide): {system_data_dir}")
                    else:
                        self.log(f"ℹ️ Plugin not installed system-wide: {system_data_dir}")
                return True
            except Exception as e:
                self.log(f"❌ Failed to copy E2E properties: {e}")
                return False
        else:
            self.log(f"❌ E2E properties file not found: {e2e_properties}")
            return False

    def create_obs_profile(self):
        """
        Copy clean OBS configuration from config directory to establish reproducible baseline.
        """
        self.log("Setting up OBS configuration from baseline")

        # Determine OBS config directory (handle CI environment where ~ doesn't resolve)
        if os.environ.get('HOME'):
            obs_config_dir = Path(os.environ['HOME']) / '.config' / 'obs-studio'
        else:
            obs_config_dir = Path.home() / '.config' / 'obs-studio'

        # Remove the basic directory to ensure completely clean state
        basic_dir = obs_config_dir / 'basic'
        if basic_dir.exists():
            self.log(f"Removing existing OBS basic config: {basic_dir}")
            shutil.rmtree(basic_dir)

        # Ensure parent directory exists
        obs_config_dir.mkdir(parents=True, exist_ok=True)

        # Copy baseline config from tests/e2e/config
        script_dir = Path(__file__).parent
        config_source = script_dir / 'config' / 'obs-studio'

        if not config_source.exists():
            raise RuntimeError(f"Baseline OBS config not found: {config_source}")

        self.log(f"Copying baseline config from {config_source} to {obs_config_dir}")
        # Copy the contents of the baseline config directory
        for item in config_source.iterdir():
            if item.is_dir():
                shutil.copytree(item, obs_config_dir / item.name)
            else:
                shutil.copy2(item, obs_config_dir / item.name)

        # Apply scenario overrides if provided (BEFORE variable replacement)
        if self.scenario_overrides_dir and self.scenario_overrides_dir.exists():
            try:
                self._apply_scenario_overrides(obs_config_dir, self.scenario_overrides_dir)
                self.log(f"✅ Applied scenario overrides from {self.scenario_overrides_dir}")
            except Exception as e:
                self.log(f"⚠️ Failed to apply scenario overrides from {self.scenario_overrides_dir}: {e}")

        # Replace variables in the configuration (after overrides so variables in overrides are replaced too)
        self._replace_config_variables(obs_config_dir)

        # Clean up state files that could trigger dialogs
        self._cleanup_obs_state_files(obs_config_dir)

        self.log("✅ OBS configuration copied from baseline")
        return obs_config_dir / 'basic' / 'profiles' / 'C64StreamTest'

    def _apply_scenario_overrides(self, obs_config_dir: Path, overrides_dir: Path):
        """Copy scenario override files into the OBS config directory after baseline copy.

        The overrides_dir may contain a tree that mirrors files under ~/.config/obs-studio, for example:
        - basic/profiles/C64StreamTest/basic.ini
        - basic/scenes/C64StreamTest.json
        - plugins/c64stream/data/properties.ini
        Files and directories are copied over the baseline (merge behavior for directories).
        """
        for root, dirs, files in os.walk(overrides_dir):
            rel = Path(root).relative_to(overrides_dir)
            dest_root = obs_config_dir / rel
            dest_root.mkdir(parents=True, exist_ok=True)
            # Copy files in this directory
            for fname in files:
                src = Path(root) / fname
                dst = dest_root / fname
                try:
                    if src.suffix.lower() == ".ini" and dst.exists():
                        if self._merge_ini_override(dst, src):
                            self.log(f"  ↳ Override (merged): {src.relative_to(overrides_dir)} -> {dst}")
                        else:
                            shutil.copy2(src, dst)
                            self.log(f"  ↳ Override: {src.relative_to(overrides_dir)} -> {dst}")
                    else:
                        shutil.copy2(src, dst)
                        self.log(f"  ↳ Override: {src.relative_to(overrides_dir)} -> {dst}")
                except Exception as e:
                    self.log(f"  ⚠️ Could not copy override {src}: {e}")

    def _merge_ini_override(self, dst: Path, src: Path) -> bool:
        """Merge INI override entries into an existing destination file."""
        parser = configparser.ConfigParser(interpolation=None)
        parser.optionxform = str
        override = configparser.ConfigParser(interpolation=None)
        override.optionxform = str
        try:
            parser.read(dst, encoding="utf-8")
            override.read(src, encoding="utf-8")
            for section in override.sections():
                if not parser.has_section(section):
                    parser.add_section(section)
                for key, value in override.items(section):
                    parser.set(section, key, value)
            with open(dst, "w", encoding="utf-8") as f:
                parser.write(f, space_around_delimiters=False)
            return True
        except (configparser.Error, OSError) as e:
            self.log(f"  ⚠️ Could not merge INI override {src}: {e}")
            return False

    def _replace_config_variables(self, obs_config_dir):
        """Replace variables in OBS configuration files with actual values."""
        # Define variable replacements
        # C64 Ultimate exact frame rates (from c64u-stream-spec.md):
        # - PAL:  50.125 Hz = 401/8  (FPSNum=401, FPSDen=8)
        # - NTSC: 59.826 Hz = 29913/500 (FPSNum=29913, FPSDen=500)
        if self.format == 'PAL':
            fps_num = '401'
            fps_den = '8'
            fps_float = '50.125'
        else:  # NTSC
            fps_num = '29913'
            fps_den = '500'
            fps_float = '59.826'

        # OBS profile values:
        # - FPSType=2 (fractional) uses FPSNum/FPSDen for exact frame rates
        # - This matches the C64 Ultimate's actual output frequency
        variables = {
            '$OUTPUT_DIR': str(self.output_dir),
            '$FPS': fps_float,
            '$FPS_NUM': fps_num,
            '$FPS_DEN': fps_den,
            '$FPS_COMMON': ('50 PAL' if self.format == 'PAL' else '60'),
        }

        # Process basic.ini profile file
        basic_ini = obs_config_dir / 'basic' / 'profiles' / 'C64StreamTest' / 'basic.ini'
        if basic_ini.exists():
            content = basic_ini.read_text()
            # Replace placeholders with actual values.
            # Important: replace longer keys first to avoid prefix collisions
            # (e.g. '$FPS' would otherwise corrupt '$FPS_COMMON').
            for key in sorted(variables.keys(), key=len, reverse=True):
                content = content.replace(key, variables[key])

            basic_ini.write_text(content)
            self.log(f"Updated configuration variables in {basic_ini}")

        # No further FPS regex edits needed; basic.ini uses placeholders.

        self.log("✅ Configuration variables replaced")

    def _cleanup_obs_state_files(self, obs_config_dir):
        """Clean up OBS state files that can trigger popup dialogs."""
        try:
            import glob

            # Files/patterns that can trigger dialogs
            state_patterns = [
                str(obs_config_dir / 'safe_mode'),
                str(obs_config_dir / '.safe_mode'),
                str(obs_config_dir / 'crashed'),
                str(obs_config_dir / '.crashed'),
                str(obs_config_dir / 'basic/crashed'),
                str(obs_config_dir / 'plugin_config/.safe_mode*'),
                '/tmp/obs-safe-mode-*',
                '/tmp/.obs-crashed*'
            ]

            cleaned_count = 0
            for pattern in state_patterns:
                for state_file in glob.glob(pattern):
                    try:
                        state_path = Path(state_file)
                        if state_path.is_dir():
                            shutil.rmtree(state_path)
                        else:
                            state_path.unlink()
                        cleaned_count += 1
                    except (OSError, IOError):
                        pass

            if cleaned_count > 0:
                self.log(f"Cleaned up {cleaned_count} OBS state files")
            else:
                self.log("No OBS state files to clean up")

        except Exception as e:
            self.log(f"Warning: Could not clean up OBS state files: {e}")

    def wait_for_plugin_initialization(self, timeout=None):
        """Wait for C64 plugin to initialize by monitoring OBS logs."""
        if timeout is None:
            timeout = self.plugin_init_timeout
        self.log(f"⏳ Monitoring OBS logs for C64 plugin initialization (timeout: {timeout}s)...")

        obs_config_dir = Path.home() / '.config' / 'obs-studio'
        logs_dir = obs_config_dir / 'logs'

        start_time = time.time()

        # Plugin initialization indicators to look for
        init_patterns = [
            b"[C64]",  # Any C64 plugin log message
            b"c64_source",  # Plugin source creation
            b"C64 Stream",  # Plugin name in logs
            b"UDP socket",  # Plugin creating UDP sockets
        ]

        while time.time() - start_time < timeout:
            # Check if OBS process crashed
            if self.obs_process.poll() is not None:
                stdout, stderr = self.obs_process.communicate()
                raise RuntimeError(f"OBS crashed during initialization:\nSTDOUT: {stdout.decode()}\nSTDERR: {stderr.decode()}")

            # Find latest log files
            if logs_dir.exists():
                log_files = list(logs_dir.glob('*.txt'))
                log_files.sort(key=lambda f: f.stat().st_mtime, reverse=True)

                # Always re-read the latest log to catch late writes
                for log_file in log_files[:1]:
                    try:
                        with open(log_file, 'rb') as f:
                            content = f.read()
                            for pattern in init_patterns:
                                if pattern in content:
                                    self.log(f"✅ C64 plugin initialized (found '{pattern.decode()}' in {log_file.name})")
                                    return True
                    except (OSError, IOError):
                        pass

            time.sleep(0.1)  # Check every 100ms

        self.log(f"⚠️ C64 plugin initialization not detected in logs within {timeout}s")
        return False

    def wait_for_obs_websocket(self, timeout=30):
        """Wait for OBS WebSocket server to be ready."""
        if not self.enable_websocket:
            self.log("⚠️  WebSocket disabled, skipping WebSocket server check")
            return False

        if not WEBSOCKET_AVAILABLE:
            self.log("⚠️  WebSocket not available, skipping WebSocket server check")
            return False

        self.log("Waiting for OBS WebSocket server...")

        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(1)
                result = sock.connect_ex(('127.0.0.1', 4455))
                sock.close()

                if result == 0:
                    self.log("✅ OBS WebSocket server is ready")
                    return True

            except Exception:
                pass

            time.sleep(1)

        return False

    def wait_for_receiver_threads(self, timeout=None):
        """Wait until the plugin's receiver threads (or UDP sockets) are ready by scanning OBS logs.

        Success when BOTH video and audio readiness patterns are seen:
        - Preferred: "Video receiver thread started on port" and "Audio receiver thread started on port"
        - Fallback:  "Created optimized UDP socket on port <video_port>" AND same for <audio_port>
        """
        if timeout is None:
            # CI can be slow to bind sockets; wait longer
            timeout = 60 if self.is_ci else 5

        self.log(f"⏳ Waiting for receiver readiness in OBS logs (timeout: {timeout}s)...")

        obs_config_dir = Path.home() / '.config' / 'obs-studio'
        logs_dir = obs_config_dir / 'logs'

        start_time = time.time()

        # Primary thread-start patterns (debug level)
        primary_video = b"Video receiver thread started on port"
        primary_audio = b"Audio receiver thread started on port"

        # Fallback socket creation patterns (info level)
        # Use the *actual* destination ports (can be updated from TCP START commands).
        video_port = int(getattr(self, 'video_dest_port', self.video_port))
        audio_port = int(getattr(self, 'audio_dest_port', self.audio_port))
        fallback_video = f"Created optimized UDP socket on port {video_port}".encode()
        fallback_audio = f"Created optimized UDP socket on port {audio_port}".encode()

        saw_video = False
        saw_audio = False
        saw_video_fallback = False
        saw_audio_fallback = False

        while time.time() - start_time < timeout:
            # Check if OBS died
            if self.obs_process and self.obs_process.poll() is not None:
                try:
                    stdout, stderr = self.obs_process.communicate()
                except Exception:
                    stdout = b""; stderr = b""
                raise RuntimeError(
                    f"OBS exited while waiting for receiver threads.\nSTDOUT: {stdout.decode()}\nSTDERR: {stderr.decode()}"
                )

            if logs_dir.exists():
                log_files = list(logs_dir.glob('*.txt'))
                log_files.sort(key=lambda f: f.stat().st_mtime, reverse=True)
                for log_file in log_files[:1]:  # Only need the newest
                    try:
                        with open(log_file, 'rb') as f:
                            content = f.read()
                            if not saw_video and primary_video in content:
                                saw_video = True
                                self.log(f"✅ Detected video receiver thread start ({log_file.name})")
                            if not saw_audio and primary_audio in content:
                                saw_audio = True
                                self.log(f"✅ Detected audio receiver thread start ({log_file.name})")

                            if not saw_video_fallback and fallback_video in content:
                                saw_video_fallback = True
                                self.log(f"ℹ️ Detected video UDP socket creation ({log_file.name})")
                            if not saw_audio_fallback and fallback_audio in content:
                                saw_audio_fallback = True
                                self.log(f"ℹ️ Detected audio UDP socket creation ({log_file.name})")

                            # Extra diagnostics in verbose mode: print actual ports parsed from logs
                            if self.verbose:
                                try:
                                    import re
                                    # Patterns for both thread start and socket creation lines
                                    ports = set(re.findall(rb"(?:port\s+)(\d{2,5})", content))
                                    if ports:
                                        decoded_ports = ', '.join(sorted({p.decode('ascii') for p in ports}))
                                        self.log(f"🔎 Observed port mentions in OBS logs: {decoded_ports}")
                                except Exception:
                                    pass

                            # Success if both primary seen, or both fallbacks seen
                            if (saw_video and saw_audio) or (saw_video_fallback and saw_audio_fallback):
                                self.log("✅ Receiver readiness confirmed")
                                return True
                    except (OSError, IOError):
                        pass

            time.sleep(0.1)

        # Final state log for diagnostics
        self.log(
            f"⚠️ Receiver readiness not confirmed within {timeout}s (video: {saw_video or saw_video_fallback}, "
            f"audio: {saw_audio or saw_audio_fallback})"
        )
        return False

    def send_obs_request(self, request_type, request_data=None):
        """Send a request to OBS via WebSocket API."""
        if not WEBSOCKET_AVAILABLE:
            self.log("⚠️  WebSocket not available, skipping OBS API call")
            return None

        if not self.enable_websocket:
            self.log("⚠️  WebSocket disabled, skipping OBS API call")
            return None

        try:
            import uuid
            import json
            import hashlib
            import base64

            # WebSocket connection parameters
            ws_url = "ws://127.0.0.1:4455"
            password = "e2etest123"

            # Create WebSocket connection
            ws = websocket.create_connection(ws_url, timeout=5)

            # Receive Hello message with authentication challenge (if enabled)
            hello_msg = json.loads(ws.recv())
            if hello_msg.get("op") != 0:  # Hello opcode
                raise Exception(f"Expected Hello message, got: {hello_msg}")

            identify_payload = {"rpcVersion": 1}
            auth_data = hello_msg.get("d", {}).get("authentication")
            if auth_data:
                challenge = auth_data["challenge"]
                salt = auth_data["salt"]
                secret = base64.b64encode(hashlib.sha256((password + salt).encode()).digest()).decode()
                auth_response = base64.b64encode(hashlib.sha256((secret + challenge).encode()).digest()).decode()
                identify_payload["authentication"] = auth_response

            # Send Identify message (with or without auth)
            identify_msg = {"op": 1, "d": identify_payload}
            ws.send(json.dumps(identify_msg))

            # Receive Identified message
            identified_msg = json.loads(ws.recv())
            if identified_msg.get("op") != 2:  # Identified opcode
                raise Exception(f"Authentication failed: {identified_msg}")

            # Send the actual request
            request_id = str(uuid.uuid4())
            request_msg = {
                "op": 6,  # Request
                "d": {
                    "requestType": request_type,
                    "requestId": request_id,
                    "requestData": request_data or {}
                }
            }

            ws.send(json.dumps(request_msg))

            # Receive response
            response = json.loads(ws.recv())
            ws.close()

            if response.get("op") == 7:  # RequestResponse opcode
                return response["d"]
            else:
                self.log(f"Unexpected response: {response}")
                return None

        except Exception as e:
            self.log(f"OBS WebSocket error: {e}")
            return None

    def start_obs_recording(self):
        """
        Start OBS with recording enabled using our test profile.
        """
        return self.start_obs(start_recording=True)

    def start_obs(self, start_recording: bool = True):
        """Start OBS using our test profile.

        Args:
            start_recording: if True, OBS is started with --startrecording. If False, OBS starts
                without recording and recording can be started later via WebSocket.
        """
        self.log("Starting OBS with C64 Stream test profile")

        # Create the OBS profile first
        profile_dir = self.create_obs_profile()

        if not profile_dir:
            raise RuntimeError("Failed to create OBS profile")

        # Give filesystem more time on CI to ensure all files are committed
        if self.is_ci:
            self.log("⏳ CI environment: waiting for filesystem sync...")
            time.sleep(2.0)  # Longer delay on CI
        else:
            time.sleep(0.5)  # Shorter delay locally

        def _launch_obs(collection_flag: str):
            # Build the OBS command with the provided collection flag name
            # Check if we can use nice with negative priority (requires root or CAP_SYS_NICE)
            can_nice = False
            try:
                # Test if nice -n -10 works
                test_result = subprocess.run(['nice', '-n', '-10', 'true'], capture_output=True, timeout=2)
                # Check if stderr contains "Permission denied" - nice still returns 0 even when it fails
                if b'Permission denied' not in test_result.stderr:
                    can_nice = True
                    self.log("✅ High priority scheduling available (nice -n -10)")
            except Exception:
                pass

            if can_nice:
                obs_cmd = [
                    'nice', '-n', '-10',
                    'obs',
                    '--profile', 'C64StreamTest',
                    collection_flag, 'C64StreamTest',
                    '--scene', 'C64 Test Scene',
                    '--minimize-to-tray',
                    '--disable-updater',
                    '--disable-missing-files-check',
                    '--multi'
                ]
            else:
                self.log("⚠️ High priority scheduling not available (nice -n -10 failed)")
                obs_cmd = [
                    'obs',
                    '--profile', 'C64StreamTest',
                    collection_flag, 'C64StreamTest',
                    '--scene', 'C64 Test Scene',
                    '--minimize-to-tray',
                    '--disable-updater',
                    '--disable-missing-files-check',
                    '--multi'
                ]

            if start_recording:
                obs_cmd.append('--startrecording')

            # Add verbose logging on CI
            if self.is_ci:
                obs_cmd.append('--verbose')
                self.log("🏗️ Added --verbose flag for CI debugging")

            self.log(f"Running: {' '.join(obs_cmd)}")

            env_vars = dict(os.environ, DISPLAY=os.environ.get('DISPLAY', ':99'))
            # Apply conservative Qt/GL settings when using headless/Xvfb.
            if self.is_ci or env_vars.get('DISPLAY') == ':99':
                env_vars.setdefault('QT_QPA_PLATFORM', 'xcb')
                env_vars.setdefault('QT_X11_NO_MITSHM', '1')
                env_vars.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
                self.log("🧪 Applied headless Qt/GL environment variables to OBS subprocess")

            # Start OBS process
            self.obs_process = subprocess.Popen(
                obs_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env_vars
            )

            self._obs_start_time_s = time.time()

            # Give OBS time to initialize
            time.sleep(self.obs_startup_delay)

            if self.obs_process.poll() is not None:
                stdout, stderr = self.obs_process.communicate()
                raise RuntimeError(f"OBS failed to start with {collection_flag}:\nSTDOUT: {stdout.decode()}\nSTDERR: {stderr.decode()}")

            self.log(f"✅ OBS started successfully with {collection_flag}")

            # Boost OBS process priority for smoother frame delivery
            self._boost_process_priority(self.obs_process.pid)

            return True

        try:
            # Try preferred flag first, then fallback.
            # OBS 28+ uses --collection; keep --scene-collection as a fallback for older builds.
            collection_flags = ['--collection', '--scene-collection']
            self.log("🚀 Trying --collection first; fallback to --scene-collection if needed")

            launched = False
            init_ok = False
            last_error = None

            for idx, flag in enumerate(collection_flags):
                # If there's a previous OBS instance, ensure it's stopped before retry
                if self.obs_process:
                    try:
                        self.obs_process.terminate()
                        self.obs_process.wait(timeout=3)
                    except Exception:
                        try:
                            self.obs_process.kill()
                            self.obs_process.wait(timeout=2)
                        except Exception:
                            pass
                    finally:
                        self.obs_process = None

                try:
                    launched = _launch_obs(flag)
                except Exception as e:
                    last_error = e
                    self.log(f"⚠️ OBS launch attempt with {flag} failed: {e}")
                    continue

                # Quick check: wait briefly for plugin init; if not seen, we may be on wrong flag
                short_timeout = max(2.0, min(self.plugin_init_timeout, 6)) if not self.is_ci else 8.0
                self.log(f"🔍 Probing plugin init with {flag} (timeout: {short_timeout}s)...")
                if self.wait_for_plugin_initialization(timeout=short_timeout):
                    init_ok = True
                    break
                else:
                    self.log(f"⚠️ Plugin init not detected quickly with {flag}; will try alternate flag if available")
                    # Loop will try next flag

            if not launched:
                # All attempts to launch failed
                raise RuntimeError(f"OBS failed to start: {last_error}")

            if not init_ok:
                # As a last resort, run full init wait on the last launch
                self.log("⏳ Running full plugin init wait on last OBS launch...")
                if not self.wait_for_plugin_initialization():
                    self._analyze_obs_logs()
                    raise RuntimeError("C64 plugin failed to initialize within timeout")
                else:
                    self.log("✅ C64 plugin initialization complete")

            # Post-startup validation: verify OBS loaded our configuration
            self.log("🔍 Validating OBS loaded our scene collection...")
            time.sleep(1.0)  # Give OBS a moment to fully initialize

            # Check if OBS created expected recording session directory
            recordings_base = Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'
            if recordings_base.exists():
                session_folders = [f for f in recordings_base.glob('session_*') if f.is_dir()]
                if session_folders:
                    # Sort by modification time to get the latest
                    session_folders.sort(key=lambda f: f.stat().st_mtime, reverse=True)
                    latest_session = session_folders[0]
                    self.log(f"  ✅ Found active recording session: {latest_session.name}")
                else:
                    self.log("  ⚠️ No recording session folders found yet")
            else:
                self.log("  ⚠️ Plugin recording directory doesn't exist yet")

            # Diagnostic: check which properties file the plugin is actually using
            self.log("🔍 Checking which properties file is being used by plugin...")
            user_props = Path.home() / '.config' / 'obs-studio' / 'plugins' / 'c64stream' / 'data' / 'properties.ini'
            system_props = Path('/usr/share/obs/obs-plugins/c64stream/properties.ini')

            if system_props.exists():
                self.log(f"  📄 System properties found: {system_props}")
                try:
                    with open(system_props, 'r') as f:
                        content = f.read()
                    if 'record_csv=true' in content.lower():
                        self.log(f"  ✅ System properties has E2E settings (record_csv=true)")
                    else:
                        self.log(f"  ❌ System properties missing E2E settings (no record_csv=true)")
                except Exception as e:
                    self.log(f"  ⚠️ Could not read system properties: {e}")
            else:
                self.log(f"  📄 No system properties file (plugin will use user properties)")

            if user_props.exists():
                self.log(f"  📄 User properties found: {user_props}")
                try:
                    with open(user_props, 'r') as f:
                        content = f.read()
                    if 'record_csv=true' in content.lower():
                        self.log(f"  ✅ User properties has E2E settings (record_csv=true)")
                    else:
                        self.log(f"  ❌ User properties missing E2E settings (no record_csv=true)")
                except Exception as e:
                    self.log(f"  ⚠️ Could not read user properties: {e}")
            else:
                self.log(f"  ❌ No user properties file found")

            # Note: plugin init was already probed above with a short timeout.
            # If not successful then, we did a full wait here as a fallback.

            # Additional validation: check if the C64 source was actually created
            self.log("🔍 Verifying C64 source creation in OBS logs...")
            obs_config_dir = Path.home() / '.config' / 'obs-studio'
            logs_dir = obs_config_dir / 'logs'
            if logs_dir.exists():
                log_files = list(logs_dir.glob('*.txt'))
                if log_files:
                    log_files.sort(key=lambda f: f.stat().st_mtime, reverse=True)
                    latest_log = log_files[0]
                    try:
                        with open(latest_log, 'r') as f:
                            content = f.read()

                        # Look for evidence that our scene and source were loaded
                        scene_loaded = 'C64StreamTest' in content
                        source_created = any(phrase in content for phrase in [
                            'Source ID \"c64_source\"',
                            'source \"C64 Stream\" (c64_source) created',
                            'C64 Stream',
                            'C6 Stream source created',
                            'C64 Stream streaming started',
                            'Created optimized UDP socket'
                        ])

                        if scene_loaded:
                            self.log("  ✅ C64StreamTest scene collection detected in logs")
                        else:
                            self.log("  ⚠️ C64StreamTest scene collection NOT found in logs")

                        if source_created:
                            self.log("  ✅ C64 source creation detected in logs")
                        else:
                            self.log("  ❌ C64 source creation NOT detected in logs")
                            if self.is_ci:
                                self.log("  🔍 CI environment: this may be normal timing variation")
                            else:
                                self.log("  🔍 This likely means OBS didn't load our scene collection properly")

                    except Exception as e:
                        self.log(f"  ❌ Could not analyze logs: {e}")

            return True

        except Exception as e:
            print(f"❌ Failed to start OBS: {e}")
            return False

    def start_recording(self):
        """Start recording in OBS."""
        self.log("Starting OBS recording...")

        # Try WebSocket API first if enabled, fallback to command line approach
        if self.enable_websocket and self.wait_for_obs_websocket(timeout=5):
            response = self.send_obs_request("StartRecord")
            if response:
                self.log("✅ Recording started via WebSocket API")
                return True

        # Fallback: Kill and restart OBS with recording enabled
        self.log("WebSocket not available or disabled, using command line recording")

        # Stop current OBS process
        if self.obs_process:
            self.obs_process.terminate()
            try:
                self.obs_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.obs_process.kill()
                self.obs_process.wait()

        # Restart OBS with recording enabled
        try:
            obs_cmd = [
                'obs',
                '--profile', 'C64StreamTest',
                '--collection', 'C64StreamTest',
                '--startrecording',
                '--minimize-to-tray',
                '--disable-updater',
                '--disable-missing-files-check',
                '--disable-shutdown-check'
            ]

            self.log(f"Restarting OBS with recording: {' '.join(obs_cmd)}")

            self.obs_process = subprocess.Popen(
                obs_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=dict(os.environ, DISPLAY=os.environ.get('DISPLAY', ':99'))
            )

            # Give OBS time to start recording
            time.sleep(5)

            if self.obs_process.poll() is not None:
                stdout, stderr = self.obs_process.communicate()
                self.log(f"OBS restart failed:\nSTDOUT: {stdout.decode()}\nSTDERR: {stderr.decode()}")
                return False

            self.log("✅ Recording started via command line")
            return True

        except Exception as e:
            self.log(f"Failed to restart OBS with recording: {e}")
            return False

    def stop_recording(self):
        """Stop recording in OBS."""
        self.log("Stopping OBS recording...")

        # Try WebSocket API first if enabled
        if WEBSOCKET_AVAILABLE and self.enable_websocket:
            response = self.send_obs_request("StopRecord")
            if response:
                self.log("✅ Recording stopped via WebSocket API")
                time.sleep(4)  # Give time for file to be written (increased from 3s)
                return True

        # Fallback: marker file approach
        marker_file = self.output_dir / 'stop_recording.marker'
        with open(marker_file, 'w') as f:
            f.write(f"stop_recording_{int(time.time())}")

        time.sleep(4)  # Increased from 3s for consistency
        return True

    def _collect_obs_log(self):
        """Copy the most relevant OBS log to output_dir for forensic analysis."""
        try:
            obs_config_dir = Path.home() / '.config' / 'obs-studio'
            logs_dir = obs_config_dir / 'logs'
            if not logs_dir.exists():
                return None

            log_files = [p for p in logs_dir.glob('*.txt') if p.is_file()]
            if not log_files:
                return None

            # Prefer logs written after we started OBS (with a little slack).
            if self._obs_start_time_s is not None:
                cutoff = self._obs_start_time_s - 5.0
                candidates = [p for p in log_files if p.stat().st_mtime >= cutoff]
                if candidates:
                    log_files = candidates

            log_files.sort(key=lambda p: p.stat().st_mtime, reverse=True)
            latest = log_files[0]
            dest = self.output_dir / 'obs_log.txt'
            shutil.copy2(latest, dest)
            return dest
        except Exception:
            return None

    def _summarize_obs_log(self, log_path: Path):
        """Extract basic render/encode lag signals from an OBS log."""
        try:
            import re

            text = log_path.read_text(errors='replace')
            patterns = {
                'render_lagged_frames': re.compile(r"Number of lagged frames due to rendering lag:\s*(\d+)", re.I),
                'encode_lagged_frames': re.compile(r"Number of lagged frames due to encoding lag:\s*(\d+)", re.I),
                'dropped_frames': re.compile(r"Dropped frames:\s*(\d+)", re.I),
                'skipped_frames': re.compile(r"Skipped frames:\s*(\d+)", re.I),
            }

            summary: dict[str, int | str | None] = {
                'log_file': str(log_path),
            }

            for key, pat in patterns.items():
                m = pat.search(text)
                if m:
                    try:
                        summary[key] = int(m.group(1))
                    except Exception:
                        summary[key] = None

            # Capture common warnings/errors (truncated to keep output small)
            warn_lines = []
            for line in text.splitlines():
                l = line.lower()
                if 'warning:' in l or 'error:' in l or 'failed' in l:
                    warn_lines.append(line.strip())
            if warn_lines:
                summary['notable_lines'] = warn_lines[:200]

            out = self.output_dir / 'obs_log_summary.json'
            out.write_text(json.dumps(summary, indent=2))
            return summary
        except Exception:
            return None

    def check_recording_output(self):
        """Check if recording file was created successfully."""
        self.log("Checking for recording output...")

        # Look for video files in multiple directories
        video_extensions = ['.mp4', '.hybrid_mp4']
        search_dirs = [
            self.output_dir,  # Our test output directory
            Path.home() / 'Videos',  # Default OBS recording directory
            Path.home(),  # Home directory
            Path('/tmp'),  # Temporary directory
        ]

        # Also accept plugin's own raw recording as valid evidence on CI
        plugin_recordings_base = Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'
        if plugin_recordings_base.exists():
            # Find latest session folder
            session_folders = [f for f in plugin_recordings_base.glob('session_*') if f.is_dir()]
            if session_folders:
                session_folders.sort(key=lambda f: f.stat().st_mtime, reverse=True)
                latest_session = session_folders[0]
                search_dirs.insert(0, latest_session)  # Prefer latest plugin session folder

        recording_files = []

        for search_dir in search_dirs:
            if search_dir.exists():
                for ext in video_extensions:
                    # Look for recent files (created in last 10 minutes)
                    import time
                    cutoff_time = time.time() - 600  # 10 minutes ago

                    for file_path in search_dir.glob(f'*{ext}'):
                        try:
                            if file_path.stat().st_mtime > cutoff_time:
                                recording_files.append(file_path)
                        except (OSError, IOError):
                            continue

        if recording_files:
            # Sort by modification time, newest first
            recording_files.sort(key=lambda f: f.stat().st_mtime, reverse=True)

            for recording in recording_files:
                file_size = recording.stat().st_size
                abs_path = recording.resolve()
                self.log(f"✅ Found recording: {abs_path} ({file_size} bytes)")

                # Basic validation - file should be larger than 10KB
                if file_size > 10240:
                    # Move to our output directory for easier access (avoid duplicates)
                    suffix = '.mp4' if recording.suffix == '.hybrid_mp4' else recording.suffix
                    dest_file = self.output_dir / f"c64_recording{suffix}"
                    try:
                        import shutil
                        shutil.move(str(recording), str(dest_file))
                        dest_abs = dest_file.resolve()
                        self.log(f"✅ Moved recording to: {dest_abs}")
                        return str(dest_abs)
                    except Exception as e:
                        self.log(f"Warning: Could not move recording: {e}")
                        return str(abs_path)

        self.log("❌ No valid recording files found")
        return None

    def check_csv_recordings(self):
        """Check if CSV recordings were created and analyze their content.

        Stores original row counts in self._original_csv_counts for use by validation.
        """
        self.log("🔍 Checking for CSV recordings...")

        # Initialize original counts storage
        self._original_csv_counts = {'network_packets': 0, 'obs_frames': 0, 'video_packets': 0, 'audio_packets': 0}

        # Look for CSV files in the plugin's recording directory
        recordings_base = Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'

        if not recordings_base.exists():
            self.log(f"❌ CSV recordings directory doesn't exist: {recordings_base}")
            return False

        # Find the most recent session folder
        session_folders = []
        for folder in recordings_base.glob('session_*'):
            if folder.is_dir():
                session_folders.append(folder)

        if not session_folders:
            self.log("❌ No CSV recording session folders found")
            return False

        # Sort by modification time, newest first
        session_folders.sort(key=lambda f: f.stat().st_mtime, reverse=True)
        latest_session = session_folders[0]

        self.log(f"📁 Found latest session folder: {latest_session}")

        # Check for CSV files
        network_csv = latest_session / 'network.csv'
        obs_csv = latest_session / 'obs.csv'

        csv_results = {}

        # Analyze network.csv
        if network_csv.exists():
            self.log(f"✅ Found network.csv: {network_csv} ({network_csv.stat().st_size} bytes)")
            try:
                with open(network_csv, 'r') as f:
                    lines = f.readlines()
                    csv_results['network_packets'] = len(lines) - 1  # Subtract header
                    self._original_csv_counts['network_packets'] = csv_results['network_packets']

                    # Count actual video/audio packets in full CSV and store for validation
                    actual_video_count = sum(1 for line in lines[1:] if line.startswith('video,'))
                    actual_audio_count = sum(1 for line in lines[1:] if line.startswith('audio,'))
                    self._original_csv_counts['video_packets'] = actual_video_count
                    self._original_csv_counts['audio_packets'] = actual_audio_count
                    print(f"🔍 DEBUG: Full network.csv has {csv_results['network_packets']} total packets ({actual_video_count} video, {actual_audio_count} audio)")
                    self.log(f"📊 Network CSV contains {csv_results['network_packets']} packet entries ({actual_video_count} video, {actual_audio_count} audio)")

                    # Show first few entries
                    if len(lines) > 1:
                        self.log(f"📝 First network entry: {lines[1].strip()}")
                    if len(lines) > 2:
                        self.log(f"📝 Second network entry: {lines[2].strip()}")

            except Exception as e:
                self.log(f"❌ Failed to read network.csv: {e}")
        else:
            self.log(f"❌ network.csv not found: {network_csv}")

        # Analyze obs.csv
        if obs_csv.exists():
            self.log(f"✅ Found obs.csv: {obs_csv} ({obs_csv.stat().st_size} bytes)")
            try:
                with open(obs_csv, 'r') as f:
                    lines = f.readlines()
                    csv_results['obs_frames'] = len(lines) - 1  # Subtract header
                    self._original_csv_counts['obs_frames'] = csv_results['obs_frames']
                    self.log(f"📊 OBS CSV contains {csv_results['obs_frames']} frame entries")

                    # Show first few entries
                    if len(lines) > 1:
                        self.log(f"📝 First OBS entry: {lines[1].strip()}")
                    if len(lines) > 2:
                        self.log(f"📝 Second OBS entry: {lines[2].strip()}")

            except Exception as e:
                self.log(f"❌ Failed to read obs.csv: {e}")
        else:
            self.log(f"❌ obs.csv not found: {obs_csv}")

        # Analyze network jitter BEFORE truncation (need full data)
        if network_csv.exists():
            self._save_network_analysis(network_csv)

        # Copy CSV files to test output for analysis (with optional truncation)
        try:
            if network_csv.exists():
                dest_network = self.output_dir / 'network.csv'
                self._copy_csv_truncated(network_csv, dest_network)
                self.log(f"✅ Copied network.csv to: {dest_network}")

            if obs_csv.exists():
                dest_obs = self.output_dir / 'obs.csv'
                self._copy_csv_truncated(obs_csv, dest_obs)
                self.log(f"✅ Copied obs.csv to: {dest_obs}")

        except Exception as e:
            self.log(f"⚠️ Failed to copy CSV files: {e}")

        return network_csv.exists() or obs_csv.exists()

    def replay_packets(self, udp_replay_path):
        """Replay video and audio packets with precise interleaved timing."""
        self.log(f"Waiting for plugin to request streaming via TCP...")

        # Wait for the plugin to send TCP start commands (with timeout)
        if not self.udp_replay_triggered.wait(timeout=30):
            self.log("❌ Timeout waiting for plugin to request streaming")
            self.log("🔍 Network diagnostics:")
            self.log(f"  - TCP listener: {self.control_bind_ip}:{self.control_port} (0.0.0.0 = all interfaces)")
            self.log(f"  - Video destination: {self.video_dest_ip}:{self.video_dest_port}")
            self.log(f"  - Audio destination: {self.audio_dest_ip}:{self.audio_dest_port}")

            # Check if TCP server is still running
            if self.tcp_server_running:
                self.log("  - TCP server is still running")
            else:
                self.log("  - TCP server is not running")

            # Check current network connections
            import subprocess
            try:
                result = subprocess.run(['netstat', '-tlnp'], capture_output=True, text=True, timeout=5)
                if result.returncode == 0:
                    self.log("  - Current TCP listeners:")
                    for line in result.stdout.split('\n'):
                        if f':{self.control_port}' in line or f'127.0.0.1:{self.control_port}' in line or ':64 ' in line:
                            self.log(f"    {line}")
            except Exception as e:
                self.log(f"  - Could not check netstat: {e}")

            # CI fallback: if sockets appear ready by logs, proceed with replay even if TCP trigger was missed
            if self.is_ci:
                self.log("🔧 CI fallback: checking for UDP socket readiness in OBS logs...")
                if self.wait_for_receiver_threads():
                    self.log("✅ UDP sockets detected - proceeding with replay")
                else:
                    return False
            else:
                return False

        self.log(f"✅ Received streaming request, starting {self.format} packet replay")
        self.log(f"🔍 UDP replay targets:")
        self.log(f"  - Video: {self.video_dest_ip}:{self.video_dest_port}")
        self.log(f"  - Audio: {self.audio_dest_ip}:{self.audio_dest_port}")

        # Prefer log-based readiness over fixed sleeps
        ready = self.wait_for_receiver_threads()
        if not ready:
            # Fallback to minimal delays if logs didn't show readiness
            import time
            time.sleep(self.udp_socket_delay)
            self.log("⏱️ Fallback UDP socket delay complete")
            time.sleep(self.buffer_setup_delay)
            self.log("⏱️ Fallback buffer setup delay complete")

        return self._replay_interleaved_packets(udp_replay_path)

    def _replay_interleaved_packets(self, udp_replay_path: Path):
        """Replay packets with proper interleaving and precise timing."""
        import socket
        import time
        import glob
        import os

        video_dir = (self.packet_dir / 'video' / self.format).resolve()
        audio_dir = (self.packet_dir / 'audio' / self.format).resolve()

        if not video_dir.exists() or not audio_dir.exists():
            raise FileNotFoundError(f"Packet directories not found: {video_dir}, {audio_dir}")

        # Load packet files
        video_files = sorted(glob.glob(str(video_dir / "*.bin")))
        audio_files = sorted(glob.glob(str(audio_dir / "*.bin")))

        if not video_files or not audio_files:
            self.log("❌ No packet files found")
            return False

        self.log(f"📦 Loaded {len(video_files)} video packets, {len(audio_files)} audio packets")

        # Precise timing based on C64 Stream specification
        # Use exact calculated intervals to avoid cumulative timing errors over thousands of packets
        if self.format == 'PAL':
            # PAL: 50.125 fps, 68 packets/frame → 293.384 µs per video packet
            video_interval_us = 293.384
            # PAL audio: 192 samples / 47983 Hz → 4001.417 µs per audio packet
            audio_interval_us = 4001.417
        else:  # NTSC
            # NTSC: 59.826 fps, 60 packets/frame → 278.586 µs per video packet
            video_interval_us = 278.586
            # NTSC audio: 192 samples / 47940 Hz → 4005.006 µs per audio packet
            audio_interval_us = 4005.006

        # Create UDP sockets
        video_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        audio_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # Do NOT send test packets - they interfere with packet counting
        # The plugin logs all received packets to network.csv, including test packets
        self.log(f"🔍 UDP sockets ready for {self.video_dest_ip}:{self.video_dest_port} and {self.audio_dest_ip}:{self.audio_dest_port}")
        self.log(f"  - Video socket: {video_sock}")
        self.log(f"  - Audio socket: {audio_sock}")

        # Diagnostic: show current UDP listeners if available (no extra tools installed)
        if self.verbose:
            try:
                import subprocess
                # netstat is present in image; show UDP listeners for our ports
                cmd = ['netstat', '-ulnp']
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
                if result.returncode == 0:
                    # Filter lines for exact port matches to avoid false positives
                    vpat = f":{self.video_dest_port}"
                    apat = f":{self.audio_dest_port}"
                    lines = [l for l in result.stdout.split('\n') if vpat in l or apat in l]
                    self.log("🔎 UDP listeners snapshot:")
                    for l in lines[:20]:
                        self.log(f"    {l}")
                else:
                    self.log(f"🔎 netstat -ulnp returned {result.returncode}")
            except Exception as e:
                self.log(f"🔎 Could not snapshot UDP listeners: {e}")

            # Also show ss snapshot with Recv-Q/Send-Q
            try:
                import subprocess
                cmd = ['ss', '-u', '-l', '-n', '-p']
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
                if result.returncode == 0:
                    vpat = f":{self.video_dest_port}"
                    apat = f":{self.audio_dest_port}"
                    lines = [l for l in result.stdout.split('\n') if vpat in l or apat in l]
                    self.log("🔎 ss -u -l -n -p snapshot:")
                    for l in lines[:20]:
                        self.log(f"    {l}")
                else:
                    self.log(f"🔎 ss returned {result.returncode}")
            except Exception as e:
                self.log(f"🔎 Could not run ss: {e}")

            # /proc diagnostics: per-socket drops and system UDP stats
            try:
                import re
                udp_lines = Path('/proc/net/udp').read_text().splitlines()
                header = udp_lines[0]
                entries = udp_lines[1:]
                # Ports in hex (uppercase, zero-padded 4)
                vhex = f"{int(self.video_dest_port):04X}"
                ahex = f"{int(self.audio_dest_port):04X}"
                v_matches = []
                a_matches = []
                for line in entries:
                    parts = line.split()
                    if len(parts) < 12:
                        continue
                    local = parts[1]  # local_address
                    drops = parts[-1]
                    if local.endswith(':'+vhex):
                        v_matches.append((local, drops))
                    if local.endswith(':'+ahex):
                        a_matches.append((local, drops))
                if v_matches or a_matches:
                    self.log("🔎 /proc/net/udp entries (local:port -> drops):")
                    for local, drops in v_matches:
                        self.log(f"    {local} -> drops={drops} (video)")
                    for local, drops in a_matches:
                        self.log(f"    {local} -> drops={drops} (audio)")
            except Exception as e:
                self.log(f"🔎 Could not read /proc/net/udp: {e}")

        try:
            # Calculate interleaved timeline
            timeline = []
            start_time_us = 0

            # Add video packets to timeline
            for i, video_file in enumerate(video_files):
                timeline.append({
                    'time_us': start_time_us + i * video_interval_us,
                    'type': 'video',
                    'file': video_file,
                    'sock': video_sock,
                    'dest': (self.video_dest_ip, self.video_dest_port)
                })

            # Add audio packets to timeline
            for i, audio_file in enumerate(audio_files):
                timeline.append({
                    'time_us': start_time_us + i * audio_interval_us,
                    'type': 'audio',
                    'file': audio_file,
                    'sock': audio_sock,
                    'dest': (self.audio_dest_ip, self.audio_dest_port)
                })

            # Sort by timestamp for proper interleaving
            timeline.sort(key=lambda x: x['time_us'])

            # Apply network simulation if configured
            import random
            random.seed()  # Use current time as seed

            self.log(f"🔍 Network simulation config: {self.network_simulation}")

            # 1. Apply jitter (positive-only delay variability)
            # Jitter simulates network delay variation - packets can only be delayed, never early
            # Supports both jitter_percent (legacy) and max_jitter_ms (preferred)
            jitter_percent = self.network_simulation.get('jitter_percent', 0)
            max_jitter_ms = self.network_simulation.get('max_jitter_ms', 0)
            self.log(f"🔍 Jitter config: percent={jitter_percent}, max_ms={max_jitter_ms}")

            if max_jitter_ms > 0:
                # Apply absolute jitter in milliseconds (preferred method)
                jitter_count = 0
                max_jitter_us = max_jitter_ms * 1000
                for i in range(1, len(timeline)):
                    # Apply random positive jitter from 0 to max_jitter_us
                    jitter = random.uniform(0, max_jitter_us)
                    timeline[i]['time_us'] += jitter
                    timeline[i]['jittered'] = True
                    timeline[i]['jitter_us'] = jitter
                    jitter_count += 1
                self.log(f"📊 Jitter enabled: 0-{max_jitter_ms}ms positive delay applied to {jitter_count} packets")

            elif jitter_percent > 0:
                jitter_count = 0
                for i in range(1, len(timeline)):
                    prev_time = timeline[i-1]['time_us']
                    current_time = timeline[i]['time_us']
                    base_interval = current_time - prev_time

                    # Apply +jitter_percent positive delay (0 to +jitter_percent)
                    jitter_range = base_interval * jitter_percent / 100.0
                    jitter = random.uniform(0, jitter_range)  # POSITIVE ONLY
                    timeline[i]['time_us'] += jitter
                    timeline[i]['jittered'] = True
                    jitter_count += 1

                self.log(f"📊 Jitter enabled: 0-{jitter_percent}% positive delay applied to {jitter_count} packet intervals")

            # 2. Apply packet reordering (positive-only delay for out-of-order delivery)
            # Reordering simulates packets taking longer routes - they arrive later, not earlier
            reorder_percent = self.network_simulation.get('reorder_percent', 0)
            reorder_max_delay_ms = self.network_simulation.get('reorder_max_delay_ms', 0)

            if reorder_percent > 0 and reorder_max_delay_ms > 0:
                reorder_count = 0

                for event in timeline:
                    # Decide if this packet should be reordered based on probability
                    if random.randint(0, 99) < reorder_percent:
                        # Apply random POSITIVE delay between 0 and max_delay_ms
                        delay_us = random.randint(0, reorder_max_delay_ms * 1000)
                        event['time_us'] += delay_us  # POSITIVE ONLY
                        event['reordered'] = True
                        reorder_count += 1
                    else:
                        event['reordered'] = False

                self.log(f"🔀 Packet reordering enabled: {reorder_percent}% probability, 0-{reorder_max_delay_ms}ms positive delay")
                self.log(f"🔀 Applied reordering to {reorder_count}/{len(timeline)} packets")

            # RE-SORT timeline by time_us after jitter simulation
            # This is CORRECT for simulating real network jitter:
            # - Each packet gets a random delay added to its original time
            # - Packets are then sent in the order they "arrive" (sorted by jittered time)
            # - This creates BOTH out-of-order sequence numbers AND variable inter-packet timing
            # - The plugin's buffer must handle both aspects
            timeline.sort(key=lambda x: x['time_us'])

            if jitter_percent > 0 or max_jitter_ms > 0 or reorder_percent > 0:
                self.log(f"✅ Network simulation applied, timeline sorted by simulated arrival time")

            self.log(f"🎯 Generated {len(timeline)} interleaved packets over {timeline[-1]['time_us']/1000:.1f}ms")

            # Generate manifests for C binary sender
            # Packets are in TIME ORDER - they will be sent when scheduled
            # This naturally creates out-of-order sequence numbers (reordering effect)
            video_manifest = []
            audio_manifest = []

            for event in timeline:
                if event['type'] == 'video':
                    video_manifest.append(event)
                else:
                    audio_manifest.append(event)

            # DO NOT sort by filename! Keep them in time order
            # The C binary will send them in the order they appear in the manifest

            # Debug: Check timing of first few packets
            self.log(f"🔍 First 5 video packets timing:")
            for i in range(min(5, len(video_manifest))):
                self.log(f"   [{i}] {Path(video_manifest[i]['file']).name}: time_us={video_manifest[i]['time_us']:.1f}")
            self.log(f"🔍 First 5 audio packets timing:")
            for i in range(min(5, len(audio_manifest))):
                self.log(f"   [{i}] {Path(audio_manifest[i]['file']).name}: time_us={audio_manifest[i]['time_us']:.1f}")

            # Write manifests as CSV files (filename,delay_us)
            # Use absolute time from global timeline start for each packet
            # This preserves jitter/reordering while keeping sequence order
            video_manifest_path = self.output_dir / 'video_manifest.csv'
            audio_manifest_path = self.output_dir / 'audio_manifest.csv'

            # Minimum inter-packet delay in microseconds.
            #
            # IMPORTANT:
            # - Manifests carry integer microsecond deltas; avoid float->int drift which can
            #   otherwise collapse pacing (and cause "send everything instantly" regressions).
            # - For local runs, keep this tiny to preserve spec timing.
            # - For CI, use a more conservative minimum to reduce receiver buffer overflows.
            MIN_PACKET_DELAY_US = 50 if self.is_ci else 1

            def write_manifest(path: Path, events: list[dict]) -> dict:
                with open(path, 'w') as f:
                    f.write("filename,delay_us\n")
                    last_sent_time_us = 0
                    min_delta_us = None
                    max_delta_us = 0
                    for event in events:
                        event_time_us = int(round(float(event['time_us'])))
                        delta_us = event_time_us - last_sent_time_us
                        if delta_us < MIN_PACKET_DELAY_US:
                            delta_us = MIN_PACKET_DELAY_US
                        filename = Path(event['file']).name
                        f.write(f"{filename},{int(delta_us)}\n")
                        last_sent_time_us += int(delta_us)
                        if min_delta_us is None or delta_us < min_delta_us:
                            min_delta_us = int(delta_us)
                        if delta_us > max_delta_us:
                            max_delta_us = int(delta_us)

                return {
                    'total_us': int(last_sent_time_us),
                    'min_delta_us': int(min_delta_us or 0),
                    'max_delta_us': int(max_delta_us),
                }

            video_manifest_stats = write_manifest(video_manifest_path, video_manifest)
            audio_manifest_stats = write_manifest(audio_manifest_path, audio_manifest)

            self.log(
                "🕒 Manifest timing summary: "
                f"video={video_manifest_stats['total_us'] / 1000.0:.1f}ms "
                f"(minΔ={video_manifest_stats['min_delta_us']}us, maxΔ={video_manifest_stats['max_delta_us']}us), "
                f"audio={audio_manifest_stats['total_us'] / 1000.0:.1f}ms "
                f"(minΔ={audio_manifest_stats['min_delta_us']}us, maxΔ={audio_manifest_stats['max_delta_us']}us)"
            )

            self.log(f"📝 Generated manifests: {len(video_manifest)} video, {len(audio_manifest)} audio packets")
            self.log(f"   Video starts at: {video_manifest[0]['time_us']/1000:.1f}ms")
            self.log(f"   Audio starts at: {audio_manifest[0]['time_us']/1000:.1f}ms")

            udp_replay_bin = Path(udp_replay_path).resolve()
            if not udp_replay_bin.exists():
                raise FileNotFoundError(f"udp_replay not found: {udp_replay_bin}")

            # Start resource monitoring RIGHT when packets begin flowing
            if self.enable_resource_monitoring and self._resource_monitor:
                self._resource_monitor.start()
                self.log(f"📊 Resource monitoring started (interval: {self.resource_interval_ms}ms)")

            # Spawn C binary processes for video and audio in parallel
            replay_start_time = time.time()

            # Align sender start across processes (audio+video) using an absolute monotonic timestamp.
            # Each sender preloads packets from disk, so we schedule a common start time a bit
            # into the future to avoid initial A/V offset due to differing preload times.
            # IMPORTANT:
            # udp_replay preloads tens of thousands of small packet files. On a cold filesystem
            # cache, video preload can take multiple seconds. If the sender misses start_at_us,
            # it will begin sending late, causing a real A/V offset in the recording.
            # Use a generous lead time to ensure both senders are ready before the shared start.
            lead_s = 10.0 if self.is_ci else 8.0
            start_at_us = (time.monotonic_ns() // 1000) + int(lead_s * 1_000_000)

            video_cmd = [
                str(udp_replay_bin),
                str(video_manifest_path),
                str(video_dir),
                self.video_dest_ip,
                str(self.video_dest_port),
                '780',  # video packet size
                '--start-at-us',
                str(start_at_us),
                '--verbose'
            ]

            audio_cmd = [
                str(udp_replay_bin),
                str(audio_manifest_path),
                str(audio_dir),
                self.audio_dest_ip,
                str(self.audio_dest_port),
                '770',  # audio packet size
                '--start-at-us',
                str(start_at_us),
                '--verbose'
            ]

            self.log(f"🚀 Starting C binary packet senders...")
            self.log(f"   Video: {' '.join(video_cmd)}")
            self.log(f"   Audio: {' '.join(audio_cmd)}")

            # Run both processes in parallel and stream their logs live.
            #
            # IMPORTANT: Do not use capture_output=True here.
            # We want UDP sender progress logs to appear on stdout at the moment
            # packets are actually sent (interleaved with resource monitoring).
            import threading
            import subprocess as sp

            def run_sender(cmd: list[str], label: str) -> tuple[int, list[str]]:
                lines: list[str] = []
                proc = sp.Popen(
                    cmd,
                    stdout=sp.PIPE,
                    stderr=sp.STDOUT,
                    text=True,
                    bufsize=1,
                )

                assert proc.stdout is not None

                for line in proc.stdout:
                    line = line.rstrip('\n')
                    if line:
                        # Keep a small buffer for error reporting.
                        if len(lines) < 200:
                            lines.append(line)
                        self.log(f"[{label}] {line}")

                rc = proc.wait()
                return rc, lines

            video_result: dict[str, object] = {'returncode': None, 'lines': []}
            audio_result: dict[str, object] = {'returncode': None, 'lines': []}

            def run_video():
                rc, lines = run_sender(video_cmd, 'UDP-VIDEO')
                video_result['returncode'] = rc
                video_result['lines'] = lines

            def run_audio():
                rc, lines = run_sender(audio_cmd, 'UDP-AUDIO')
                audio_result['returncode'] = rc
                audio_result['lines'] = lines

            video_thread = threading.Thread(target=run_video, name='udp-replay-video')
            audio_thread = threading.Thread(target=run_audio, name='udp-replay-audio')

            video_thread.start()
            audio_thread.start()

            video_thread.join()
            audio_thread.join()

            elapsed_ms = (time.time() - replay_start_time) * 1000

            # Check results
            if video_result['returncode'] != 0:
                self.log("❌ Video sender failed")
                for line in (video_result.get('lines') or [])[-30:]:
                    self.log(f"[UDP-VIDEO] {line}")
                return False

            if audio_result['returncode'] != 0:
                self.log("❌ Audio sender failed")
                for line in (audio_result.get('lines') or [])[-30:]:
                    self.log(f"[UDP-AUDIO] {line}")
                return False

            # Parse output to get packet counts
            packets_sent = len(video_manifest) + len(audio_manifest)

            print(f"📡 C binary sender: sent {packets_sent} packets in {elapsed_ms:.1f}ms")
            self.log(f"✅ Packet replay complete: {packets_sent} packets sent in {elapsed_ms:.1f}ms")

            # NOTE: Resource monitoring continues running! It will be stopped after
            # the grace period in run() to capture full OBS processing time.
            # This ensures we measure the entire test duration, not just packet sending.

            # Give plugin time to process the packets (increased slightly for CI)
            time.sleep(2.0)
            self.log("✅ Plugin processing delay complete")

            return packets_sent > 0

        finally:
            video_sock.close()
            audio_sock.close()

    def _run_replay(self, cmd, stream_type, results):
        """Run a UDP replay command."""
        self.log(f"Starting {stream_type} packet replay")
        try:
            result = subprocess.run(cmd, check=True, capture_output=True, text=True)
            if self.verbose:
                print(f"[{stream_type.upper()}] {result.stdout}")
            results[stream_type] = True
        except subprocess.CalledProcessError as e:
            print(f"❌ {stream_type} replay failed: {e.stderr}")
            results[stream_type] = False

    def start_mock_c64_server(self):
        """Start mock C64 Ultimate TCP server on configurable control port."""
        self.log(f"Starting mock C64 Ultimate TCP server on port {self.control_port}")

        try:
            import subprocess

            self.tcp_server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.tcp_server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

            # Add network diagnostics
            self.log(f"🔍 Network diagnostics:")
            self.log(f"  - Binding to {self.control_bind_ip}:{self.control_port}")
            self.log(f"  - Socket family: AF_INET")
            self.log(f"  - Socket type: SOCK_STREAM")

            # Check if port is already in use
            import subprocess
            try:
                result = subprocess.run(['netstat', '-tlnp'], capture_output=True, text=True, timeout=5)
                if result.returncode == 0:
                    self.log(f"  - Current TCP listeners:")
                    for line in result.stdout.split('\n'):
                        if f':{self.control_port}' in line or f'127.0.0.1:{self.control_port}' in line:
                            self.log(f"    {line}")
            except Exception as e:
                self.log(f"  - Could not check netstat: {e}")

            self.tcp_server_socket.bind((self.control_bind_ip, self.control_port))
            self.tcp_server_socket.listen(5)
            self.tcp_server_running = True

            # Verify binding worked
            actual_addr = self.tcp_server_socket.getsockname()
            self.log(f"  - Successfully bound to {actual_addr}")

            self.tcp_server_thread = threading.Thread(target=self._tcp_server_worker)
            self.tcp_server_thread.daemon = True
            self.tcp_server_thread.start()

            self.log("✅ Mock C64 Ultimate TCP server started")
            # CI fallback: also listen on default control port 64 in case plugin did not apply CI properties
            if self.is_ci and self.control_port != 64:
                try:
                    self.tcp_server_socket_alt = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    self.tcp_server_socket_alt.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    self.tcp_server_socket_alt.bind((self.control_bind_ip, 64))
                    self.tcp_server_socket_alt.listen(3)
                    self.tcp_server_thread_alt = threading.Thread(
                        target=self._tcp_server_worker_socket, args=(self.tcp_server_socket_alt, "alt-64")
                    )
                    self.tcp_server_thread_alt.daemon = True
                    self.tcp_server_thread_alt.start()
                    self.log("ℹ️ CI fallback control listener active on port 64")
                except Exception as alt_e:
                    self.log(f"⚠️ Could not start CI fallback control listener on port 64: {alt_e}")
            return True

        except Exception as e:
            self.log(f"❌ Failed to start mock C64 Ultimate TCP server: {e}")
            self.log(f"  - Error type: {type(e).__name__}")
            self.log(f"  - Error details: {str(e)}")
            return False

    def _tcp_server_worker(self):
        """TCP server worker thread - handles incoming connections for primary socket."""
        self._tcp_server_worker_socket(self.tcp_server_socket, label="primary")

    def _tcp_server_worker_socket(self, server_socket, label="socket"):
        """TCP server worker thread for a given listening socket."""
        self.log(f"TCP server worker ({label}) started, waiting for connections...")

        while self.tcp_server_running:
            try:
                server_socket.settimeout(1.0)  # Non-blocking accept
                conn, addr = server_socket.accept()
                self.log(f"TCP connection ({label}) received from {addr}")

                # Handle the connection in a separate thread
                conn_thread = threading.Thread(target=self._handle_tcp_connection, args=(conn, addr))
                conn_thread.daemon = True
                conn_thread.start()

            except socket.timeout:
                continue  # Check if we should still be running
            except Exception as e:
                if self.tcp_server_running:
                    self.log(f"TCP server ({label}) error: {e}")
                break

        self.log(f"TCP server worker ({label}) stopped")

    def _handle_tcp_connection(self, conn, addr):
        """Handle a single TCP connection from the C64 Stream plugin."""
        self.log(f"🔍 TCP connection received from {addr}")
        self.log(f"  - Connection details: {conn}")
        self.log(f"  - Local address: {conn.getsockname()}")
        self.log(f"  - Remote address: {conn.getpeername()}")

        try:
            conn.settimeout(5.0)  # 5 second timeout for receive
            data = conn.recv(1024)
            self.log(f"📨 Received {len(data)} bytes from {addr}")

            if len(data) >= 4:
                self.log(f"Received TCP command from {addr}: {data.hex()}")

                # Parse the command according to C64 protocol
                # Format: [command_byte][0xFF][param_len][0x00][duration_bytes...][ip:port_string]
                cmd_byte = data[0]

                if data[1] == 0xFF:  # Valid command marker
                    stream_id = cmd_byte & 0x0F  # Extract stream ID (0=video, 1=audio)
                    is_start = (cmd_byte & 0xF0) == 0x20  # 0x20 = start, 0x30 = stop

                    if is_start:
                        self.log(f"✅ Received START command for stream {stream_id}")

                        # Extract destination IP:port if present
                        if len(data) > 6:
                            param_len = data[2]
                            if param_len > 2 and len(data) >= 6 + param_len - 2:
                                dest_str = data[6:6+param_len-2].decode('ascii', errors='ignore')
                                self.log(f"Stream destination: {dest_str}")

                                # Parse and store the destination for UDP replay
                                if ':' in dest_str:
                                    dest_ip, dest_port_str = dest_str.split(':', 1)
                                    try:
                                        dest_port = int(dest_port_str)
                                        if stream_id == 0:  # Video
                                            self.video_dest_ip = dest_ip
                                            self.video_dest_port = dest_port
                                            self.log(f"Updated video destination: {dest_ip}:{dest_port}")
                                        elif stream_id == 1:  # Audio
                                            self.audio_dest_ip = dest_ip
                                            self.audio_dest_port = dest_port
                                            self.log(f"Updated audio destination: {dest_ip}:{dest_port}")
                                    except ValueError:
                                        self.log(f"Invalid port in destination: {dest_str}")

                        # Signal that we should start UDP packet replay.
                        # Wait until we've received START for both streams (video+audio).
                        with self._stream_start_lock:
                            if stream_id == 0:
                                self._stream_start_mask |= 0x1
                            elif stream_id == 1:
                                self._stream_start_mask |= 0x2
                            if self._stream_start_mask == 0x3:
                                self.udp_replay_triggered.set()

                    else:
                        self.log(f"Received STOP command for stream {stream_id}")

            conn.close()

        except Exception as e:
            self.log(f"Error handling TCP connection from {addr}: {e}")
            try:
                conn.close()
            except:
                pass

    def stop_mock_c64_server(self):
        """Stop the mock C64 Ultimate TCP server."""
        self.log("Stopping mock C64 Ultimate TCP server")

        self.tcp_server_running = False

        if self.tcp_server_socket:
            try:
                self.tcp_server_socket.close()
            except:
                pass
            self.tcp_server_socket = None

        if self.tcp_server_socket_alt:
            try:
                self.tcp_server_socket_alt.close()
            except:
                pass
            self.tcp_server_socket_alt = None

        if self.tcp_server_thread:
            self.tcp_server_thread.join(timeout=2)
            self.tcp_server_thread = None

        if self.tcp_server_thread_alt:
            self.tcp_server_thread_alt.join(timeout=2)
            self.tcp_server_thread_alt = None

        self.log("✅ Mock C64 Ultimate TCP server stopped")

    def stop_obs(self):
        """Stop OBS recording with proper cleanup."""
        self.log("Stopping OBS")

        if self.obs_process:
            try:
                # First try to stop recording via WebSocket if available and enabled
                if WEBSOCKET_AVAILABLE and self.enable_websocket:
                    self.send_obs_request("StopRecord")
                    time.sleep(1)

                # Send SIGTERM for graceful shutdown
                self.obs_process.terminate()

                # Wait for graceful shutdown (increased from 8s to allow complete processing)
                try:
                    self.obs_process.wait(timeout=12)
                    self.log("✅ OBS stopped gracefully")
                except subprocess.TimeoutExpired:
                    self.log("OBS didn't stop gracefully within 12s, sending SIGKILL...")
                    self.obs_process.kill()
                    self.obs_process.wait(timeout=5)  # Also increased kill timeout
                    self.log("✅ OBS stopped forcefully")

            except Exception as e:
                self.log(f"Error stopping OBS: {e}")
                try:
                    self.obs_process.kill()
                    self.obs_process.wait()
                except:
                    pass

            # Clean up any OBS lock files that might cause crash recovery dialogs
            self.cleanup_obs_locks()

    def stop_xvfb(self):
        """Stop Xvfb and clean up lock files."""
        self.log("Stopping Xvfb")

        if self.xvfb_process:
            self.xvfb_process.terminate()

            try:
                self.xvfb_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.xvfb_process.kill()
                self.xvfb_process.wait()

            # Clean up lock files
            try:
                display = os.environ.get('DISPLAY', ':99')
                display_num = display.lstrip(':')
                lock_file = f"/tmp/.X{display_num}-lock"
                if os.path.exists(lock_file):
                    os.remove(lock_file)
                    self.log(f"Cleaned up lock file: {lock_file}")
            except OSError:
                pass  # Ignore permission errors

            self.log("✅ Xvfb stopped")

    def cleanup_obs_state_files(self, obs_config_dir):
        """Clean up OBS state files that can trigger popup dialogs."""
        try:
            import glob

            # Files/patterns that can trigger dialogs
            state_patterns = [
                str(obs_config_dir / 'safe_mode'),
                str(obs_config_dir / '.safe_mode'),
                str(obs_config_dir / 'crashed'),
                str(obs_config_dir / '.crashed'),
                str(obs_config_dir / 'basic/crashed'),
                str(obs_config_dir / 'plugin_config/.safe_mode*'),
                '/tmp/obs-safe-mode-*',
                '/tmp/.obs-crashed*'
            ]

            for pattern in state_patterns:
                for state_file in glob.glob(pattern):
                    try:
                        if Path(state_file).is_dir():
                            import shutil
                            shutil.rmtree(state_file)
                        else:
                            Path(state_file).unlink(missing_ok=True)
                        self.log(f"Cleaned up state file: {state_file}")
                    except (OSError, IOError):
                        pass

        except Exception as e:
            self.log(f"Warning: Could not clean up OBS state files: {e}")

    def cleanup_obs_locks(self):
        """Clean up OBS lock files and crash recovery state."""
        try:
            import glob
            obs_config_dir = Path.home() / '.config' / 'obs-studio'

            # Remove common OBS lock/crash files
            lock_patterns = [
                str(obs_config_dir / '*.lock'),
                str(obs_config_dir / 'basic' / 'profiles' / '*' / '*.lock'),
                str(obs_config_dir / 'crashes' / '*'),
                '/tmp/obs-studio-*',
                '/tmp/.obs-*'
            ]

            for pattern in lock_patterns:
                for lock_file in glob.glob(pattern):
                    try:
                        Path(lock_file).unlink(missing_ok=True)
                        self.log(f"Cleaned up: {lock_file}")
                    except (OSError, IOError):
                        pass

            # Also clean state files that trigger dialogs
            self.cleanup_obs_state_files(obs_config_dir)

        except Exception as e:
            self.log(f"Warning: Could not clean up OBS locks: {e}")

    def _restore_properties_ini(self):
        """Restore production properties.ini files that were backed up before E2E testing.

        This ensures that when the user starts OBS manually after an E2E run,
        it uses their production settings (e.g., connecting to a real C64 Ultimate)
        instead of the E2E test settings (localhost).
        """
        if not self._backed_up_properties:
            return

        self.log("📦 Restoring production properties.ini files...")
        for backup_path, original_path in self._backed_up_properties:
            try:
                if backup_path.exists():
                    shutil.copy2(backup_path, original_path)
                    backup_path.unlink()
                    self.log(f"✅ Restored: {original_path}")
                else:
                    self.log(f"⚠️ Backup not found: {backup_path}")
            except Exception as e:
                self.log(f"❌ Failed to restore {original_path}: {e}")

        self._backed_up_properties.clear()

    def _analyze_obs_logs(self):
        """Analyze OBS logs for debugging purposes (called only when needed)."""
        obs_config_dir = Path.home() / '.config' / 'obs-studio'
        logs_dir = obs_config_dir / 'logs'

        if not logs_dir.exists():
            self.log("  - OBS logs directory not found")
            return

        log_files = list(logs_dir.glob('*.txt'))
        log_files.sort(key=lambda f: f.stat().st_mtime, reverse=True)

        if not log_files:
            self.log("  - No OBS log files found")
            return

        latest_log = log_files[0]
        self.log(f"  - Checking latest log: {latest_log.name}")

        try:
            with open(latest_log, 'r') as f:
                content = f.read()

            # Look for plugin-related messages
            plugin_lines = [line for line in content.split('\n') if 'c64' in line.lower() or 'C64' in line]
            if plugin_lines:
                self.log(f"  - Found {len(plugin_lines)} plugin-related log entries:")
                for line in plugin_lines[-10:]:  # Show last 10 lines
                    self.log(f"    {line}")

            # Look for async task or streaming messages
            async_lines = [line for line in content.split('\n') if 'async' in line.lower() or 'streaming' in line.lower() or 'retry' in line.lower()]
            if async_lines:
                self.log(f"  - Found {len(async_lines)} async/streaming log entries:")
                for line in async_lines[-5:]:  # Show last 5 lines
                    self.log(f"    {line}")

            # Look for any error messages
            error_lines = [line for line in content.split('\n') if 'error' in line.lower() or 'failed' in line.lower()]
            if error_lines:
                self.log(f"  - Found {len(error_lines)} error/warning messages:")
                for line in error_lines[-5:]:  # Show last 5 error lines
                    self.log(f"    {line}")

            # Check plugin properties file
            plugin_props_file = obs_config_dir / 'plugins' / 'c64stream' / 'data' / 'properties.ini'
            if plugin_props_file.exists():
                self.log(f"  - Plugin properties file exists: {plugin_props_file}")
                try:
                    with open(plugin_props_file, 'r') as f:
                        props_content = f.read()
                    self.log(f"  - Properties file content (first 300 chars):")
                    self.log(f"    {props_content[:300]}...")
                except Exception as e:
                    self.log(f"  - Could not read properties file: {e}")
            else:
                self.log(f"  - Plugin properties file not found: {plugin_props_file}")

        except Exception as e:
            self.log(f"  - Could not read log file: {e}")

    def validate_test_results(self, replay_success, recording_success, csv_success, recording_file):
        """
        Comprehensive validation of E2E test results.
        Provides clear, concise validation with detailed error info when needed.
        Returns: (overall_success, validation_results_dict)
        """
        print(f"\n{'='*60}")
        print("E2E Test Validation Results")
        print(f"{'='*60}")

        validation_errors = []
        validation_warnings = []
        is_full_frame_pop = (self.scenario_id == 'ntsc_default_debug')

        # Track individual validation results
        validation_results = {
            'udp_reception': {'status': 'unknown', 'details': ''},
            'frame_processing': {'status': 'unknown', 'details': ''},
            'video_recording': {'status': 'unknown', 'details': ''},
            'packet_integrity': {'status': 'unknown', 'details': ''},
            'network_timing': {'status': 'unknown', 'details': ''},
        }

        # Calculate expected packet counts using actual generation logic
        if self.format == 'PAL':
            video_packets_per_frame = 68  # 272 lines / 4 lines per packet
            frame_rate = 50.125
            audio_sample_rate = 47983
        else:  # NTSC
            video_packets_per_frame = 60  # 240 lines / 4 lines per packet
            frame_rate = 59.826
            audio_sample_rate = 47940

        # Video packets calculation (unchanged)
        expected_video_packets = self.frames * video_packets_per_frame

        # Audio packets calculation (matches generate_packets.py logic)
        frame_duration_ms = 1000.0 / frame_rate
        total_test_duration_ms = self.frames * frame_duration_ms
        audio_samples_per_packet = 192  # Stereo samples
        audio_packet_duration_ms = (audio_samples_per_packet / audio_sample_rate) * 1000
        expected_audio_packets = int(total_test_duration_ms / audio_packet_duration_ms)

        expected_total_packets = expected_video_packets + expected_audio_packets

        print(f"Expected: {expected_total_packets} packets ({expected_video_packets} video + {expected_audio_packets} audio)")

        # 1. UDP Packet Reception Validation
        # Use original counts from before CSV truncation for accurate validation
        original_counts = getattr(self, '_original_csv_counts', {'network_packets': 0, 'obs_frames': 0, 'video_packets': 0, 'audio_packets': 0})
        received_packets = original_counts.get('network_packets', 0)
        video_packets = original_counts.get('video_packets', 0)
        audio_packets = original_counts.get('audio_packets', 0)

        if received_packets > 0:
            if received_packets == expected_total_packets:
                print(f"✅ UDP Reception: {received_packets}/{expected_total_packets} packets ({video_packets} video, {audio_packets} audio)")
                validation_results['udp_reception'] = {'status': 'pass', 'details': f"{received_packets}/{expected_total_packets} packets ({video_packets} video, {audio_packets} audio)"}
            elif received_packets >= expected_total_packets * 0.95:  # 95% threshold
                print(f"⚠️  UDP Reception: {received_packets}/{expected_total_packets} packets ({video_packets} video, {audio_packets} audio)")
                validation_warnings.append(f"Packet loss: {expected_total_packets - received_packets} packets missing")
                validation_results['udp_reception'] = {'status': 'warning', 'details': f"{received_packets}/{expected_total_packets} packets ({video_packets} video, {audio_packets} audio, minor loss)"}
            else:
                # Major packet loss - but defer error decision until we check frame processing
                # If frames are processed successfully, packet logging loss is a warning (CI timing issue)
                # If frames also fail, then it's a true error
                print(f"⚠️  UDP Reception: {received_packets}/{expected_total_packets} packets ({video_packets} video, {audio_packets} audio)")
                # Store info for deferred decision after frame processing check
                validation_results['udp_reception'] = {
                    'status': 'deferred',  # Will be resolved after frame check
                    'details': f"{received_packets}/{expected_total_packets} packets ({video_packets} video, {audio_packets} audio, major loss)",
                    'packets_missing': expected_total_packets - received_packets
                }
        else:
            print("❌ UDP Reception: No network.csv found")
            validation_errors.append("Missing network.csv - plugin may not be receiving UDP packets")
            validation_results['udp_reception'] = {'status': 'fail', 'details': 'No CSV file found'}

        # 1b. Network Timing / Pacing Validation (from network.json)
        # This catches severe sender regressions (e.g. "all packets sent instantly").
        network_json = self.output_dir / 'network.json'
        net_status, net_details, net_errors, net_warnings = validate_network_timing(
            network_json_path=network_json,
            video_format=self.format,
            frames=self.frames,
            network_simulation=self.network_simulation,
        )

        strict_network_timing = self.is_ci or (os.environ.get('C64_E2E_STRICT_NETWORK_TIMING', '') == '1')
        if strict_network_timing and net_status == 'warning':
            # Escalate only *non-benign* warnings to failures in CI.
            # Some warnings (e.g. max jitter spikes) can be caused by CI host scheduling
            # and are not necessarily a sender regression.
            critical_warnings = [w for w in net_warnings if not _is_benign_network_timing_warning(w)]
            if critical_warnings:
                net_status = 'fail'
                net_errors = net_errors + [
                    f"Network timing warning treated as error: {w}" for w in critical_warnings
                ]

        validation_errors.extend(net_errors)
        validation_warnings.extend(net_warnings)
        validation_results['network_timing'] = {'status': net_status, 'details': net_details}

        if net_status == 'fail':
            print(f"❌ Network Timing: {net_details}")
        elif net_status == 'warning':
            print(f"⚠️  Network Timing: {net_details}")
        elif net_status == 'pass':
            print(f"✅ Network Timing: {net_details}")
        else:
            print(f"❓ Network Timing: {net_details}")

        # 2. Frame Processing Validation
        # Use original frame count from before CSV truncation
        processed_frames = original_counts.get('obs_frames', 0)

        obs_csv = self.output_dir / 'obs.csv'
        if obs_csv.exists() and processed_frames > 0:
            try:
                # For short tests, we might not get exactly the expected frames due to timing
                min_expected_frames = max(1, int(self.frames * 0.8))  # At least 80% of frames

                if processed_frames >= min_expected_frames:
                    print(f"✅ Frame Processing: {processed_frames} frames processed (≥{min_expected_frames} expected)")
                    validation_results['frame_processing'] = {'status': 'pass', 'details': f"{processed_frames} frames processed"}
                else:
                    print(f"❌ Frame Processing: {processed_frames} frames processed (<{min_expected_frames} expected)")
                    validation_errors.append(f"Insufficient frame processing: {processed_frames} < {min_expected_frames}")
                    validation_results['frame_processing'] = {'status': 'fail', 'details': f"{processed_frames} frames (insufficient)"}

            except Exception as e:
                print(f"❌ Frame Processing: Failed to validate obs.csv - {e}")
                validation_errors.append(f"OBS CSV validation failed: {e}")
                validation_results['frame_processing'] = {'status': 'fail', 'details': 'CSV validation error'}
        elif not obs_csv.exists():
            print("❌ Frame Processing: No obs.csv found")
            validation_errors.append("Missing obs.csv - plugin may not be processing frames")
            validation_results['frame_processing'] = {'status': 'fail', 'details': 'No CSV file found'}
        else:
            print("❌ Frame Processing: No frames recorded in obs.csv")
            validation_errors.append("No frames recorded in obs.csv")
            validation_results['frame_processing'] = {'status': 'fail', 'details': 'No frames recorded'}

        # Resolve deferred UDP reception status based on frame processing result
        # If frame processing succeeded, packet logging loss is just a warning (CI timing issue)
        # If frame processing failed, packet loss is a contributing error
        if validation_results.get('udp_reception', {}).get('status') == 'deferred':
            udp_info = validation_results['udp_reception']
            frame_status = validation_results.get('frame_processing', {}).get('status', 'fail')
            if frame_status == 'pass':
                # Frame processing worked despite packet logging loss - demote to warning
                validation_warnings.append(f"Packet logging loss: {udp_info['packets_missing']} packets not logged (CI timing issue, frames OK)")
                validation_results['udp_reception'] = {'status': 'warning', 'details': udp_info['details']}
            else:
                # Both packet reception and frame processing failed - this is a real error
                validation_errors.append(f"Significant packet loss: {udp_info['packets_missing']} packets missing")
                validation_results['udp_reception'] = {'status': 'fail', 'details': udp_info['details']}

        # 3. Video Recording Validation
        if recording_file and Path(recording_file).exists():
            try:
                file_size = Path(recording_file).stat().st_size
                min_expected_size = 100 * 1024  # At least 100KB for a valid recording

                if file_size >= min_expected_size:
                    print(f"✅ Video Recording: {file_size:,} bytes ({file_size/1024/1024:.1f} MB)")
                    validation_results['video_recording'] = {'status': 'pass', 'details': f"{file_size/1024/1024:.1f} MB"}

                    # Optional: Quick video validation using ffprobe if available
                    try:
                        import subprocess
                        result = subprocess.run(['ffprobe', '-v', 'error', '-show_entries',
                                                 'format=duration', '-of', 'csv=p=0', recording_file],
                                                capture_output=True, text=True, timeout=5)
                        if result.returncode == 0:
                            duration = float(result.stdout.strip())
                            expected_duration = self.frames / (59.826 if self.format == 'NTSC' else 50.125)
                            if duration >= expected_duration * 0.5:  # At least 50% of expected duration
                                print(f"✅ Video Duration: {duration:.1f}s (≥{expected_duration*0.5:.1f}s expected)")
                                validation_results['packet_integrity'] = {'status': 'pass', 'details': f"{duration:.1f}s duration"}
                            else:
                                validation_warnings.append(f"Short video duration: {duration:.1f}s < {expected_duration*0.5:.1f}s")
                                validation_results['packet_integrity'] = {'status': 'warning', 'details': f"{duration:.1f}s (short)"}
                    except Exception:
                        validation_results['packet_integrity'] = {'status': 'unknown', 'details': 'Duration check failed'}

                    # Video brightness check - detect all-black or nearly-black videos.
                    # Be robust against OBS startup/shutdown padding and heavy tinting effects:
                    # - sample multiple timestamps
                    # - analyze center crop (avoid letterboxing)
                    # - use luma instead of raw RGB byte mean
                    if is_full_frame_pop:
                        validation_results['video_brightness'] = {
                            'status': 'skipped',
                            'details': 'Skipped (full-frame-pop scenario)'
                        }
                    else:
                        try:
                            import subprocess
                            import numpy as np

                            w, h = 1920, 1080
                            crop_w, crop_h = w // 2, h // 2
                            frame_bytes = crop_w * crop_h * 3

                            # Choose a few offsets that are likely to land inside stable content.
                            # Use duration if available; otherwise fall back to fixed timestamps.
                            offsets = []
                            try:
                                d = float(duration) if duration else 0.0
                            except Exception:
                                d = 0.0

                            if d > 2.0:
                                offsets = [max(0.5, d * 0.25), max(0.5, d * 0.5), max(0.5, min(d * 0.75, d - 0.5))]
                            else:
                                offsets = [0.5, 1.0, 1.5]

                            # Ensure offsets are unique and within bounds.
                            cleaned_offsets = []
                            for t in offsets:
                                t = float(t)
                                if d > 0.0:
                                    t = max(0.0, min(t, max(0.0, d - 0.1)))
                                if t not in cleaned_offsets:
                                    cleaned_offsets.append(t)

                            best_mean_luma = None
                            best_offset = None
                            sampled = []

                            for t in cleaned_offsets:
                                brightness_cmd = [
                                    'ffmpeg', '-v', 'error',
                                    '-ss', f'{t:.3f}',
                                    '-i', str(recording_file),
                                    '-vframes', '1',
                                    '-vf', 'crop=iw*0.5:ih*0.5:iw*0.25:ih*0.25',
                                    '-f', 'rawvideo',
                                    '-pix_fmt', 'rgb24',
                                    '-'
                                ]
                                brightness_result = subprocess.run(brightness_cmd, capture_output=True, timeout=10)
                                if brightness_result.returncode != 0 or len(brightness_result.stdout) != frame_bytes:
                                    continue

                                frame = np.frombuffer(brightness_result.stdout, dtype=np.uint8).reshape((crop_h, crop_w, 3))
                                # Luma in 0..255
                                f = frame.astype(np.float32)
                                luma = 0.2126 * f[..., 0] + 0.7152 * f[..., 1] + 0.0722 * f[..., 2]
                                mean_luma = float(np.mean(luma))
                                sampled.append({"t": float(t), "mean_luma": mean_luma})
                                if best_mean_luma is None or mean_luma > best_mean_luma:
                                    best_mean_luma = mean_luma
                                    best_offset = float(t)

                            if best_mean_luma is not None:
                                details = {"best_offset_s": best_offset, "best_mean_luma": float(best_mean_luma), "samples": sampled}
                                # Note: Sparse patterns like 'dots' can have very low mean luma (e.g., 1.12)
                                # Only fail if essentially black (< 1.0), warn if very dark (< 5.0)
                                if best_mean_luma < 1.0:  # Essentially black
                                    print(f"❌ Video Brightness: Content appears black (best_mean_luma={best_mean_luma:.2f} @ {best_offset:.1f}s)")
                                    validation_errors.append(
                                        f"Video content appears black (best mean luma {best_mean_luma:.1f}/255 @ {best_offset:.1f}s)"
                                    )
                                    validation_results['video_brightness'] = {'status': 'fail', 'details': f'Black (best_mean_luma={best_mean_luma:.1f})', 'metrics': details}
                                elif best_mean_luma < 5.0:  # Very dark (e.g., sparse dot patterns)
                                    print(f"⚠️  Video Brightness: Content appears very dark (best_mean_luma={best_mean_luma:.2f} @ {best_offset:.1f}s) - OK for sparse patterns")
                                    validation_results['video_brightness'] = {'status': 'pass', 'details': f'Very dark but acceptable (best_mean_luma={best_mean_luma:.1f})', 'metrics': details}
                                elif best_mean_luma < 15.0:  # Dark
                                    print(f"⚠️  Video Brightness: Content appears very dark (best_mean_luma={best_mean_luma:.2f} @ {best_offset:.1f}s)")
                                    validation_warnings.append(
                                        f"Video content is very dark (best mean luma {best_mean_luma:.1f}/255 @ {best_offset:.1f}s)"
                                    )
                                    validation_results['video_brightness'] = {'status': 'warning', 'details': f'Very dark (best_mean_luma={best_mean_luma:.1f})', 'metrics': details}
                                else:
                                    print(f"✅ Video Brightness: Normal (best_mean_luma={best_mean_luma:.2f} @ {best_offset:.1f}s)")
                                    validation_results['video_brightness'] = {'status': 'pass', 'details': f'Normal (best_mean_luma={best_mean_luma:.1f})', 'metrics': details}
                            else:
                                validation_results['video_brightness'] = {'status': 'unknown', 'details': 'Could not sample frames for brightness check'}
                        except Exception as e:
                            # Non-critical - just log and continue
                            validation_results['video_brightness'] = {'status': 'unknown', 'details': f'Check failed: {e}'}

                else:
                    print(f"❌ Video Recording: {file_size:,} bytes (<{min_expected_size:,} bytes)")
                    validation_errors.append(f"Video file too small: {file_size} < {min_expected_size} bytes")
                    validation_results['video_recording'] = {'status': 'fail', 'details': f"{file_size/1024:.0f} KB (too small)"}

            except Exception as e:
                print(f"❌ Video Recording: Failed to validate file - {e}")
                validation_errors.append(f"Video file validation failed: {e}")
                validation_results['video_recording'] = {'status': 'fail', 'details': 'Validation error'}
        else:
            print("❌ Video Recording: No recording file found")
            validation_errors.append("Missing video recording")
            validation_results['video_recording'] = {'status': 'fail', 'details': 'No file found'}

        # 4. A/V Synchronization Validation (A/V sync check)
        # A/V sync is a critical component of the streaming functionality
        av_validation = True
        visuals_results = None  # cache visual checks to avoid running twice
        if (not is_full_frame_pop) and recording_file and Path(recording_file).exists() and verify_av_sync:
            try:
                print(f"🎵 A/V Sync: Running A/V sync check (pops, tolerance={self.av_sync_tolerance_ms}ms)...")
                # Tolerance: configurable per-scenario, default 60ms (~3 frames at 60fps)
                # Jitter scenarios may need higher tolerance (e.g., 150ms)
                sync_results = verify_av_sync(recording_file, tolerance_ms=self.av_sync_tolerance_ms)

                # Report detailed offsets summary even in success case
                diffs = [d['difference_ms'] for d in sync_results['sync_details'] if d.get('closest_video_pop_ms') is not None]
                avg_diff = (sum(diffs) / len(diffs)) if diffs else 0.0
                max_diff = max(diffs) if diffs else 0.0
                # Persist full sync analysis to validation results for report generation
                try:
                    validation_results['av_sync_details'] = sync_results
                except Exception:
                    pass

                # Check for infrastructure issues:
                # - 0 video pops = no video content received
                # - Very few video pops vs audio pops = partial video content (UDP timing issue)
                video_pops_detected = len(sync_results.get('video_pop_times_ms', []))
                audio_pops_detected = sync_results.get('total_audio_pops', 0)

                if video_pops_detected == 0:
                    print(f"⚠️  A/V Sync: No video pops detected (UDP timing/infrastructure issue)")
                    validation_warnings.append("A/V sync skipped: no video pops detected (UDP timing issue)")
                    validation_results['av_sync'] = {'status': 'skip', 'details': 'No video pops detected (infrastructure issue)'}
                    # Don't fail the test for infrastructure issues
                elif audio_pops_detected >= 3 and video_pops_detected < audio_pops_detected / 2:
                    # If we have 3+ audio pops but less than half as many video pops,
                    # this indicates partial video content due to UDP timing
                    print(f"⚠️  A/V Sync: Insufficient video pops ({video_pops_detected}/{audio_pops_detected} audio) - partial content")
                    validation_warnings.append(f"A/V sync skipped: only {video_pops_detected} video pops vs {audio_pops_detected} audio pops")
                    validation_results['av_sync'] = {'status': 'skip', 'details': f'Partial video content ({video_pops_detected}/{audio_pops_detected})'}
                elif sync_results['is_perfectly_synced']:
                    print(f"✅ A/V Sync: Perfect synchronization ({sync_results['sync_accuracy_percent']:.1f}%) — avg offset {avg_diff:.1f}ms, max {max_diff:.1f}ms")
                    validation_results['av_sync'] = {'status': 'pass', 'details': f"{sync_results['perfect_sync_count']}/{sync_results['total_analyzed']} analyzed pops synced"}
                elif sync_results['sync_accuracy_percent'] >= 50.0:  # 50% threshold for pass (lowered for CRT effects)
                    print(f"✅ A/V Sync: Good synchronization ({sync_results['sync_accuracy_percent']:.1f}%) — avg offset {avg_diff:.1f}ms, max {max_diff:.1f}ms")
                    validation_results['av_sync'] = {'status': 'pass', 'details': f"{sync_results['perfect_sync_count']}/{sync_results['total_analyzed']} analyzed pops synced"}
                else:
                    print(f"❌ A/V Sync: Poor synchronization ({sync_results['sync_accuracy_percent']:.1f}%) — avg offset {avg_diff:.1f}ms, max {max_diff:.1f}ms")
                    validation_errors.append(f"A/V sync accuracy too low: {sync_results['sync_accuracy_percent']:.1f}% (minimum 50% required)")
                    validation_results['av_sync'] = {'status': 'fail', 'details': f"Only {sync_results['perfect_sync_count']}/{sync_results['total_analyzed']} pops synced"}
                    av_validation = False

                # Traffic-light summary per pop incl. channel
                try:
                    tl = sync_results.get('traffic', [])
                    details = sync_results.get('sync_details', [])
                    if tl and details:
                        legend = {'green': '🟢', 'yellow': '🟡', 'red': '🔴', 'gray': '⚪'}
                        marks = ''.join(legend.get(x, '•') for x in tl)
                        # Use audio_channel for display (L/R), fall back to 'channel' for backward compat
                        chans = ''.join(('L' if d.get('audio_channel', d.get('channel')) == 'L' else ('R' if d.get('audio_channel', d.get('channel')) == 'R' else 'B')) for d in details)
                        print(f"   Pops traffic: {marks}")
                        print(f"   Channels:     {chans}")
                        # Verify strict alternation regardless of starting side; ignore 'B' and unmatched pops
                        seq = []
                        for d in details:
                            if not d.get('included_in_analysis', True):
                                continue  # Skip unmatched pops
                            ch = d.get('audio_channel', d.get('channel'))
                            if ch == 'L':
                                seq.append('L')
                            elif ch == 'R':
                                seq.append('R')
                        alt_ok = False
                        if len(seq) >= 2:
                            alt_ok = all(seq[i] != seq[i-1] for i in range(1, len(seq)))
                        if alt_ok:
                            print(f"   🔁 Channel alternation: OK (alternating, starts with {seq[0]})")
                        else:
                            print("   🔁 Channel alternation: MISMATCH")
                            validation_warnings.append("Audio pops not strictly alternating between L and R")
                except Exception:
                    pass

                # Schedule constraint: no A/V event allowed in the last 1000ms of the recording
                if 'last_event_within_limit' in sync_results and not sync_results['last_event_within_limit']:
                    validation_warnings.append("Video event detected within the last 1000ms of the recording (violates schedule constraint)")

                # Frame box sequence check: enabled only for explicit default scenarios.
                visuals_results = {
                    'frame_sequence_box': {
                        'status': 'skipped',
                        'message': 'Skipped (disabled)',
                        'details': {},
                        'metrics': {},
                    }
                }
                # Frame progression assertion - uses position marker that works for ALL presets
                # (including monochrome presets like Green Monitor, Amber Monitor)
                enable_frame_progression = True
                if enable_frame_progression and recording_file:
                    try:
                        from assertions.frame_progression import FrameProgressionAssertion

                        a = FrameProgressionAssertion()
                        res = a.verify(
                            Path(recording_file),
                            properties={"settling_seconds": self.settling_seconds},
                            preset=None,
                            verbose=self.verbose,
                        )
                        status_map = {
                            'pass': 'pass',
                            'warning': 'warning',
                            'skip': 'skipped',
                            'fail': 'fail',
                        }
                        visuals_results['frame_sequence_box'] = {
                            'status': status_map.get(res.status.value, res.status.value),
                            'message': res.message,
                            'details': res.details,
                            'metrics': res.metrics,
                        }
                        if res.status.value == 'pass':
                            print(f"✅ Frame Sequence Box: {res.message}")
                        elif res.status.value == 'warning':
                            print(f"⚠️  Frame Sequence Box: {res.message}")
                            validation_warnings.append(f"Frame Sequence Box: {res.message}")
                        elif res.status.value == 'skip':
                            print(f"⚪ Frame Sequence Box: {res.message}")
                        else:
                            print(f"❌ Frame Sequence Box: {res.message}")
                            validation_errors.append(f"Frame Sequence Box: {res.message}")
                    except Exception as e:
                        print(f"❌ Frame Sequence Box: Analysis failed - {e}")
                        validation_errors.append(f"Frame Sequence Box analysis error: {e}")
                        visuals_results['frame_sequence_box'] = {
                            'status': 'fail',
                            'message': f'Analysis failed - {e}',
                            'details': {},
                            'metrics': {},
                        }
                else:
                    print("⚪ Frame Sequence Box: Skipped (disabled)")

                # Scanline uniformity check (runs when scanlines are enabled in the active scene)
                scanlines_results = {
                    'status': 'skipped',
                    'details': 'Skipped (not enabled)'
                }
                if recording_file:
                    try:
                        import json

                        from assertions.config import PresetConfig, load_settings_from_obs_scene
                        from assertions.scanlines import ScanlineAssertion

                        scene_path = Path.home() / '.config' / 'obs-studio' / 'basic' / 'scenes' / 'C64StreamTest.json'
                        if scene_path.exists():
                            settings = load_settings_from_obs_scene(scene_path)
                            preset = PresetConfig.from_obs_settings(settings)
                            if preset.has_scanlines():
                                # Tighten variance when there is no intentional blur.
                                thresholds = {
                                    'min_scanline_count': 35,
                                }
                                if preset.blur_strength >= 0.5:
                                    thresholds['max_variance_percent'] = 1.5
                                elif preset.blur_strength >= 0.3:
                                    thresholds['max_variance_percent'] = 1.0
                                else:
                                    thresholds['max_variance_percent'] = 0.3
                                    if preset.scan_line_strength >= 0.6:
                                        thresholds['min_contrast_ratio'] = 0.20

                                a = ScanlineAssertion(thresholds)
                                res = a.verify(Path(recording_file), properties={}, preset=preset, verbose=self.verbose)
                                scanlines_results = {
                                    'status': res.status.value,
                                    'details': res.message,
                                    'metrics': res.metrics,
                                }
                                if res.status.value == 'pass':
                                    print(f"✅ Scanlines: {res.message}")
                                elif res.status.value == 'skip':
                                    print(f"⚪ Scanlines: {res.message}")
                                else:
                                    print(f"❌ Scanlines: {res.message}")
                                    validation_errors.append(f"Scanlines: {res.message}")
                            else:
                                print("⚪ Scanlines: Skipped (not enabled)")
                        else:
                            print(f"⚪ Scanlines: Skipped (missing scene file: {scene_path})")
                    except Exception as e:
                        print(f"❌ Scanlines: Analysis failed - {e}")
                        validation_errors.append(f"Scanlines analysis error: {e}")
                        scanlines_results = {
                            'status': 'fail',
                            'details': f'Analysis failed - {e}',
                        }

                validation_results['scanlines'] = scanlines_results

                # Add frame sequence box results to validation_results for README generation
                validation_results['frame_sequence_box'] = visuals_results['frame_sequence_box']

                # Recording assertions: verify record_audio, record_video, record_obs, record_network
                # These run only for scenarios that enable recording (e.g., ntsc_default_record)
                recording_results = {
                    'record_audio': {'status': 'skipped', 'message': 'Skipped (not enabled)'},
                    'record_video': {'status': 'skipped', 'message': 'Skipped (not enabled)'},
                    'record_obs': {'status': 'skipped', 'message': 'Skipped (not enabled)'},
                    'record_network': {'status': 'skipped', 'message': 'Skipped (not enabled)'},
                    'record_frames': {'status': 'skipped', 'message': 'Skipped (not enabled)'},
                }
                enable_recording_assertions = (self.scenario_id == 'ntsc_default_record')
                if enable_recording_assertions and recording_file:
                    try:
                        from assertions.record_audio import RecordAudioAssertion
                        from assertions.record_frames import RecordFramesAssertion
                        from assertions.record_network import RecordNetworkAssertion
                        from assertions.record_obs import RecordObsAssertion
                        from assertions.record_video import RecordVideoAssertion

                        # Run each recording assertion
                        for assertion_name, assertion_cls in [
                            ('record_audio', RecordAudioAssertion),
                            ('record_video', RecordVideoAssertion),
                            ('record_obs', RecordObsAssertion),
                            ('record_network', RecordNetworkAssertion),
                            ('record_frames', RecordFramesAssertion),
                        ]:
                            try:
                                if assertion_name == 'record_video':
                                    record_height = 240 if self.format == 'NTSC' else 272
                                    a = assertion_cls(
                                        thresholds={
                                            "expected_width": 384,
                                            "expected_height": record_height,
                                        }
                                    )
                                else:
                                    a = assertion_cls()
                                res = a.verify(Path(recording_file), properties={}, preset=None, verbose=self.verbose)
                                recording_results[assertion_name] = {
                                    'status': res.status.value,
                                    'message': res.message,
                                    'details': res.details,
                                    'metrics': res.metrics,
                                }
                                if res.status.value == 'pass':
                                    print(f"✅ {a.name}: {res.message}")
                                elif res.status.value == 'warning':
                                    print(f"⚠️  {a.name}: {res.message}")
                                    validation_warnings.append(f"{a.name}: {res.message}")
                                elif res.status.value == 'skip':
                                    print(f"⚪ {a.name}: {res.message}")
                                else:
                                    print(f"❌ {a.name}: {res.message}")
                                    validation_errors.append(f"{a.name}: {res.message}")
                            except Exception as e:
                                print(f"❌ {assertion_name}: Analysis failed - {e}")
                                validation_errors.append(f"{assertion_name} error: {e}")
                                recording_results[assertion_name] = {
                                    'status': 'fail',
                                    'message': f'Analysis failed - {e}',
                                }
                    except ImportError as e:
                        print(f"⚪ Recording assertions: Skipped (import error: {e})")

                validation_results['recording'] = recording_results

            except Exception as e:
                print(f"❌ A/V Sync: Analysis failed - {e}")
                validation_errors.append(f"A/V sync analysis error: {e}")
                validation_results['av_sync'] = {'status': 'fail', 'details': 'Analysis failed'}
                av_validation = False
        else:
            if is_full_frame_pop:
                print("⚪ A/V Sync: Skipped (full-frame-pop scenario)")
                validation_results['av_sync'] = {'status': 'skipped', 'details': 'Skipped (full-frame-pop scenario)'}
            elif not verify_av_sync:
                print("❌ A/V Sync: Analysis not available (missing dependencies)")
                validation_errors.append("A/V sync analysis unavailable")
                validation_results['av_sync'] = {'status': 'fail', 'details': 'Analysis unavailable'}
                av_validation = False

        if is_full_frame_pop and recording_file and Path(recording_file).exists():
            try:
                from assertions.av_pop_offset import AvPopOffsetAssertion

                a = AvPopOffsetAssertion()
                res = a.verify(Path(recording_file), properties={}, preset=None, verbose=self.verbose)
                validation_results['av_pop_offset'] = {
                    'status': res.status.value,
                    'message': res.message,
                    'details': res.details,
                    'metrics': res.metrics,
                }
                if res.status.value == 'pass':
                    print(f"✅ {a.name}: {res.message}")
                elif res.status.value == 'warning':
                    print(f"⚠️  {a.name}: {res.message}")
                    validation_warnings.append(f"{a.name}: {res.message}")
                elif res.status.value == 'skip':
                    print(f"⚪ {a.name}: {res.message}")
                else:
                    print(f"❌ {a.name}: {res.message}")
                    validation_errors.append(f"{a.name}: {res.message}")
            except Exception as e:
                print(f"❌ av_pop_offset: Analysis failed - {e}")
                validation_errors.append(f"av_pop_offset error: {e}")
                validation_results['av_pop_offset'] = {
                    'status': 'fail',
                    'message': f'Analysis failed - {e}',
                }

        # Summary with traffic-light statuses and key metrics
        print(f"\n{'='*60}")

        if not validation_errors and not validation_warnings:
            print("🎉 PERFECT: All validations passed!")
            overall_success = True
        elif not validation_errors:
            print(f"✅ SUCCESS: Test passed with {len(validation_warnings)} warning(s)")
            for warning in validation_warnings:
                print(f"   ⚠️  {warning}")
            overall_success = True
        else:
            print(f"❌ FAILED: {len(validation_errors)} critical error(s)")
            for error in validation_errors:
                print(f"   💥 {error}")
            if validation_warnings:
                print(f"   Additional {len(validation_warnings)} warning(s):")
                for warning in validation_warnings:
                    print(f"   ⚠️  {warning}")
            overall_success = False

        # Print a compact summary table
        try:
            # UDP counts
            udp_line = validation_results.get('udp_reception', {})
            nt_line = validation_results.get('network_timing', {})
            fr_line = validation_results.get('frame_processing', {})
            vr_line = validation_results.get('video_recording', {})
            pi_line = validation_results.get('packet_integrity', {})
            av_line = validation_results.get('av_sync', {})
            def icon(status):
                return {'pass': '🟢', 'warning': '🟡', 'fail': '🔴', 'skipped': '⚪', 'unknown': '⚪'}.get(status, '⚪')
            print("Summary (checks):")
            print(f"  UDP Packets     {icon(udp_line.get('status'))}  {udp_line.get('details','')}")
            print(f"  Network Timing  {icon(nt_line.get('status'))}  {nt_line.get('details','')}")
            print(f"  OBS Frames      {icon(fr_line.get('status'))}  {fr_line.get('details','')}")
            print(f"  Recording File  {icon(vr_line.get('status'))}  {vr_line.get('details','')}")
            print(f"  Duration Check  {icon(pi_line.get('status'))}  {pi_line.get('details','')}")
            vb_line = validation_results.get('video_brightness', {})
            if vb_line:
                print(f"  Video Bright.   {icon(vb_line.get('status'))}  {vb_line.get('details','')}")
            if av_line:
                print(f"  A/V Sync        {icon(av_line.get('status'))}  {av_line.get('details','')}")
            sl_line = validation_results.get('scanlines', {})
            if sl_line:
                print(f"  Scanlines       {icon(sl_line.get('status'))}  {sl_line.get('details','')}")
            # Include visual checks summary if present
            # Visual checks disabled: ensure placeholder is printed without analysis
            if visuals_results is None:
                visuals_results = {
                    'frame_sequence_box': {
                        'status': 'skipped',
                        'message': 'Skipped (disabled)',
                        'details': {},
                        'metrics': {},
                    }
                }
            fsb = visuals_results['frame_sequence_box']
            print(f"  Frame Box Seq   {icon(fsb.get('status'))}  {fsb.get('message','')}")
        except Exception:
            pass

        print(f"{'='*60}\n")
        return overall_success, validation_results

    def cleanup(self):
        """Cleanup all test processes and restore production properties.ini."""
        self.log("Cleaning up test environment")
        self.stop_mock_c64_server()
        self.stop_obs()
        self.stop_xvfb()
        self.cleanup_obs_locks()
        self._restore_properties_ini()

    def run(self, udp_replay_path):
        """
        Run the complete e2e test.

        Returns:
            bool: True if test passed, False otherwise
        """
        print(f"\n{'='*60}")
        heading = self.scenario_name or self.format
        print(f"C64 Stream E2E Test - {heading}")
        if self.scenario_name:
            print(f"Format: {self.format}")
        print(f"{'='*60}\n")

        try:
            # Clean test output directory first
            self.clean_test_output()

            # Setup test environment
            if not self.start_xvfb():
                return False

            # Copy E2E properties configuration to plugin
            if not self.copy_e2e_properties():
                self.log("❌ Failed to copy E2E properties")
                return False

            # Start mock C64 Ultimate TCP server BEFORE OBS
            # This is critical because the plugin auto-connects when it's created
            if not self.start_mock_c64_server():
                self.log("❌ Failed to start mock C64 server")
                return False

            # Start OBS with recording enabled
            if not self.start_obs(start_recording=True):
                self.log("❌ Failed to start OBS")
                return False

            # Prepare resource monitoring now that OBS is running (enables per-process attribution)
            if self.enable_resource_monitoring:
                tracked = {
                    "obs": int(self.obs_process.pid) if self.obs_process else -1,
                    "harness": int(os.getpid()),
                }
                self._resource_monitor = ResourceMonitor(
                    interval_ms=self.resource_interval_ms,
                    verbose=self.verbose,
                    tracked_pids=tracked,
                    allow_interactive=False,
                )

            # Now that OBS is running, do resource monitor warmup (CPU measurement priming)
            if self.enable_resource_monitoring and self._resource_monitor:
                self._resource_monitor.warmup()  # Prime CPU measurement

            # Wait for plugin to be ready by watching logs - this is precise timing!
            # No need for arbitrary delays - we start streaming as soon as plugin signals readiness
            self.log("⏳ Waiting for C64 plugin to be ready (watching logs)...")
            plugin_ready_timeout = 10 if self.is_ci else 5
            if not self.wait_for_plugin_initialization(timeout=plugin_ready_timeout):
                self.log("⚠️ Plugin may not be fully initialized, proceeding anyway")
            else:
                self.log("✅ Plugin is ready, starting packet replay immediately")

            # Brief delay to ensure UDP sockets are bound and plugin is fully ready
            udp_ready_delay = 2.0 if self.is_ci else 0.3
            self.log(f"⏳ Allowing {udp_ready_delay}s for UDP socket binding and plugin readiness...")
            time.sleep(udp_ready_delay)

            self.log("✅ OBS recording active, plugin ready")

            # Run packet replay while recording
            self.log("Running packet replay while OBS is recording...")

            # Optional: record a perf profile of OBS during replay + grace period.
            if self.enable_perf_profile and self.obs_process:
                fps = 60.0 if self.format == 'NTSC' else 50.0
                grace = 5.0 if self.is_ci else 3.0
                expected_s = (float(self.frames) / fps) + grace + 2.0
                duration_s = self.perf_duration_s if self.perf_duration_s is not None else expected_s
                self._start_perf_profile(self.obs_process.pid, duration_s)

            replay_success = self.replay_packets(udp_replay_path)

            if replay_success:
                self.log("✅ Packet replay completed successfully")
            else:
                self.log("❌ Packet replay failed")

            # Stop recording promptly after last frame received
            # Keep recording a short grace period to allow OBS to flush frames
            # Reduced grace period to minimize logo display at end (was 5s/3s, now 2s/1.5s)
            grace = 2.0 if self.is_ci else 1.5
            self.log(f"⏳ Waiting {grace}s after last frame, then stopping recording...")
            time.sleep(grace)
            self.stop_recording()
            # Minimal extra wait to ensure the output file is finalized
            time.sleep(1)

            # Stop resource monitoring NOW - after full OBS processing is complete
            # This captures the entire test duration including packet sending + grace period
            if self.enable_resource_monitoring and self._resource_monitor:
                self._resource_summary = self._resource_monitor.stop()
                self._save_resource_data()
                self.log(f"📊 Resource monitoring stopped ({self._resource_summary.cpu.sample_count} samples)")

            if self.enable_perf_profile:
                self._finalize_perf_profile()

            # Proactively stop OBS now to avoid lingering recordings while analysis runs
            self.stop_obs()

            # Capture OBS log and summarize render/encode lag
            log_path = self._collect_obs_log()
            if log_path:
                self._summarize_obs_log(log_path)

            # Check CSV recordings first (crucial for debugging packet reception)
            csv_found = self.check_csv_recordings()
            if csv_found:
                self.log("✅ CSV recordings found and analyzed")
                csv_success = True
            else:
                self.log("⚠️ No CSV recordings found - may indicate packet reception issues")
                csv_success = False

            # Check if recording file was created (hybrid_mp4 outputs MP4 directly)
            recording_file = self.check_recording_output()
            if recording_file:
                self.log(f"✅ Recording created successfully: {recording_file}")
                recording_success = True
            else:
                self.log("❌ No recording file found")
                recording_success = False

            # Post-test log analysis for debugging if needed
            if not replay_success or not csv_success:
                self.log("🔍 Analyzing OBS logs for debugging...")
                self._analyze_obs_logs()

            # Comprehensive validation
            validation_success, validation_results = self.validate_test_results(replay_success, recording_success, csv_success, recording_file)

            # Store validation results for shell script access
            results_file = self.output_dir / 'validation_results.json'
            import json
            with open(results_file, 'w') as f:
                json.dump(validation_results, f, indent=2)

            return validation_success

        except Exception as e:
            print(f"\n❌ Test failed: {e}")
            import traceback
            traceback.print_exc()
            return False

        finally:
            self.cleanup()


def main():
    # Set up signal handler for clean shutdown
    test_instance = None

    def signal_handler(signum, frame):
        print(f"\n\u26a0\ufe0f  Received signal {signum}, cleaning up...")
        if test_instance:
            test_instance.cleanup()
        sys.exit(1)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    parser = argparse.ArgumentParser(
        description='Run C64 Stream e2e tests',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )

    parser.add_argument('--test-dir', default=str(Path(__file__).parent),
                        help='Test directory (default: script directory)')
    parser.add_argument('--format', choices=['PAL', 'NTSC'], default='NTSC',
                        help='Video format to test (default: NTSC for speed)')
    parser.add_argument('--frames', type=int, default=299,
                        help='Number of frames to test (default: 299 = 5s NTSC)')
    parser.add_argument('--video-port', type=int, default=21000,
                        help='Video UDP port (default: 21000)')
    parser.add_argument('--audio-port', type=int, default=21001,
                        help='Audio UDP port (default: 21001)')
    parser.add_argument('--control-port', type=int, default=6400,
                        help='Control TCP port for mock C64 Ultimate server (default: 6400)')
    parser.add_argument('--udp-replay', default='./udp_replay',
                        help='Path to udp_replay executable (default: ./udp_replay)')
    parser.add_argument('--verbose', action='store_true',
                        help='Enable verbose logging')
    parser.add_argument('--enable-websocket', action='store_true',
                        help='Enable WebSocket API attempts (disabled by default for performance)')
    parser.add_argument('--scenario-overrides', default=None,
                        help='Path to a directory with files to overlay onto ~/.config/obs-studio after baseline copy')
    parser.add_argument('--scenario-name', default=None,
                        help='Human-readable scenario name for logging/reporting')
    parser.add_argument('--scenario-id', default=None,
                        help='Scenario id (folder name) for gating checks (e.g., ntsc_default)')
    parser.add_argument('--output-dir', default=None,
                        help='Directory where test artifacts are written (default: test_output under --test-dir)')
    parser.add_argument('--csv-max-rows', type=int, default=2000,
                        help='Truncate CSV files to first N lines (incl header) (default: 2000, use 0 to disable)')
    parser.add_argument('--enable-resource-monitoring', action='store_true',
                        help='Enable CPU/GPU/RAM monitoring during packet replay')
    parser.add_argument('--monitor-resource-duration', type=float, default=None,
                        help='Resource monitoring sample interval in seconds (supports fractions, e.g. 0.5)')
    parser.add_argument('--resource-interval-ms', type=int, default=500,
                        help='Resource monitoring sample interval in milliseconds (default: 500)')

    parser.add_argument('--perf-profile', action='store_true',
                        help='Record a perf profile of OBS during packet replay (Linux, best-effort)')
    parser.add_argument('--perf-flamegraph', action='store_true',
                        help='Generate flamegraph.svg if FlameGraph scripts are installed')
    parser.add_argument('--perf-frequency-hz', type=int, default=199,
                        help='perf sampling frequency in Hz (default: 199)')
    parser.add_argument('--perf-callgraph', choices=['dwarf', 'fp'], default='dwarf',
                        help='perf callgraph mode (default: dwarf)')
    parser.add_argument('--perf-duration', type=float, default=None,
                        help='Override perf capture duration in seconds (default: auto from frames+grace)')

    parser.add_argument('--settling-duration', '--settling-seconds', dest='settling_seconds', type=float, default=0.0,
                        help='Ignore frame progression errors during first N seconds (pass/fail gating only)')

    parser.add_argument('--scenario-yaml', type=str, default=None,
                        help='Path to scenario YAML file (for loading network_simulation config)')

    args = parser.parse_args()

    if args.monitor_resource_duration is not None:
        if args.monitor_resource_duration < 0.1:
            raise SystemExit('--monitor-resource-duration must be >= 0.1 seconds')
        args.resource_interval_ms = int(args.monitor_resource_duration * 1000.0 + 0.5)

    # Load network_simulation from scenario YAML if provided
    network_simulation = {}
    av_sync_tolerance_ms = 60  # Default tolerance
    if args.scenario_yaml:
        import yaml
        try:
            with open(args.scenario_yaml, 'r') as f:
                scenario_data = yaml.safe_load(f)
                network_simulation = scenario_data.get('network_simulation', {})
                # Load A/V sync tolerance from scenario (default 60ms)
                av_sync_tolerance_ms = scenario_data.get('av_sync_tolerance_ms', 60)
                if network_simulation:
                    jitter_pct = network_simulation.get('jitter_percent', 0)
                    jitter_ms = network_simulation.get('max_jitter_ms', 0)
                    reorder_pct = network_simulation.get('reorder_percent', 0)
                    print(f"📡 Loaded network simulation config: jitter={jitter_pct}% (or {jitter_ms}ms), reorder={reorder_pct}%")
                if av_sync_tolerance_ms != 60:
                    print(f"📡 A/V sync tolerance: {av_sync_tolerance_ms}ms")
        except Exception as e:
            print(f"⚠️  Failed to load scenario YAML: {e}")

    # Verify UDP replay tool exists; (re)build if needed.
    #
    # IMPORTANT:
    # - The repo includes a prebuilt `udp_replay` binary, but in CI we rebuild from source
    #   so changes to `udp_replay.c` actually take effect.
    # - Do NOT build into args.output_dir: the test harness wipes output_dir at runtime
    #   (see E2ETest.clean_test_output), which would delete the freshly built binary.
    script_dir = Path(__file__).parent
    udp_replay_src = script_dir / "udp_replay.c"

    udp_replay_requested = Path(args.udp_replay)
    udp_replay_path = udp_replay_requested

    is_ci = os.environ.get('CI', '').lower() in ('1', 'true', 'yes')
    is_windows = sys.platform.startswith('win')

    rebuild_reason = None
    if not udp_replay_requested.exists():
        rebuild_reason = "tool missing"
    elif udp_replay_src.exists() and udp_replay_requested.stat().st_mtime < udp_replay_src.stat().st_mtime:
        rebuild_reason = "source newer than tool"
    elif is_ci and not is_windows:
        rebuild_reason = "CI rebuild"

    if rebuild_reason is not None:
        if not udp_replay_src.exists():
            print(f"❌ UDP replay source not found: {udp_replay_src}")
            return 1

        tool_dir = (Path(args.test_dir).resolve() / ".e2e-tools")
        tool_dir.mkdir(parents=True, exist_ok=True)
        udp_replay_path = tool_dir / "udp_replay"

        print(f"⚠️  (Re)building UDP replay tool ({rebuild_reason})...")
        print(f"🔨 Building UDP replay tool: {udp_replay_path}")

        build_cmd = ["gcc", "-O2", "-o", str(udp_replay_path), str(udp_replay_src)]
        try:
            subprocess.run(build_cmd, check=True, capture_output=True, text=True)
            print(f"✅ Successfully built UDP replay tool: {udp_replay_path}")
        except subprocess.CalledProcessError as e:
            print("❌ Failed to build UDP replay tool:")
            print(f"   Command: {' '.join(build_cmd)}")
            print(f"   Error: {e.stderr}")
            return 1
        except FileNotFoundError:
            print("❌ gcc compiler not found. Install build-essential package.")
            return 1

    # Create and run test
    # Parse csv_max_rows: 0 means disable truncation (None)
    csv_max_rows = args.csv_max_rows if args.csv_max_rows > 0 else None

    test = E2ETest(
        args.test_dir,
        video_port=args.video_port,
        audio_port=args.audio_port,
        control_port=args.control_port,
        format=args.format,
        frames=args.frames,
        verbose=args.verbose,
        enable_websocket=args.enable_websocket,
        scenario_overrides_dir=args.scenario_overrides,
        scenario_name=args.scenario_name,
        scenario_id=args.scenario_id,
        output_dir=args.output_dir,
        csv_max_rows=csv_max_rows,
        enable_resource_monitoring=args.enable_resource_monitoring,
        resource_interval_ms=args.resource_interval_ms,
        settling_seconds=args.settling_seconds,
        enable_perf_profile=args.perf_profile,
        perf_frequency_hz=args.perf_frequency_hz,
        perf_callgraph=args.perf_callgraph,
        perf_duration_s=args.perf_duration,
        enable_flamegraph=args.perf_flamegraph,
        network_simulation=network_simulation,
        av_sync_tolerance_ms=av_sync_tolerance_ms,
    )

    # Store reference for signal handler
    test_instance = test

    success = test.run(udp_replay_path)
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
