from __future__ import annotations
import statistics
import math
from pathlib import Path
from typing import Dict, List, Optional, Any
import csv

def analyze_network_jitter(network_csv: Path) -> Optional[Dict[str, Any]]:
    """
    Analyze network.csv and return jitter statistics.
    Adapted from legacy e2e_main.py logic.
    """
    if not network_csv.exists():
        return None

    try:
        video_intervals = []
        audio_intervals = []
        all_intervals = []
        video_sequence = []
        audio_sequence = []

        last_video_us = None
        last_audio_us = None

        elapsed_us_values = []

        with open(network_csv, 'r', errors='replace') as f:
            reader = csv.DictReader(f)
            # Check required columns
            required = {'elapsed_us', 'packet_type', 'sequence_num'}
            if not getattr(reader, 'fieldnames', None) or not required.issubset(reader.fieldnames):
                 return float('nan') # Signal format error

            for row in reader:
                try:
                    elapsed_us = float(row['elapsed_us'])
                    elapsed_us_values.append(elapsed_us)
                    packet_type = row.get('packet_type', 'unknown')
                    seq_str = row.get('sequence_num', '0')

                    # Compute spacing/intervals
                    interval_us = 0.0
                    if packet_type == 'video':
                         if last_video_us is not None:
                              interval_us = elapsed_us - last_video_us
                         last_video_us = elapsed_us
                    elif packet_type == 'audio':
                         if last_audio_us is not None:
                              interval_us = elapsed_us - last_audio_us
                         last_audio_us = elapsed_us

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
            k = int(math.ceil(q * len(sorted_values))) - 1
            k = max(0, min(k, len(sorted_values) - 1))
            return sorted_values[k]

        def spacing_stats(intervals):
            if len(intervals) < 2:
                return {'count': len(intervals)}

            intervals_sorted = sorted(intervals)
            median_us = statistics.median(intervals_sorted)
            mean_us = statistics.mean(intervals_sorted)
            min_us = intervals_sorted[0]
            max_us = intervals_sorted[-1]
            std_us = statistics.pstdev(intervals_sorted)
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
                if seq_list[i] < seq_list[i-1]:
                    out_of_order += 1
            rate = (out_of_order / len(seq_list)) * 100 if seq_list else 0
            return out_of_order, rate

        # Analyze video packet intervals
        if len(video_intervals) >= 2:
            video_stats = spacing_stats(video_intervals)
            video_median = video_stats.get('spacing_median_us', 0)
            video_jitter = [abs(v - video_median) for v in video_intervals]
            video_jitter_median = statistics.median(video_jitter)
            video_jitter_max = max(video_jitter)
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

        # Analyze audio packet intervals
        if len(audio_intervals) >= 2:
            audio_stats = spacing_stats(audio_intervals)
            audio_median = audio_stats.get('spacing_median_us', 0)
            audio_jitter = [abs(a - audio_median) for a in audio_intervals]
            audio_jitter_median = statistics.median(audio_jitter)
            audio_jitter_max = max(audio_jitter)
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
        # Fallback empty structure
        return None
