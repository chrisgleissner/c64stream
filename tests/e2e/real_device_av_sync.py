#!/usr/bin/env python3
"""
C64 Stream - Real Device A/V Sync Runner
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Runs a local OBS recording against a real C64 Ultimate device and analyzes A/V pop deltas.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import time
from pathlib import Path
from typing import Optional
import platform
import subprocess

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from util import av_pop_analyzer  # noqa: E402
import e2e  # noqa: E402
from util import test_av_sync  # noqa: E402


class RealDeviceE2E(e2e.E2ETest):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._backed_up_obs_files: list[tuple[Path, Path]] = []
        self._created_obs_files: list[Path] = []

    def _backup_file(self, path: Path) -> None:
        if not path.exists():
            return
        backup_path = path.with_name(path.name + ".real_device_backup")
        if backup_path.exists():
            backup_path = path.with_name(path.name + f".real_device_backup.{int(time.time())}")
        shutil.copy2(path, backup_path)
        self._backed_up_obs_files.append((backup_path, path))

    def _restore_obs_files(self) -> None:
        for backup_path, original_path in self._backed_up_obs_files:
            try:
                if backup_path.exists():
                    shutil.copy2(backup_path, original_path)
                    backup_path.unlink()
            except Exception:
                pass
        self._backed_up_obs_files.clear()
        for created_path in self._created_obs_files:
            try:
                if created_path.exists():
                    created_path.unlink()
            except Exception:
                pass
        self._created_obs_files.clear()

    def create_obs_profile(self):
        """Create or update a dedicated OBS profile/scene collection without deleting user configs."""
        obs_config_dir = Path.home() / ".config" / "obs-studio"
        obs_config_dir.mkdir(parents=True, exist_ok=True)

        config_source = SCRIPT_DIR / "config" / "obs-studio"
        if not config_source.exists():
            raise RuntimeError(f"Baseline OBS config not found: {config_source}")

        global_ini_src = config_source / "global.ini"
        global_ini_dst = obs_config_dir / "global.ini"
        if global_ini_src.exists():
            if global_ini_dst.exists():
                self._backup_file(global_ini_dst)
            else:
                self._created_obs_files.append(global_ini_dst)
            shutil.copy2(global_ini_src, global_ini_dst)

        profile_src = config_source / "basic" / "profiles" / "C64StreamTest" / "basic.ini"
        profile_dst_dir = obs_config_dir / "basic" / "profiles" / "C64StreamTest"
        profile_dst_dir.mkdir(parents=True, exist_ok=True)
        profile_dst = profile_dst_dir / "basic.ini"
        if profile_src.exists():
            if profile_dst.exists():
                self._backup_file(profile_dst)
            else:
                self._created_obs_files.append(profile_dst)
            shutil.copy2(profile_src, profile_dst)

        scenes_src_dir = config_source / "basic" / "scenes"
        scenes_dst_dir = obs_config_dir / "basic" / "scenes"
        scenes_dst_dir.mkdir(parents=True, exist_ok=True)
        for scene_name in ("C64StreamTest.json", "Untitled.json"):
            src = scenes_src_dir / scene_name
            dst = scenes_dst_dir / scene_name
            if src.exists():
                if dst.exists():
                    self._backup_file(dst)
                else:
                    self._created_obs_files.append(dst)
                shutil.copy2(src, dst)

        self._replace_config_variables(obs_config_dir)
        self._cleanup_obs_state_files(obs_config_dir)
        return profile_dst_dir

    def cleanup(self):
        super().cleanup()
        self._restore_obs_files()


def _replace_or_add(lines: list[str], key: str, value: str) -> list[str]:
    pattern = re.compile(rf"^\s*{re.escape(key)}\s*=")
    replaced = False
    out_lines: list[str] = []
    for line in lines:
        if pattern.match(line):
            out_lines.append(f"{key}={value}\n")
            replaced = True
        else:
            out_lines.append(line)
    if not replaced:
        out_lines.append(f"{key}={value}\n")
    return out_lines


def apply_properties_overrides(
    properties_path: Path,
    host: str,
    dns_server_ip: str,
    control_port: int,
    video_port: int,
    audio_port: int,
) -> None:
    # NOTE: The plugin normally applies properties.ini values as OBS defaults only.
    # If the scene collection has user-values (e.g. c64_host=127.0.0.1), those
    # will override defaults and the real-device test will still connect to localhost.
    # Setting is_ci=true makes the plugin enforce values as both defaults + direct.
    overrides = {
        "c64_host": host,
        "control_port": str(control_port),
        "video_port": str(video_port),
        "audio_port": str(audio_port),
        "dns_server_ip": dns_server_ip,
        "auto_detect_ip": "true",
        "obs_ip_address": "",
        "record_csv": "true",
        "record_video": "false",
        "record_frames": "false",
        "debug_logging": "true",
        "is_ci": "true",
    }

    text = properties_path.read_text(encoding="utf-8", errors="ignore")
    lines = text.splitlines(keepends=True)

    # Ensure `is_ci=true` is seen early while parsing so subsequent keys are enforced.
    # Remove existing is_ci entries and re-insert it near the top (after any header comments).
    lines = [ln for ln in lines if not re.match(r"^\s*is_ci\s*=", ln)]
    insert_at = 0
    while insert_at < len(lines):
        stripped = lines[insert_at].strip()
        if not stripped or stripped.startswith("#") or stripped.startswith(";"):
            insert_at += 1
            continue
        break
    lines.insert(insert_at, "is_ci=true\n")

    for key, value in overrides.items():
        lines = _replace_or_add(lines, key, value)
    if lines and not lines[-1].endswith("\n"):
        lines[-1] = lines[-1] + "\n"
    properties_path.write_text("".join(lines), encoding="utf-8")


def copy_recording(output_dir: Path, cutoff_time_s: Optional[float]) -> Optional[Path]:
    local_candidates = []
    for ext in (".mp4", ".hybrid_mp4", ".mkv"):
        local_candidates.extend(output_dir.glob(f"*{ext}"))
    if local_candidates:
        local_candidates.sort(key=lambda p: p.stat().st_mtime, reverse=True)
        return local_candidates[0]

    recordings_base = Path.home() / "Documents" / "obs-studio" / "c64stream" / "recordings"
    if not recordings_base.exists():
        return None

    candidates = []
    for ext in (".mp4", ".hybrid_mp4", ".mkv"):
        candidates.extend(recordings_base.rglob(f"*{ext}"))

    if not candidates:
        return None

    if cutoff_time_s is not None:
        candidates = [p for p in candidates if p.stat().st_mtime >= cutoff_time_s - 5.0]
    candidates.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    recording = candidates[0]
    suffix = ".mp4" if recording.suffix == ".hybrid_mp4" else recording.suffix
    dest = output_dir / f"c64_recording{suffix}"
    try:
        shutil.copy2(recording, dest)
        return dest
    except Exception:
        return None


def _analyze_mp4_recording(recording: Optional[Path], tolerance_ms: float) -> dict:
    if not recording:
        return {"status": "missing", "details": "No recording available", "path": None}

    try:
        results = test_av_sync.verify_av_sync(
            recording,
            tolerance_ms=tolerance_ms,
            audio_threshold_factor=1.8,
            audio_min_duration_ms=6,
            envelope_window_ms=1,
        )
        diffs = [
            d["difference_ms"]
            for d in results.get("sync_details", [])
            if d.get("included_in_analysis", True) and d.get("difference_ms") is not None
        ]
        if not diffs:
            return {
                "status": "fail",
                "details": "No matched pop events in MP4 analysis",
                "path": str(recording),
            }
        diffs_sorted = sorted(diffs)
        def _percentile(vals: list[float], pct: float) -> float:
            if not vals:
                return 0.0
            if pct <= 0:
                return vals[0]
            if pct >= 100:
                return vals[-1]
            idx = int(round((pct / 100.0) * (len(vals) - 1)))
            idx = max(0, min(idx, len(vals) - 1))
            return vals[idx]
        return {
            "status": "ok",
            "path": str(recording),
            "pop_count": len(diffs),
            "max_delta_ms": max(diffs),
            "avg_delta_ms": sum(diffs) / len(diffs),
            "p50_delta_ms": _percentile(diffs_sorted, 50.0),
            "p95_delta_ms": _percentile(diffs_sorted, 95.0),
            "tolerance_ms": results.get("tolerance_ms"),
            "sync_accuracy_percent": results.get("sync_accuracy_percent"),
            "total_audio_pops": results.get("total_audio_pops"),
            "total_video_pops": results.get("total_video_pops"),
            "total_analyzed": results.get("total_analyzed"),
            "channel_match_count": results.get("channel_match_count"),
            "channel_mismatch_count": results.get("channel_mismatch_count"),
            "channels_all_match": results.get("channels_all_match"),
            "deltas_ms": diffs,
        }
    except Exception as exc:
        return {"status": "error", "details": str(exc), "path": str(recording)}


def _get_git_info() -> dict[str, str]:
    info = {"revision": "unknown", "dirty": "unknown"}
    try:
        rev = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
        info["revision"] = rev
        dirty = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
        info["dirty"] = "yes" if dirty else "no"
    except Exception:
        pass
    return info


def _get_obs_version(obs_log: Optional[Path]) -> str:
    if not obs_log or not obs_log.exists():
        return "unknown"
    try:
        text = obs_log.read_text(errors="replace")
    except Exception:
        return "unknown"
    match = re.search(r"OBS\s+([\d\.]+)", text)
    return match.group(1) if match else "unknown"


def write_session_readme(
    output_dir: Path,
    report: dict,
    args: argparse.Namespace,
    recording: Optional[Path],
    obs_csv: Optional[Path],
    network_csv: Optional[Path],
    obs_log: Optional[Path],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime())
    git_info = _get_git_info()
    obs_version = _get_obs_version(obs_log)
    python_version = sys.version.split()[0]

    disk_usage = shutil.disk_usage(str(output_dir))
    disk_total_gb = disk_usage.total / (1024**3)
    disk_free_gb = disk_usage.free / (1024**3)

    sources = report.get("sources", {})
    authoritative = report.get("authoritative_source", "unknown")
    errors = report.get("errors", [])

    def _fmt1(value: object) -> str:
        if value is None:
            return "n/a"
        try:
            return f"{float(value):.1f}"
        except (TypeError, ValueError):
            return str(value)

    def _link(path: Optional[Path], label: str) -> str:
        if path and path.exists():
            rel = path.name
            return f"[{label}]({rel})"
        return f"{label} (missing)"

    readme_lines = [
        "# Real C64U A/V Sync Report",
        "",
        f"Generated: {timestamp}",
        "",
        "## Test configuration",
        f"- Host: {args.host}",
        f"- Format: {args.format}",
        f"- Duration: {args.duration} seconds",
        f"- Video Port: {args.video_port}",
        f"- Audio Port: {args.audio_port}",
        f"- Control Port: {args.control_port}",
        f"- Max Delta Threshold: {_fmt1(args.max_delta_ms)} ms",
        f"- Min Pop Events: {args.min_pop_events}",
        "",
        "## Test invocation",
        f"- Command: `{' '.join(sys.argv)}`",
        "",
        "## Build information",
        f"- Git revision: {git_info['revision']}",
        f"- Dirty: {git_info['dirty']}",
        "",
        "## System information",
        f"- OS: {platform.system()} {platform.release()}",
        f"- Architecture: {platform.machine()}",
        f"- CPU: {platform.processor() or 'unknown'}",
        f"- Python: {python_version}",
        f"- OBS: {obs_version}",
        f"- Disk (output dir): {disk_total_gb:.1f}GiB total, {disk_free_gb:.1f}GiB free",
        "",
        "## Files analyzed",
        f"- Recording: {_link(recording, 'c64_recording')}",
        f"- OBS CSV: {_link(obs_csv, 'obs.csv')}",
        f"- Network CSV: {_link(network_csv, 'network.csv')}",
        f"- OBS Log: {_link(obs_log, 'obs_log.txt')}",
        f"- Report: {_link(output_dir / 'av_pop_report.json', 'av_pop_report.json')}",
        "",
        "## A/V Sync Summary",
        f"- Status: {report.get('status', 'unknown')}",
        f"- Authoritative Source: {authoritative}",
        f"- Max Delta (authoritative): {_fmt1(report.get('max_delta_ms'))} ms",
        f"- Avg Delta (authoritative): {_fmt1(report.get('avg_delta_ms'))} ms",
        f"- Pop Count (authoritative): {report.get('pop_count')}",
        "",
        "### What the delta means",
        "- Each value is the absolute A/V offset between a matched audio pop and video pop.",
        "- Smaller deltas mean tighter A/V alignment; this is not a network or capture latency.",
        "",
        "### Sources",
    ]

    for key in ("obs_csv", "network_csv", "obs_log", "obs_mp4_recording"):
        if key not in sources:
            continue
        src = sources.get(key, {})
        line = f"- {key}: "
        if src.get("status") in ("error", "fail", "missing"):
            line += f"{src.get('status')} ({src.get('details', 'no details')})"
        else:
            line += (
                f"max={_fmt1(src.get('max_delta_ms'))}ms, "
                f"avg={_fmt1(src.get('avg_delta_ms'))}ms, "
                f"p50={_fmt1(src.get('p50_delta_ms'))}ms, "
                f"p95={_fmt1(src.get('p95_delta_ms'))}ms, "
                f"pops={src.get('pop_count')}"
            )
        readme_lines.append(line)

    if errors:
        readme_lines += ["", "### Errors", *[f"- {e}" for e in errors]]

    readme_path = output_dir / "README.md"
    readme_path.write_text("\n".join(readme_lines) + "\n", encoding="utf-8")


def run(args: argparse.Namespace) -> int:
    if not os.environ.get("DISPLAY"):
        print("DISPLAY is not set; OBS needs a running graphical environment.")
        return 1

    output_dir = Path(args.output_dir).resolve()
    properties_path = (
        Path.home()
        / ".config"
        / "obs-studio"
        / "plugins"
        / "c64stream"
        / "data"
        / "properties.ini"
    )
    properties_existed = properties_path.exists()

    test = RealDeviceE2E(
        test_dir=str(SCRIPT_DIR),
        video_port=args.video_port,
        audio_port=args.audio_port,
        control_port=args.control_port,
        format=args.format,
        frames=max(1, int(args.duration * (59.826 if args.format == "NTSC" else 50.125))),
        verbose=args.verbose,
        enable_websocket=args.enable_websocket,
        output_dir=str(output_dir),
        csv_max_rows=None,
    )

    obs_csv = None
    network_csv = None
    obs_log = None

    try:
        test.clean_test_output()

        if not test.copy_e2e_properties():
            print("Failed to apply E2E properties.")
            return 1

        if not properties_path.exists():
            print(f"properties.ini not found: {properties_path}")
            return 1

        apply_properties_overrides(
            properties_path=properties_path,
            host=args.host,
            dns_server_ip=args.dns_server_ip,
            control_port=args.control_port,
            video_port=args.video_port,
            audio_port=args.audio_port,
        )

        if not test.start_obs(start_recording=True):
            print("Failed to start OBS.")
            return 1

        if not test.wait_for_plugin_initialization(timeout=20):
            print("Plugin did not initialize within timeout.")
            return 1

        if not test.wait_for_receiver_threads(timeout=10):
            print("Receiver threads did not become ready within timeout.")
            return 1

        time.sleep(args.duration)
        test.stop_recording()
        test.stop_obs()

        recording = copy_recording(output_dir, test._obs_start_time_s)
        if recording:
            print(f"Recording copied to: {recording}")
        else:
            print("No recording found.")

        csv_success = test.check_csv_recordings()
        if not csv_success:
            print("No CSV outputs found.")

        obs_log = test._collect_obs_log()
        if obs_log:
            print(f"OBS log copied to: {obs_log}")

        obs_csv = output_dir / "obs.csv"
        network_csv = output_dir / "network.csv"

        exit_code, report = av_pop_analyzer.analyze_paths(
            obs_csv=obs_csv if obs_csv.exists() else None,
            network_csv=network_csv if network_csv.exists() else None,
            obs_log=obs_log if obs_log else None,
            max_delta_ms=args.max_delta_ms,
            p50_max_ms=args.p50_max_ms,
            p95_max_ms=args.p95_max_ms,
            max_max_ms=args.max_max_ms,
            min_pop_events=args.min_pop_events,
            verbose=args.verbose,
        )
        if not args.no_mp4_analysis:
            mp4_source = _analyze_mp4_recording(recording, args.max_delta_ms)
            if "sources" not in report or not isinstance(report["sources"], dict):
                report["sources"] = {}
            report["sources"]["obs_mp4_recording"] = mp4_source
        else:
            print("Skipping MP4 analysis (--no-mp4-analysis)")
        report_path = output_dir / "av_pop_report.json"
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        av_pop_analyzer.print_summary(report)
        print(f"A/V pop report: {report_path}")
        write_session_readme(
            output_dir=output_dir,
            report=report,
            args=args,
            recording=recording,
            obs_csv=obs_csv if obs_csv.exists() else None,
            network_csv=network_csv if network_csv.exists() else None,
            obs_log=obs_log,
        )
        return exit_code
    finally:
        test.cleanup()
        if not properties_existed and properties_path.exists():
            try:
                properties_path.unlink()
            except Exception:
                pass


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a real-device A/V sync capture and analyze A/V pop deltas."
    )
    parser.add_argument("--host", default="c64u", help="C64 Ultimate hostname or IP (default: c64u)")
    parser.add_argument(
        "--dns-server-ip",
        default="192.168.1.1",
        help="DNS server IP for resolving --host when it is a hostname (default: 192.168.1.1)",
    )
    parser.add_argument("--duration", type=int, default=10, help="Recording duration in seconds")
    parser.add_argument(
        "--output-dir",
        default=str(SCRIPT_DIR / "results" / "real_c64u_av_sync"),
        help="Output directory for artifacts",
    )
    parser.add_argument("--max-delta-ms", type=float, default=30.0, help="Legacy max allowed A/V delta in ms")
    parser.add_argument("--p50-max-ms", type=float, default=20.0, help="Max allowed p50 A/V delta in ms")
    parser.add_argument("--p95-max-ms", type=float, default=40.0, help="Max allowed p95 A/V delta in ms")
    parser.add_argument("--max-max-ms", type=float, default=60.0, help="Max allowed max A/V delta in ms")
    parser.add_argument("--min-pop-events", type=int, default=2, help="Minimum pop events required")
    parser.add_argument("--video-port", type=int, default=21000, help="Video UDP port")
    parser.add_argument("--audio-port", type=int, default=21001, help="Audio UDP port")
    parser.add_argument("--control-port", type=int, default=64, help="Control TCP port")
    parser.add_argument("--format", choices=["PAL", "NTSC"], default="NTSC", help="Video format hint")
    parser.add_argument("--enable-websocket", action="store_true", help="Enable OBS WebSocket calls")
    parser.add_argument("--verbose", action="store_true", help="Verbose logging")
    parser.add_argument("--no-mp4-analysis", action="store_true", help="Skip MP4 recording analysis")

    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
