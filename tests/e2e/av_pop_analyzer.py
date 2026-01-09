#!/usr/bin/env python3
"""
C64 Stream - A/V Pop Analyzer
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Analyze A/V pop deltas from obs.csv, network.csv, or obs.log.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path
from typing import Any, Optional


def _percentile_nearest_rank(values: list[float], p: float) -> Optional[float]:
    if not values:
        return None
    if p <= 0:
        return min(values)
    if p >= 100:
        return max(values)
    values = sorted(values)
    # Nearest-rank method: https://en.wikipedia.org/wiki/Percentile#The_nearest-rank_method
    import math

    k = int(math.ceil((p / 100.0) * len(values)))
    k = max(1, min(k, len(values)))
    return values[k - 1]


def _infer_video_standard_from_pop_period_ms(video_pop_times_us: list[int]) -> Optional[dict[str, Any]]:
    # av-sync-auto emits a pop every N frames (currently 48). Rather than requiring a user-provided PAL/NTSC flag,
    # infer the standard from the observed pop period:
    # - NTSC: ~60 fps => 48 frames ~= 800ms
    # - PAL:  ~50 fps => 48 frames ~= 960ms
    if len(video_pop_times_us) < 3:
        return None

    times_us = sorted(video_pop_times_us)
    periods_ms: list[float] = []
    for a, b in zip(times_us, times_us[1:]):
        dt_us = b - a
        if dt_us <= 0:
            continue
        periods_ms.append(dt_us / 1000.0)
    if len(periods_ms) < 2:
        return None

    p50_period_ms = _percentile_nearest_rank(periods_ms, 50.0)
    if p50_period_ms is None:
        return None

    ntsc_ms = 800.0
    pal_ms = 960.0
    dist_ntsc = abs(p50_period_ms - ntsc_ms)
    dist_pal = abs(p50_period_ms - pal_ms)
    inferred = "NTSC" if dist_ntsc <= dist_pal else "PAL"
    return {
        "inferred_video_standard": inferred,
        "pop_period_p50_ms": p50_period_ms,
        "pop_period_p95_ms": _percentile_nearest_rank(periods_ms, 95.0),
    }

def _collapse_pop_times(times_us: list[int], min_gap_us: int) -> list[int]:
    if not times_us:
        return []
    times_us.sort()
    clusters = [[times_us[0]]]
    for ts_us in times_us[1:]:
        if ts_us - clusters[-1][-1] <= min_gap_us:
            clusters[-1].append(ts_us)
        else:
            clusters.append([ts_us])
    # Use the median timestamp to avoid early noise skewing the pop time.
    collapsed: list[int] = []
    for cluster in clusters:
        if not cluster:
            continue
        cluster.sort()
        collapsed.append(cluster[len(cluster) // 2])
    return collapsed


def _compute_deltas(
    video_times_us: list[int],
    audio_times_us: list[int],
    match_window_us: int = 250_000,
) -> Optional[dict[str, Any]]:
    if not video_times_us or not audio_times_us:
        return None
    video_times_us = sorted(video_times_us)
    audio_times_us = sorted(audio_times_us)

    # Match one audio pop to each video pop.
    # This avoids a single spurious audio pop creating a 1s-ish "nearest" mismatch.
    deltas_ms: list[float] = []
    ai = 0
    unmatched_video = 0
    for vt in video_times_us:
        while ai + 1 < len(audio_times_us) and abs(audio_times_us[ai + 1] - vt) <= abs(audio_times_us[ai] - vt):
            ai += 1
        delta_us = abs(audio_times_us[ai] - vt)
        if delta_us > match_window_us:
            unmatched_video += 1
            continue
        deltas_ms.append(delta_us / 1000.0)

    if not deltas_ms:
        return None

    p50_ms = _percentile_nearest_rank(deltas_ms, 50.0)
    p95_ms = _percentile_nearest_rank(deltas_ms, 95.0)
    max_ms = max(deltas_ms)

    return {
        "pop_count": len(deltas_ms),
        "max_delta_ms": max_ms,
        "avg_delta_ms": sum(deltas_ms) / len(deltas_ms),
        "p50_delta_ms": p50_ms,
        "p95_delta_ms": p95_ms,
        "deltas_ms": deltas_ms,
        "audio_pops": len(audio_times_us),
        "video_pops": len(video_times_us),
        "unmatched_video_pops": unmatched_video,
    }


def analyze_obs_csv(csv_path: Path) -> tuple[Optional[dict[str, Any]], Optional[str]]:
    try:
        with open(csv_path, "r", newline="") as f:
            reader = csv.DictReader(f)
            if not reader.fieldnames:
                return None, "obs.csv is empty or missing headers"
            if "is_all_white" not in reader.fieldnames or "has_signal" not in reader.fieldnames:
                return None, "obs.csv missing debug columns (is_all_white/has_signal)"

            video_times_us: list[int] = []
            audio_times_us: list[int] = []
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
                        video_times_us.append(ts_us)
                elif event_type == "audio":
                    if (row.get("has_signal") or "") == "1":
                        audio_times_us.append(ts_us)

            video_times_us = _collapse_pop_times(video_times_us, 100000)
            audio_times_us = _collapse_pop_times(audio_times_us, 100000)
            result = _compute_deltas(video_times_us, audio_times_us)
            if result is None:
                return None, "obs.csv contains no pop events"
            if result.get("unmatched_video_pops", 0) > 0:
                return None, f"obs.csv has {result['unmatched_video_pops']} unmatched video pop(s)"
            return result, None
    except Exception as exc:
        return None, f"obs.csv parse failed: {exc}"


def analyze_network_csv(csv_path: Path) -> tuple[Optional[dict[str, Any]], Optional[str]]:
    try:
        with open(csv_path, "r", newline="") as f:
            reader = csv.DictReader(f)
            if not reader.fieldnames:
                return None, "network.csv is empty or missing headers"
            if "is_all_white" not in reader.fieldnames or "has_signal" not in reader.fieldnames:
                return None, "network.csv missing debug columns (is_all_white/has_signal)"

            video_times_by_frame: dict[int, int] = {}
            audio_times_us: list[int] = []
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
                        if not frame_num:
                            continue
                        try:
                            frame_id = int(frame_num)
                        except ValueError:
                            continue
                        existing = video_times_by_frame.get(frame_id)
                        if existing is None or ts_us < existing:
                            video_times_by_frame[frame_id] = ts_us
                elif packet_type == "audio":
                    if (row.get("has_signal") or "") == "1":
                        audio_times_us.append(ts_us)

            video_times_us = list(video_times_by_frame.values())
            audio_times_us = _collapse_pop_times(audio_times_us, 100000)
            result = _compute_deltas(video_times_us, audio_times_us)
            if result is None:
                return None, "network.csv contains no pop events"
            if result.get("unmatched_video_pops", 0) > 0:
                return None, f"network.csv has {result['unmatched_video_pops']} unmatched video pop(s)"
            return result, None
    except Exception as exc:
        return None, f"network.csv parse failed: {exc}"


def analyze_obs_log(log_path: Path) -> tuple[Optional[dict[str, Any]], Optional[str]]:
    try:
        video_re = re.compile(r"A/V pop video #\d+: .*?ts=(\d+)\s+ns")
        audio_re = re.compile(r"A/V pop audio #\d+: .*?ts=(\d+)\s+ns")
        # Also support the newer paired log format emitted by the plugin:
        # "AV SYNC: offset=... video_ts=<ns> audio_ts=<ns>"
        pair_re = re.compile(r"AV SYNC:.*?video_ts=(\d+)\s+audio_ts=(\d+)")
        video_times_us: list[int] = []
        audio_times_us: list[int] = []
        for line in log_path.read_text(errors="replace").splitlines():
            pair_match = pair_re.search(line)
            if pair_match:
                try:
                    v_ns = int(pair_match.group(1))
                    a_ns = int(pair_match.group(2))
                    video_times_us.append(v_ns // 1000)
                    audio_times_us.append(a_ns // 1000)
                    continue
                except ValueError:
                    pass
            video_match = video_re.search(line)
            if video_match:
                try:
                    ts_ns = int(video_match.group(1))
                    video_times_us.append(ts_ns // 1000)
                except ValueError:
                    continue
            audio_match = audio_re.search(line)
            if audio_match:
                try:
                    ts_ns = int(audio_match.group(1))
                    audio_times_us.append(ts_ns // 1000)
                except ValueError:
                    continue

        video_times_us = _collapse_pop_times(video_times_us, 100000)
        audio_times_us = _collapse_pop_times(audio_times_us, 100000)
        result = _compute_deltas(video_times_us, audio_times_us)
        if result is None:
            return None, "obs.log contains no pop events"
        if result.get("unmatched_video_pops", 0) > 0:
            return None, f"obs.log has {result['unmatched_video_pops']} unmatched video pop(s)"
        return result, None
    except Exception as exc:
        return None, f"obs.log parse failed: {exc}"


def _find_input_files(input_dir: Path) -> dict[str, Optional[Path]]:
    candidates = {
        "obs_csv": None,
        "network_csv": None,
        "obs_log": None,
    }
    if not input_dir.exists():
        return candidates

    obs_csv = input_dir / "obs.csv"
    network_csv = input_dir / "network.csv"
    if obs_csv.exists():
        candidates["obs_csv"] = obs_csv
    if network_csv.exists():
        candidates["network_csv"] = network_csv

    for name in ("obs_log.txt", "obs.log", "obs.txt"):
        log_path = input_dir / name
        if log_path.exists():
            candidates["obs_log"] = log_path
            break

    if candidates["obs_csv"] or candidates["network_csv"]:
        return candidates

    session_dirs = sorted(
        [p for p in input_dir.glob("session_*") if p.is_dir()],
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    if session_dirs:
        session = session_dirs[0]
        obs_csv = session / "obs.csv"
        network_csv = session / "network.csv"
        if obs_csv.exists():
            candidates["obs_csv"] = obs_csv
        if network_csv.exists():
            candidates["network_csv"] = network_csv

    return candidates


def analyze_paths(
    obs_csv: Optional[Path],
    network_csv: Optional[Path],
    obs_log: Optional[Path],
    max_delta_ms: float,
    p50_max_ms: float,
    p95_max_ms: float,
    max_max_ms: float,
    min_pop_events: int,
    verbose: bool = False,
) -> tuple[int, dict[str, Any]]:
    sources: dict[str, Any] = {}
    errors: list[str] = []

    obs_result = None
    network_result = None
    log_result = None

    if obs_csv:
        obs_result, err = analyze_obs_csv(obs_csv)
        if err:
            errors.append(err)
        if obs_result:
            sources["obs_csv"] = obs_result
            sources["obs_csv"]["path"] = str(obs_csv)

    if network_csv:
        network_result, err = analyze_network_csv(network_csv)
        if err:
            errors.append(err)
        if network_result:
            sources["network_csv"] = network_result
            sources["network_csv"]["path"] = str(network_csv)

    if obs_result:
        authoritative = "obs_csv"
        combined = {
            "max_delta_ms": obs_result["max_delta_ms"],
            "avg_delta_ms": obs_result["avg_delta_ms"],
            "p50_delta_ms": obs_result.get("p50_delta_ms"),
            "p95_delta_ms": obs_result.get("p95_delta_ms"),
            "pop_count": obs_result["pop_count"],
            "authoritative_source": authoritative,
        }
    elif network_result:
        authoritative = "network_csv"
        combined = {
            "max_delta_ms": network_result["max_delta_ms"],
            "avg_delta_ms": network_result["avg_delta_ms"],
            "p50_delta_ms": network_result.get("p50_delta_ms"),
            "p95_delta_ms": network_result.get("p95_delta_ms"),
            "pop_count": network_result["pop_count"],
            "authoritative_source": authoritative,
        }
    else:
        combined = None
        authoritative = None

    if not combined and obs_log:
        log_result, err = analyze_obs_log(obs_log)
        if err:
            errors.append(err)
        if log_result:
            sources["obs_log"] = log_result
            sources["obs_log"]["path"] = str(obs_log)
            combined = {
                "max_delta_ms": log_result["max_delta_ms"],
                "avg_delta_ms": log_result["avg_delta_ms"],
                "p50_delta_ms": log_result.get("p50_delta_ms"),
                "p95_delta_ms": log_result.get("p95_delta_ms"),
                "pop_count": log_result["pop_count"],
                "authoritative_source": "obs_log",
            }
            authoritative = "obs_log"

    if combined is None:
        report = {
            "status": "fail",
            "errors": errors or ["No usable inputs provided"],
            "sources": sources,
        }
        return 1, report

    status = "pass"
    if combined["pop_count"] < min_pop_events:
        status = "fail"
        errors.append(
            f"Too few pop events ({combined['pop_count']} < {min_pop_events})"
        )

    # New acceptance criteria: p50 <= p50_max_ms, p95 <= p95_max_ms, max <= max_max_ms.
    # Keep --max-delta-ms for backwards compatibility, but treat it as an extra (optional) max constraint.
    p50_val = combined.get("p50_delta_ms")
    p95_val = combined.get("p95_delta_ms")
    max_val = combined.get("max_delta_ms")

    if p50_val is not None and p50_val > p50_max_ms:
        status = "fail"
        errors.append(f"p50 delta {p50_val:.2f}ms exceeds {p50_max_ms:.2f}ms")
    if p95_val is not None and p95_val > p95_max_ms:
        status = "fail"
        errors.append(f"p95 delta {p95_val:.2f}ms exceeds {p95_max_ms:.2f}ms")
    if max_val is not None and max_val > max_max_ms:
        status = "fail"
        errors.append(f"Max delta {max_val:.2f}ms exceeds {max_max_ms:.2f}ms")
    if max_val is not None and max_val > max_delta_ms:
        status = "fail"
        errors.append(f"Max delta {max_val:.2f}ms exceeds legacy max-delta-ms {max_delta_ms:.2f}ms")

    # Infer PAL/NTSC from video pop cadence (best-effort) for convenience.
    inferred = None
    if authoritative == "obs_csv" and obs_result:
        # We can only infer from the full list of pop times; easiest is to re-parse quickly from the source path.
        pass
    # We'll attempt inference from whichever CSV is authoritative and available via its path.
    try:
        if authoritative == "obs_csv" and obs_csv:
            with open(obs_csv, "r", newline="") as f:
                reader = csv.DictReader(f)
                video_pop_times: list[int] = []
                for row in reader:
                    if (row.get("event_type") or "").strip() != "video":
                        continue
                    if (row.get("is_all_white") or "") != "1":
                        continue
                    elapsed_us = row.get("elapsed_us")
                    if not elapsed_us:
                        continue
                    video_pop_times.append(int(float(elapsed_us)))
                inferred = _infer_video_standard_from_pop_period_ms(video_pop_times)
        elif authoritative == "network_csv" and network_csv:
            with open(network_csv, "r", newline="") as f:
                reader = csv.DictReader(f)
                video_pop_times: list[int] = []
                for row in reader:
                    if (row.get("packet_type") or "").strip() != "video":
                        continue
                    if (row.get("is_all_white") or "") != "1":
                        continue
                    elapsed_us = row.get("elapsed_us")
                    if not elapsed_us:
                        continue
                    video_pop_times.append(int(float(elapsed_us)))
                inferred = _infer_video_standard_from_pop_period_ms(video_pop_times)
    except Exception:
        inferred = None

    report = {
        "status": status,
        "authoritative_source": authoritative,
        "max_delta_ms": combined["max_delta_ms"],
        "avg_delta_ms": combined["avg_delta_ms"],
        "p50_delta_ms": combined.get("p50_delta_ms"),
        "p95_delta_ms": combined.get("p95_delta_ms"),
        "pop_count": combined["pop_count"],
        "thresholds": {
            "p50_max_ms": p50_max_ms,
            "p95_max_ms": p95_max_ms,
            "max_max_ms": max_max_ms,
            "legacy_max_delta_ms": max_delta_ms,
            "min_pop_events": min_pop_events,
        },
        "sources": sources,
        "errors": errors,
    }
    if inferred:
        report.update(inferred)
    return 0 if status == "pass" else 1, report


def print_summary(report: dict[str, Any]) -> None:
    status = report.get("status", "fail")
    print("A/V pop analysis")
    print(f"- status: {status}")
    if "authoritative_source" in report and report.get("authoritative_source"):
        print(f"- authoritative: {report['authoritative_source']}")
    print(f"- max_delta_ms: {report.get('max_delta_ms')}")
    print(f"- avg_delta_ms: {report.get('avg_delta_ms')}")
    if report.get("p50_delta_ms") is not None:
        print(f"- p50_delta_ms: {report.get('p50_delta_ms')}")
    if report.get("p95_delta_ms") is not None:
        print(f"- p95_delta_ms: {report.get('p95_delta_ms')}")
    print(f"- pop_count: {report.get('pop_count')}")

    if report.get("inferred_video_standard"):
        print(f"- inferred_video_standard: {report.get('inferred_video_standard')}")
        if report.get("pop_period_p50_ms") is not None:
            print(f"- pop_period_p50_ms: {report.get('pop_period_p50_ms')}")

    thresholds = report.get("thresholds", {})
    if thresholds:
        print(
            f"- thresholds: p50_max_ms={thresholds.get('p50_max_ms')}, "
            f"p95_max_ms={thresholds.get('p95_max_ms')}, "
            f"max_max_ms={thresholds.get('max_max_ms')}, "
            f"min_pop_events={thresholds.get('min_pop_events')}"
        )

    sources = report.get("sources", {})
    for key in ("obs_csv", "network_csv", "obs_log", "obs_mp4_recording"):
        if key not in sources:
            continue
        src = sources[key]
        print(
            f"- {key}: max_delta_ms={src.get('max_delta_ms')}, "
            f"avg_delta_ms={src.get('avg_delta_ms')}, "
            f"pop_count={src.get('pop_count')}"
        )

    errors = report.get("errors", [])
    if errors:
        print("- errors:")
        for err in errors:
            print(f"  - {err}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Analyze A/V pop deltas from obs.csv, network.csv, or obs.log."
    )
    parser.add_argument("--obs-csv", type=str, default=None, help="Path to obs.csv")
    parser.add_argument("--network-csv", type=str, default=None, help="Path to network.csv")
    parser.add_argument("--obs-log", type=str, default=None, help="Path to obs.log")
    parser.add_argument("--input-dir", type=str, default=None, help="Directory containing obs.csv/network.csv/obs_log")
    parser.add_argument("--max-delta-ms", type=float, default=30.0, help="Legacy max allowed A/V delta in ms")
    parser.add_argument("--p50-max-ms", type=float, default=20.0, help="Max allowed p50 A/V delta in ms")
    parser.add_argument("--p95-max-ms", type=float, default=40.0, help="Max allowed p95 A/V delta in ms")
    parser.add_argument("--max-max-ms", type=float, default=60.0, help="Max allowed max A/V delta in ms")
    parser.add_argument("--min-pop-events", type=int, default=2, help="Minimum pop events required")
    parser.add_argument("--json-out", type=str, default=None, help="Optional JSON output path")
    parser.add_argument("--verbose", action="store_true", help="Verbose logging")

    args = parser.parse_args()

    obs_csv = Path(args.obs_csv).resolve() if args.obs_csv else None
    network_csv = Path(args.network_csv).resolve() if args.network_csv else None
    obs_log = Path(args.obs_log).resolve() if args.obs_log else None

    if args.input_dir:
        input_dir = Path(args.input_dir).resolve()
        discovered = _find_input_files(input_dir)
        obs_csv = obs_csv or discovered.get("obs_csv")
        network_csv = network_csv or discovered.get("network_csv")
        obs_log = obs_log or discovered.get("obs_log")

    exit_code, report = analyze_paths(
        obs_csv=obs_csv,
        network_csv=network_csv,
        obs_log=obs_log,
        max_delta_ms=args.max_delta_ms,
        p50_max_ms=args.p50_max_ms,
        p95_max_ms=args.p95_max_ms,
        max_max_ms=args.max_max_ms,
        min_pop_events=args.min_pop_events,
        verbose=args.verbose,
    )

    if args.json_out:
        try:
            Path(args.json_out).write_text(json.dumps(report, indent=2))
        except Exception as exc:
            print(f"Failed to write JSON output: {exc}", file=sys.stderr)
            exit_code = 1

    print_summary(report)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
