from __future__ import annotations
import json
import logging
from pathlib import Path
from typing import Optional, Dict, Tuple, List

logger = logging.getLogger(__name__)

class NetworkTimingValidator:
    """Validates network timing metrics from collected logs."""

    @staticmethod
    def validate(
        network_json_path: Path,
        video_format: str,
        frames: int,
        network_simulation: Optional[Dict],
        packet_source: str = 'mock',
    ) -> Tuple[str, str, List[str], List[str]]:
        """Validate sender pacing using derived metrics in network.json.

        Returns: (status, details, errors, warnings)
        - status: pass|warning|fail|unknown
        """
        if network_simulation is None:
            network_simulation = {}

        if not network_json_path.exists():
            return 'unknown', 'network.json not found', [], []

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

        errors: List[str] = []
        warnings: List[str] = []

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
            if str(packet_source or 'mock').strip().lower() == 'device':
                max_ok_ms = (expected_duration_ms * 2.0) + extra_delay_ms + 2000.0
            else:
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

        if max_jitter_ms <= 0 and float(network_simulation.get('reorder_percent', 0) or 0) <= 0:
            v_p99_us = video_stats.get('spacing_p99_us', None)
            a_p99_us = audio_stats.get('spacing_p99_us', None)

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
