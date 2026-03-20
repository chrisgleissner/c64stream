#!/usr/bin/env python3
"""
Report Generator for C64 Stream E2E Tests.
Generates README.md and playback.csv from test artifacts.
"""

import sys
import json
import csv
import os
import math
import subprocess
from pathlib import Path
from datetime import datetime, timezone
from typing import Dict, Any, Optional, List, Tuple

def load_json(path: Path) -> Dict[str, Any]:
    if not path.exists():
        return {}
    try:
        with open(path, 'r') as f:
            return json.load(f)
    except Exception:
        return {}

def format_duration(seconds: float) -> str:
    return f"{seconds:.1f}"

def _format_trimmed(value: float, decimals: int = 2) -> str:
    fmt = f"{value:.{decimals}f}"
    if "." in fmt:
        fmt = fmt.rstrip("0").rstrip(".")
    return fmt

def _format_mmss(seconds: float) -> str:
    if seconds < 0:
        seconds = 0.0
    minutes = int(seconds // 60)
    sec = seconds - (minutes * 60)
    sec_whole = int(sec)
    sec_tenths = int(round((sec - sec_whole) * 10))
    if sec_tenths == 10:
        sec_whole += 1
        sec_tenths = 0
        if sec_whole == 60:
            minutes += 1
            sec_whole = 0
    return f"{minutes:02d}:{sec_whole:02d}.{sec_tenths:d}"

def _get_git_info(project_root: Path) -> Tuple[str, str]:
    branch = "unknown"
    git_id = "unknown"
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=str(project_root),
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode == 0 and result.stdout.strip():
            branch = result.stdout.strip()
    except Exception:
        pass

    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(project_root),
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode == 0 and result.stdout.strip():
            git_id = result.stdout.strip()
    except Exception:
        pass

    return branch, git_id

class ReportGenerator:
    def __init__(self, output_dir: Path, scenario_name: str, video_format: str, frames: int,
                 project_root: Path):
        self.output_dir = output_dir
        self.scenario_name = scenario_name
        self.format = video_format
        self.frames = frames
        self.project_root = project_root

        self.fps = 50.0 if video_format == 'PAL' else 60.0
        if video_format == 'NTSC':
            self.fps = 59.826  # More precise NTSC

        self.validation_file = output_dir / 'validation_results.json'
        self.resource_file = output_dir / 'resource.json'
        self.network_file = output_dir / 'network.json'
        self.av_sync_file = output_dir / 'av-sync.csv'
        self.playback_csv = output_dir / 'playback.csv'
        self.obs_csv = self._find_obs_csv()
        self.recording_path = self._find_recording()

        self.validation_data = load_json(self.validation_file)
        self.resource_data = load_json(self.resource_file)
        self.network_data = load_json(self.network_file)

    def _find_obs_csv(self) -> Optional[Path]:
        # Search for obs.csv in subdirectories (session folders)
        for f in self.output_dir.glob('**/obs.csv'):
            return f
        return None

    def _find_recording(self) -> Optional[Path]:
        if (self.output_dir / 'c64_recording.mp4').exists():
            return self.output_dir / 'c64_recording.mp4'
        # Check for timestamped mp4s
        files = sorted(self.output_dir.glob('*.mp4'), key=lambda p: p.stat().st_mtime, reverse=True)
        return files[0] if files else None

    def _cluster_jitter_events(self, max_gap: float = 0.5) -> List[Dict[str, Any]]:
        """Cluster jitter events (repeated/skipped) from playback.csv.

        Returns list of clusters with: events, center, std_dev, span, window
        """
        if not self.playback_csv.exists():
            return []

        # Read playback.csv and find jitter events
        events = []
        frame_seq_box = self.validation_data.get('frame_sequence_box')
        settling_seconds = 0.0
        if frame_seq_box and frame_seq_box is not None:
            settling_seconds = frame_seq_box.get('metrics', {}).get('settling_seconds', 0.0)

        try:
            with open(self.playback_csv, 'r') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    # Only consider post-settling events
                    video_s_str = row.get('video_s', '')
                    if not video_s_str or video_s_str == '':
                        continue

                    try:
                        video_s = float(video_s_str)
                    except ValueError:
                        continue

                    if video_s < settling_seconds:
                        continue

                    # Check for jitter (repeated=1 or skipped=1)
                    repeated = row.get('repeated', '0')
                    skipped = row.get('skipped', '0')

                    if repeated == '1' or skipped == '1':
                        events.append(video_s)
        except Exception as e:
            print(f"Warning: Failed to parse playback.csv for jitter clustering: {e}")
            return []

        if not events:
            return []

        # Cluster events with max gap
        events.sort()
        clusters = []
        current_cluster = [events[0]]

        for i in range(1, len(events)):
            if events[i] - current_cluster[-1] <= max_gap:
                current_cluster.append(events[i])
            else:
                # Close current cluster
                clusters.append(current_cluster)
                current_cluster = [events[i]]

        # Don't forget last cluster
        if current_cluster:
            clusters.append(current_cluster)

        # Calculate statistics for each cluster
        results = []
        for idx, cluster in enumerate(clusters, start=1):
            n = len(cluster)
            center = sum(cluster) / n

            # Calculate std dev
            if n > 1:
                variance = sum((x - center) ** 2 for x in cluster) / n
                std_dev = math.sqrt(variance)
            else:
                std_dev = 0.0

            span = max(cluster) - min(cluster) if n > 1 else 0.0
            window_start = min(cluster)
            window_end = max(cluster)

            results.append({
                'num': idx,
                'events': n,
                'center': center,
                'std_dev': std_dev,
                'span': span,
                'window_start': window_start,
                'window_end': window_end
            })

        return results

    def _load_playback_jitter_events(
        self, settling_seconds: float
    ) -> Tuple[List[float], Optional[Tuple[float, float]]]:
        events: List[float] = []
        content_video_s: List[float] = []

        if not self.playback_csv.exists():
            return events, None

        try:
            with open(self.playback_csv, 'r') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    video_s_raw = row.get('video_s')
                    content_s_raw = row.get('content_s')
                    try:
                        video_s = float(video_s_raw) if video_s_raw not in (None, '') else None
                        content_s = float(content_s_raw) if content_s_raw not in (None, '') else None
                    except Exception:
                        continue

                    if content_s is not None and video_s is not None:
                        content_video_s.append(video_s)

                    t = video_s if video_s is not None else (content_s if content_s is not None else 0.0)
                    if t < settling_seconds:
                        continue

                    try:
                        repeated = int(float(row.get('repeated') or 0))
                        skipped = int(float(row.get('skipped') or 0))
                    except Exception:
                        continue

                    if repeated or skipped:
                        events.append(t)
        except Exception as e:
            print(f"Warning: Failed to parse playback.csv for jitter clustering: {e}")
            return [], None

        events.sort()
        content_video_s.sort()
        content_span = (content_video_s[0], content_video_s[-1]) if content_video_s else None
        return events, content_span

    def generate_playback_csv(self):
        """Generate playback.csv for frame analysis."""
        frame_seq = self.validation_data.get("frame_sequence_box")
        if not frame_seq or frame_seq is None:
            return

        print(f"Generating playback.csv to {self.playback_csv}...")

        details = frame_seq.get("details", {})

        # Determine bounds
        start_frame = details.get("window", {}).get("start_frame", 0)
        end_frame = details.get("window", {}).get("end_frame", 0)

        content_bounds = details.get("content_bounds") or {}
        first_content = content_bounds.get("first_content_frame", start_frame)
        last_content = content_bounds.get("last_content_frame", end_frame)

        # Load obs frames
        obs_frame_nums = []
        if self.obs_csv and self.obs_csv.exists():
            try:
                with open(self.obs_csv, 'r') as f:
                    reader = csv.DictReader(f)
                    for row in reader:
                        if row.get('event_type') == 'video':
                            obs_frame_nums.append(int(row.get('frame_num', 0)))
            except Exception:
                pass

        # Total frames from recording if possible
        total_frames = end_frame + 1
        if self.recording_path:
             try:
                 import cv2
                 cap = cv2.VideoCapture(str(self.recording_path))
                 if cap.isOpened():
                     total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
                 cap.release()
             except ImportError:
                 pass

        # Fallback total frames
        if total_frames == 0:
            total_frames = max(end_frame + 1, 100)

        # Frame Mapping Logic (Slots -> FrameNums)
        frame_slots = details.get("frame_slots", {}) # video_idx -> slot

        video_to_frame_num = {}
        last_fn = 0

        # Pre-calculate slot lookup
        slot_map = {i: [] for i in range(8)}
        for fn in obs_frame_nums:
            slot_map[fn % 8].append(fn)

        for idx in range(first_content, min(last_content + 1, total_frames)):
            idx_str = str(idx)
            if idx_str in frame_slots:
                slot = frame_slots[idx_str]
                # Find matching frame num >= last_fn
                candidates = slot_map.get(slot, [])
                match = None
                for fn in candidates:
                    if fn >= last_fn:
                        match = fn
                        break

                # Allow repeat (same as last)
                if match is None and candidates:
                    for fn in candidates:
                         if fn == last_fn or fn == last_fn-1:
                             match = fn
                             break

                if match is not None:
                     video_to_frame_num[idx] = match
                     last_fn = match
                else:
                     # Interpolate
                     base = (last_fn // 8) * 8
                     derived = base + slot
                     if derived < last_fn: derived += 8
                     video_to_frame_num[idx] = derived
                     last_fn = derived
            else:
                 video_to_frame_num[idx] = last_fn

        # Events
        repeated_events = {e.get('frame'): e.get('count') for e in details.get("repeated_events", [])}
        skip_events = {e.get('frame'): e.get('skipped') for e in details.get("skip_events", [])}

        # AV Sync Pops
        av_details = self.validation_data.get("av_sync_details", {})
        video_pop_indices = set(av_details.get("video_pop_frame_indices", []))
        audio_pop_indices = set()
        for d in av_details.get("sync_details", []):
             if d.get("closest_video_pop_frame") is not None:
                 audio_pop_indices.add(d.get("closest_video_pop_frame"))

        first_content_time = first_content / self.fps if first_content > 0 else 0.0

        with open(self.playback_csv, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['playback_frame_index', 'frame_num', 'frame_slot', 'video_s', 'video_ssff',
                           'content_s', 'repeated', 'skipped', 'event', 'video_pop', 'audio_pop'])

            for i in range(total_frames):
                video_s = i / self.fps
                # SS:FF
                ss = int(video_s)
                ff = int((video_s - ss) * self.fps)
                video_ssff = f"{ss:02d}:{ff:02d}"

                frame_num = ""
                frame_slot = ""
                content_s = ""

                if first_content <= i <= last_content:
                    frame_num = video_to_frame_num.get(i, "")
                    frame_slot = frame_slots.get(str(i), "")
                    content_s = max(0.0, video_s - first_content_time)
                    content_s = round(content_s, 3)

                rep = repeated_events.get(i, "")
                skp = skip_events.get(i, "")
                evt = []
                if rep: evt.append("repeated")
                if skp: evt.append("skipped")
                evt_str = "+".join(evt)

                v_pop = "video_pop" if i in video_pop_indices else ""
                a_pop = "audio_pop" if i in audio_pop_indices else ""

                writer.writerow([i, frame_num, frame_slot, round(video_s, 3), video_ssff,
                               content_s, rep, skp, evt_str, v_pop, a_pop])

    def generate_markdown(self):
        """Generate README.md."""
        md_path = self.output_dir / 'README.md'
        print(f"Generating report to {md_path}...")

        now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

        resolved_version = "unknown"
        try:
             # Try buildspec or helper
             with open(self.project_root / 'buildspec.json') as f:
                  resolved_version = json.load(f).get('version', 'unknown')
        except:
             pass

        if not self.network_data:
            network_csv = self.output_dir / 'network.csv'
            if network_csv.exists():
                try:
                    from util.network_analysis import analyze_network_jitter
                    analysis = analyze_network_jitter(network_csv)
                    if isinstance(analysis, dict) and analysis:
                        self.network_data = analysis
                        with open(self.network_file, 'w') as f:
                            json.dump(analysis, f, indent=2)
                except Exception:
                    pass

        # Gather System Info (enhanced)
        import platform
        import shutil

        # OS info
        try:
            if platform.system() == 'Linux':
                import subprocess as sp
                if shutil.which('lsb_release'):
                    distro = sp.check_output(['lsb_release', '-ds'], text=True).strip().strip('"')
                elif Path('/etc/os-release').exists():
                    with open('/etc/os-release', 'r') as f:
                        for line in f:
                            if line.startswith('PRETTY_NAME='):
                                distro = line.split('=', 1)[1].strip().strip('"')
                                break
                        else:
                            distro = platform.system()
                else:
                    distro = platform.system()
                kernel = platform.release()
                sys_info = f"- OS: {distro} (kernel {kernel})\n"
            else:
                sys_info = f"- OS: {platform.system()} {platform.release()}\n"
        except:
            sys_info = f"- OS: {platform.system()} {platform.release()}\n"

        # OBS version
        try:
            obs_version = subprocess.check_output(['obs', '--version'], text=True, stderr=subprocess.DEVNULL).strip().split()[-1]
            sys_info += f"- OBS: {obs_version}\n"
        except:
            pass

        # CPU info
        cpu_model = "Unknown"
        try:
            if platform.system() == 'Linux':
                with open('/proc/cpuinfo', 'r') as f:
                    for line in f:
                        if line.startswith('model name'):
                            cpu_model = line.split(':', 1)[1].strip()
                            break
        except:
            pass
        cpu_cores = os.cpu_count() or 0
        sys_info += f"- CPU: {cpu_model} ({cpu_cores} cores)\n"

        # RAM info
        try:
            if platform.system() == 'Linux':
                with open('/proc/meminfo', 'r') as f:
                    meminfo = dict(line.split(':', 1) for line in f if ':' in line)
                    total_kb = int(meminfo.get('MemTotal', '0 kB').split()[0])
                    avail_kb = int(meminfo.get('MemAvailable', '0 kB').split()[0])
                    total_gi = total_kb / (1024 * 1024)
                    avail_gi = avail_kb / (1024 * 1024)
                    sys_info += f"- RAM: {total_gi:.0f}Gi total, {avail_gi:.0f}Gi available\n"
        except:
            pass

        # Disk info
        try:
            disk_total = None
            disk_available = None
            disk_mount = None
            try:
                result = subprocess.check_output(
                    ['df', '-h', str(self.project_root)], text=True, stderr=subprocess.DEVNULL
                )
                lines = result.strip().splitlines()
                if len(lines) >= 2:
                    parts = lines[1].split()
                    if len(parts) >= 6:
                        disk_total = parts[1]
                        disk_available = parts[3]
                        disk_mount = parts[5]
            except Exception:
                disk = shutil.disk_usage(str(self.project_root))
                total_tb = disk.total / (1024**4)
                free_tb = disk.free / (1024**4)
                disk_total = f"{total_tb:.1f}T"
                disk_available = f"{free_tb:.1f}T"
                disk_mount = str(self.project_root)

            if disk_total and disk_available and disk_mount:
                sys_info += f"- Disk ({disk_mount}): {disk_total} total, {disk_available} available\n"
        except:
            pass

        md = []
        md.append("# C64 Stream E2E Test Report")
        md.append("")
        md.append(f"## Scenario: {self.scenario_name}")
        md.append("")
        md.append(f"- Generated: {now}")
        branch, git_id = _get_git_info(self.project_root)
        env_label = "CI" if (os.environ.get("CI") or os.environ.get("GITHUB_ACTIONS")) else "local"
        md.append(f"- Git Branch: {branch}")
        md.append(f"- Git ID: {git_id}")
        md.append(f"- Environment: {env_label}")
        md.append("")
        md.append("## Test configuration")
        md.append("")
        md.append(f"- Format: {self.format}")
        md.append(f"- Frames: {self.frames}")
        md.append(f"- Duration: {format_duration(self.frames / self.fps)} seconds")
        md.append(f"- Video Port: 21000") # TODO: Pass from args if variable
        md.append(f"- Audio Port: 21001")
        md.append(f"- OBS Enabled: true")

        md.append("")
        md.append("## Build information")
        md.append("")
        md.append("- Project: c64stream")
        md.append(f"- Version: {resolved_version}")
        md.append("")
        md.append("## System information")
        md.append("")
        md.extend(sys_info.strip().splitlines())
        md.append("")
        md.append("## Test results")
        md.append("")

        # Validation Summary
        md.append("### Validation Summary")
        md.append("")
        for key, label in [
            ('udp_reception', 'UDP Packet Reception'),
            ('network_timing', 'Network Timing'),
            ('frame_processing', 'Frame Processing'),
            ('video_recording', 'Video Recording'),
            ('packet_integrity', 'Content Integrity')
        ]:
            val = self.validation_data.get(key, {})
            status = val.get('status', 'unknown')
            details = val.get('details', '')
            if key == 'udp_reception':
                metrics = val.get('metrics', {})
                expected = metrics.get('expected_total')
                received = metrics.get('received_total')
                missing = metrics.get('missing_total')
                missing_pct = metrics.get('missing_pct')
                if expected is not None and received is not None and missing is not None and missing_pct is not None:
                    details = (
                        f"Expected {expected}, Received {received}, Missing {missing} "
                        f"({_format_trimmed(float(missing_pct), 2)}%)"
                    )
            icon = {'pass': '✅', 'fail': '❌', 'warning': '⚠️'}.get(status, '❓')
            md.append(f"- {icon} {label}: {details}")

        # Resource Usage
        if self.resource_data:
             md.append("")
             md.append("### Resource Usage")
             md.append("")

             # Add context about processing window
             duration_s = self.resource_data.get('duration_ms', 0) / 1000.0
             sample_count = self.resource_data.get('sample_count', 0)
             total_sample_count = self.resource_data.get('total_sample_count', 0)
             allocated_cpus = self.resource_data.get('allocated_cpu_cores', 0)
             total_cpus = self.resource_data.get('total_cpu_cores', 0)

             if total_sample_count and total_sample_count != sample_count:
                 samples_text = f"{sample_count} of {total_sample_count} samples"
             else:
                 samples_text = f"{sample_count} samples"

             context = f"During the test's processing window ({duration_s:.1f}s, {samples_text})"
             if allocated_cpus and total_cpus and allocated_cpus < total_cpus:
                 context += f" (cgroup-limited: {_format_trimmed(allocated_cpus, 2)} of {total_cpus} cores)"
             elif total_cpus:
                 context += f" ({total_cpus} cores)"
             context += ":\n"
             md.append(context.rstrip())
             md.append("")

             cpu = self.resource_data.get('cpu_percent', {})
             ram = self.resource_data.get('ram_mb', {})
             gpu = self.resource_data.get('gpu_percent', {})

             md.append("| Metric | Min | Median | Mean | Max |")
             md.append("|--------|-----|--------|------|-----|")

             def fmt_stat(stats, key, decimals=2):
                 val = stats.get(key, 0)
                 try:
                     val = float(val)
                 except Exception:
                     return str(val)
                 return _format_trimmed(val, decimals)

             cpu_row = (
                 f"| CPU | {fmt_stat(cpu, 'min')}% | {fmt_stat(cpu, 'median')}% | "
                 f"{fmt_stat(cpu, 'mean')}% | {fmt_stat(cpu, 'max')}% |"
             )
             md.append(cpu_row)

             ram_row = (
                 f"| RAM | {fmt_stat(ram, 'min')} MB | {fmt_stat(ram, 'median')} MB | "
                 f"{fmt_stat(ram, 'mean')} MB | {fmt_stat(ram, 'max')} MB |"
             )
             md.append(ram_row)

             if gpu:
                  gpu_row = (
                      f"| GPU | {fmt_stat(gpu, 'min')}% | {fmt_stat(gpu, 'median')}% | "
                      f"{fmt_stat(gpu, 'mean')}% | {fmt_stat(gpu, 'max')}% |"
                  )
                  md.append(gpu_row)

             md.append("")
             md.append("Details: [resource.csv](resource.csv) | [resource.json](resource.json)")

        # Network Quality
        md.append("")
        md.append("### Packet & Network Data")
        md.append("")
        test_packets = self.project_root / "tests" / "e2e" / "test_packets"
        udp_details = self.validation_data.get('udp_reception', {}).get('details', '')
        if udp_details.startswith("Received"):
            md.append("- ℹ️ Packet Generation: Skipped (device packet source)")
            md.append("- ✅ UDP Capture: Device stream")
        elif test_packets.exists():
            video_count = len(list((test_packets / "video" / self.format).glob("*.bin")))
            audio_count = len(list((test_packets / "audio" / self.format).glob("*.bin")))
            if video_count or audio_count:
                md.append(f"- ✅ Packet Generation: {video_count} video, {audio_count} audio packets")
            else:
                md.append("- ⚠️ Packet Generation: Not captured")
            md.append("- ✅ UDP Replay: Completed successfully")
        else:
            md.append("- ⚠️ Packet Generation: Not captured")
            md.append("- ✅ UDP Replay: Completed successfully")

        event_links = []
        if (self.output_dir / 'network.csv').exists():
            event_links.append("[network.csv](network.csv)")
        if (self.output_dir / 'obs.csv').exists():
            event_links.append("[obs.csv](obs.csv)")
        if (self.output_dir / 'playback.csv').exists():
            event_links.append("[playback.csv](playback.csv)")
        if event_links:
            md.append(f"- Events: {', '.join(event_links)}")

        if self.network_data:
             md.append("")
             md.append("#### Network Quality (Measured)")
             md.append("")
             summary = self.network_data.get('summary', {})
             span = summary.get('duration_ms', 0)
             count = summary.get('total_packets', 0)
             if span:
                 md.append(f"- Packet span (first→last): {span:.3f} ms")
             if count:
                 md.append(f"- Total packets analyzed: {count}")
             md.append("")

             # Enhanced network quality table with all spacing/burst metrics
             md.append("| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |")
             md.append("|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|")

             # Combined all streams
             all_stats = self.network_data.get('all', {})
             vid = self.network_data.get('video', {})
             aud = self.network_data.get('audio', {})

             def net_row_enhanced(name, stats):
                 count = stats.get('count', 0)
                 spacing_min = stats.get('spacing_min_us', 0) / 1000
                 spacing_mean = stats.get('spacing_mean_us', 0) / 1000
                 spacing_max = stats.get('spacing_max_us', 0) / 1000
                 cv = stats.get('spacing_cv_pct', 0)
                 burst_short = stats.get('burst_short_pct', 0)
                 burst_long = stats.get('burst_long_pct', 0)
                 p99_p50 = stats.get('burst_p99_p50', 0)
                 return f"| {name} | {count} | {spacing_min:.3f} ms | {spacing_mean:.3f} ms | {spacing_max:.3f} ms | {cv:.2f}% | {burst_short:.2f}% | {burst_long:.2f}% | {p99_p50:.3f} |"

             if all_stats and all_stats.get('count', 0) > 0:
                 md.append(net_row_enhanced("All", all_stats))
             md.append(net_row_enhanced("Video", vid))
             md.append(net_row_enhanced("Audio", aud))

             # Jitter table
             md.append("")
             md.append("| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |")
             md.append("|--------|---------|-----------------|--------------|--------------|")

             def net_row_jitter(name, stats):
                 count = stats.get('count', 0)
                 jitter_median = stats.get('jitter_median_ms', stats.get('jitter_median_us', 0) / 1000)
                 jitter_max = stats.get('jitter_max_ms', stats.get('jitter_max_us', 0) / 1000)
                 out_of_order = stats.get('out_of_order_count', 0)
                 ooo_rate = stats.get('out_of_order_rate_pct', 0)
                 ooo_str = f"{out_of_order} ({ooo_rate:.1f}%)" if out_of_order else "0"
                 return f"| {name} | {count} | {jitter_median:.3f} ms | {jitter_max:.3f} ms | {ooo_str} |"

             md.append(net_row_jitter("Video", vid))
             md.append(net_row_jitter("Audio", aud))

             md.append("")
             md.append("Details: [network.json](network.json)")

        # A/V Sync
        av_status = self.validation_data.get('av_sync', {})
        av_status_value = av_status.get('status')
        av_status_details = av_status.get('details', '')
        if av_status_value == 'skipped':
             md.append("")
             md.append("### A/V Sync")
             md.append("")
             md.append(f"- ⏭️ {av_status_details or 'Skipped'}")
        elif 'av_sync_details' in self.validation_data:
             av = self.validation_data['av_sync_details']
             md.append("")
             md.append("### A/V Sync")
             md.append("")
             acc = av.get('sync_accuracy_percent', 0)
             is_perfect = av.get('is_perfectly_synced', False)
             sync_details = av.get('sync_details', [])
             diffs = [d.get('difference_ms') for d in sync_details if d.get('closest_video_pop_ms') is not None]
             diffs = [d for d in diffs if d is not None]
             avg_offset = sum(diffs) / len(diffs) if diffs else 0.0
             max_offset = max(diffs) if diffs else 0.0

             if is_perfect:
                 md.append(f"- ✅ Good synchronization ({acc:.1f}%): avg offset {avg_offset:.1f}ms, max {max_offset:.1f}ms")
             elif acc >= 60:
                 md.append(f"- ✅ Acceptable synchronization ({acc:.1f}%): avg offset {avg_offset:.1f}ms, max {max_offset:.1f}ms")
             else:
                 md.append(f"- ❌ Poor synchronization ({acc:.1f}%): avg offset {avg_offset:.1f}ms, max {max_offset:.1f}ms")

             md.append("")
             md.append("#### Sync Details")
             md.append("")
             for i, d in enumerate(sync_details, 1):
                  diff = d.get('difference_ms', 0)
                  audio_t = d.get('audio_pop_time_ms', 0)
                  video_t = d.get('closest_video_pop_ms', None)
                  frame_num = d.get('closest_video_pop_frame', None)
                  audio_ch = d.get('audio_channel') or d.get('channel') or '?'
                  color = d.get('traffic', '')
                  icon = "🟢" if color == 'green' else ("🟡" if color == 'yellow' else ("🔴" if color == 'red' else "•"))

                  audio_t_fmt = f"{float(audio_t):.1f}" if audio_t is not None else "0.0"
                  if video_t is not None and diff is not None:
                      if frame_num is not None and frame_num != '':
                          md.append(
                              f"- {icon} Pop #{i} [{audio_ch}]: audio={audio_t_fmt}ms, "
                              f"video={float(video_t):.1f}ms (frame {frame_num}), diff={float(diff):.1f}ms"
                          )
                      else:
                          md.append(
                              f"- {icon} Pop #{i} [{audio_ch}]: audio={audio_t_fmt}ms, "
                              f"video={float(video_t):.1f}ms, diff={float(diff):.1f}ms"
                          )
                  else:
                      md.append(f"- {icon} Pop #{i} [{audio_ch}]: audio={audio_t_fmt}ms, no matching video pop found")

             channels = "".join(
                 ("L" if (d.get('audio_channel') or d.get('channel')) == "L" else
                  "R" if (d.get('audio_channel') or d.get('channel')) == "R" else "B")
                 for d in sync_details
             )
             md.append("")
             md.append(f"- Channels: {channels}")

             lr_seq = [
                 (d.get('audio_channel') or d.get('channel'))
                 for d in sync_details
                 if (d.get('audio_channel') or d.get('channel')) in ("L", "R")
             ]
             alternates = True
             if len(lr_seq) >= 2:
                 for i in range(1, len(lr_seq)):
                     if lr_seq[i] == lr_seq[i - 1]:
                         alternates = False
                         break
             if alternates and lr_seq:
                 md.append(f"- 🔁 Channel alternation: OK (alternating, starts with {lr_seq[0]})")
             else:
                 md.append("- 🔁 Channel alternation: MISMATCH")

        # Frame Progression
        if 'frame_sequence_box' in self.validation_data:
             fb = self.validation_data['frame_sequence_box']
             # Skip if frame_sequence_box is None (full-frame-pop scenarios)
             if fb is not None:
                 metrics = fb.get('metrics', {})
                 details = fb.get('details', {})

                 md.append("")
                 md.append("### Frame Progression")
                 md.append("")

                 # Status icon and summary
                 status = fb.get('status', 'unknown')
                 status_icon = "🟢" if status == "pass" else "🔴" if status == "fail" else "🟡"
                 message = fb.get('message', '')

                 valid_frames = metrics.get('valid_frames', 0)
                 distinct_colors = metrics.get('distinct_colors', 0)
                 try:
                     valid_frames = int(float(valid_frames))
                 except Exception:
                     valid_frames = valid_frames or 0
                 try:
                     distinct_colors = int(float(distinct_colors))
                 except Exception:
                     distinct_colors = distinct_colors or 0

                 if status == "pass":
                     md.append(
                         f"- {status_icon} Frame sequence verified ({valid_frames} frames analyzed, {distinct_colors} colors)"
                     )
                 elif message:
                     md.append(f"- {status_icon} {message}")
                 else:
                     md.append(f"- {status_icon} Frame sequence verification incomplete")

                 # Settling info
                 settling_s = metrics.get('settling_seconds', details.get('settling_seconds', 0.0))
                 md.append("")
                 md.append(f"- Settling: {settling_s}s (pass/fail uses post-settling only)")

                 # Table with min/med/max statistics
                 pre_stuck_count = metrics.get('pre_settling_stuck_run_count', 0)
                 pre_skip_count = metrics.get('pre_settling_skip_count', 0)
                 pre_back_steps = metrics.get('pre_settling_back_steps', 0)
                 pre_severe_steps = metrics.get('pre_settling_severe_steps', 0)
                 post_stuck_count = metrics.get('post_settling_stuck_run_count', 0)
                 post_skip_count = metrics.get('post_settling_skip_count', 0)
                 post_back_steps = metrics.get('post_settling_back_steps', 0)
                 post_severe_steps = metrics.get('post_settling_severe_steps', 0)

                 def has_nonzero(value):
                     try:
                         return float(value) != 0.0
                     except Exception:
                         return bool(value)

                 if (
                     has_nonzero(pre_stuck_count)
                     or has_nonzero(pre_skip_count)
                     or has_nonzero(pre_back_steps)
                     or has_nonzero(pre_severe_steps)
                     or has_nonzero(post_stuck_count)
                     or has_nonzero(post_skip_count)
                     or has_nonzero(post_back_steps)
                     or has_nonzero(post_severe_steps)
                 ):
                     md.append("")
                     md.append("| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |")
                     md.append("|--------|------------------------------:|--------------------------:|-----------:|-------------:|")

                     pre_stuck_min = metrics.get('pre_settling_stuck_run_min', 0)
                     pre_stuck_med = metrics.get('pre_settling_stuck_run_median', 0)
                     pre_stuck_max = metrics.get('pre_settling_max_stuck_run', 0)
                     pre_skip_min = metrics.get('pre_settling_skip_min', 0)
                     pre_skip_med = metrics.get('pre_settling_skip_median', 0)
                     pre_skip_max = metrics.get('pre_settling_skip_max', 0)

                     post_stuck_min = metrics.get('post_settling_stuck_run_min', 0)
                     post_stuck_med = metrics.get('post_settling_stuck_run_median', 0)
                     post_stuck_max = metrics.get('post_settling_max_stuck_run', 0)
                     post_skip_min = metrics.get('post_settling_skip_min', 0)
                     post_skip_med = metrics.get('post_settling_skip_median', 0)
                     post_skip_max = metrics.get('post_settling_skip_max', 0)

                     def fmt_int(val):
                         try:
                             return str(int(float(val)))
                         except Exception:
                             return str(val)

                     md.append(
                         f"| During settling | {fmt_int(pre_stuck_count)}/{fmt_int(pre_stuck_min)}/"
                         f"{fmt_int(pre_stuck_med)}/{fmt_int(pre_stuck_max)} | "
                         f"{fmt_int(pre_skip_count)}/{fmt_int(pre_skip_min)}/"
                         f"{fmt_int(pre_skip_med)}/{fmt_int(pre_skip_max)} | "
                         f"{fmt_int(pre_back_steps)} | {fmt_int(pre_severe_steps)} |"
                     )
                     md.append(
                         f"| After settling | {fmt_int(post_stuck_count)}/{fmt_int(post_stuck_min)}/"
                         f"{fmt_int(post_stuck_med)}/{fmt_int(post_stuck_max)} | "
                         f"{fmt_int(post_skip_count)}/{fmt_int(post_skip_min)}/"
                         f"{fmt_int(post_skip_med)}/{fmt_int(post_skip_max)} | "
                         f"{fmt_int(post_back_steps)} | {fmt_int(post_severe_steps)} |"
                     )

                 if self.playback_csv.exists():
                     md.append("")
                     md.append("See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.")

        # Playback Jitter Clusters
        if self.playback_csv.exists():
            md.append("")
            md.append("#### Playback Jitter Clusters (post-settling)")
            md.append("")

            settling_seconds = 0.0
            frame_seq_box = self.validation_data.get('frame_sequence_box')
            if frame_seq_box and frame_seq_box is not None:
                settling_seconds = frame_seq_box.get('metrics', {}).get('settling_seconds', 0.0)

            events, content_span = self._load_playback_jitter_events(settling_seconds)

            if not events:
                md.append("- No post-settling repeated/skipped markers detected in playback timeline.")
            else:
                md.append("- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s")
                md.append("- Note: this is independent from the Frame Progression (frame-box) check above")
                if content_span is not None:
                    md.append(
                        f"- Note: repeated/skipped markers only exist while content is detected "
                        f"(video_s {content_span[0]:.3f}–{content_span[1]:.3f})."
                    )
                    md.append(
                        "  The jitter-free tail after content ends is expected and does not indicate steady-state performance."
                    )
                md.append("")

                max_gap_s = 0.5
                clusters = []
                bucket = [events[0]]
                prev = events[0]
                for t in events[1:]:
                    if (t - prev) <= max_gap_s:
                        bucket.append(t)
                    else:
                        clusters.append(bucket)
                        bucket = [t]
                    prev = t
                clusters.append(bucket)

                def stats(xs):
                    n = len(xs)
                    center = sum(xs) / n
                    var = sum((x - center) ** 2 for x in xs) / n if n else 0.0
                    return center, math.sqrt(var), xs[-1] - xs[0]

                summaries = []
                for c in clusters:
                    center, std, span = stats(c)
                    summaries.append((len(c), span, center, std, c[0], c[-1]))

                summaries.sort(key=lambda x: (x[0], x[1]), reverse=True)

                md.append("| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |")
                md.append("|---|--------|------------|-------------|----------|------------|")
                for i, (count, span, center, std, start, end) in enumerate(summaries[:3], start=1):
                    md.append(f"| {i} | {count} | {center:.3f} | {std:.3f} | {span:.3f} | {start:.3f}–{end:.3f} |")

        # Video
        md.append("")
        md.append("### Video")
        md.append("")
        if self.recording_path:
             name = self.recording_path.name
             md.append(f"- Download: [{name}]({name}) (Available from local runs or CI build artifacts.)")

             # Get duration using ffprobe
             try:
                 result = subprocess.run(
                     ['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
                      '-of', 'default=noprint_wrappers=1:nokey=1', str(self.recording_path)],
                     capture_output=True, text=True, check=True
                 )
                 duration = float(result.stdout.strip())
                 md.append(f"- Duration: {duration:.1f} s")
             except Exception:
                 pass
        else:
             md.append("- ❌ Recording not found")

        # Sample Frame
        still_path = self.output_dir / 'c64_recording_still.png'
        if self.recording_path and self.recording_path.exists():
            md.append("")
            md.append("")
            md.append("### Sample Frame")
            md.append("")

            # Extract frame at 50% mark if not exists
            if not still_path.exists():
                self._extract_sample_frame(still_path)

            if still_path.exists():
                md.append("![Sample Frame](./c64_recording_still.png)")

                # Add description of sample frame elements
                md.append("")
                md.append("- **Top-left**: Text box with scenario name")
                md.append("- **Top-right**: VIC-II palette reference grid of all C64 colors")
                md.append("- **Center**: Diagonal pattern cycling through all C64 colors")
                md.append("- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)")
                if av_status_value == 'skipped':
                    md.append("- **Bottom-right**: A/V pop indicator disabled for this scenario")
                else:
                    md.append("- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)")

                # Add frame context
                av_details = self.validation_data.get('av_sync_details', {})
                video_pop_frames = av_details.get('video_pop_frame_indices', [])
                video_pop_times = av_details.get('video_pop_times_ms', [])
                if video_pop_frames and video_pop_times:
                    first_video_frame = video_pop_frames[0]
                    first_video_pop_ms = video_pop_times[0]
                else:
                    sync_details = av_details.get('sync_details', [])
                    if sync_details:
                        first_video_frame = sync_details[0].get('closest_video_pop_frame', 0)
                        first_video_pop_ms = sync_details[0].get('closest_video_pop_ms', 0)
                    else:
                        first_video_frame = 0
                        first_video_pop_ms = 0

                if first_video_frame and first_video_pop_ms:
                    try:
                        result = subprocess.run(
                            ['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
                             '-of', 'default=noprint_wrappers=1:nokey=1', str(self.recording_path)],
                            capture_output=True, text=True, check=True
                        )
                        duration = float(result.stdout.strip())
                        timestamp = _format_mmss(first_video_pop_ms / 1000.0)
                        md.append(
                            f"- Taken from frame {first_video_frame} at {timestamp} of the {duration:.1f} s video above."
                        )
                    except:
                        pass
            else:
                md.append("- ⚠️ Failed to extract sample frame")

        with open(md_path, 'w') as f:
             f.write("\n".join(md) + "\n")

    def _extract_sample_frame(self, output_path: Path):
        """Extract a sample frame, preferring times with AV pops when available."""
        if not self.recording_path or not self.recording_path.exists():
            return

        try:
            # Get video duration
            result = subprocess.run(
                ['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
                 '-of', 'default=noprint_wrappers=1:nokey=1', str(self.recording_path)],
                capture_output=True, text=True, check=True
            )
            duration = float(result.stdout.strip())

            # Try to extract at first video pop time if av_sync data is available
            timestamp = duration / 2.0  # default: 50% mark
            av_details = self.validation_data.get('av_sync_details', {})
            video_pop_times = av_details.get('video_pop_times_ms', [])
            if video_pop_times:
                first_video_pop_ms = video_pop_times[0]
                if first_video_pop_ms > 0:
                    timestamp = first_video_pop_ms / 1000.0
                    print(f"Extracting sample frame at first video pop: {timestamp:.3f}s")
            else:
                sync_details = av_details.get('sync_details', [])
                if sync_details:
                    first_video_pop_ms = sync_details[0].get('closest_video_pop_ms', 0)
                    if first_video_pop_ms > 0:
                        timestamp = first_video_pop_ms / 1000.0
                        print(f"Extracting sample frame at first video pop: {timestamp:.3f}s")
                    else:
                        first_pop_ms = sync_details[0].get('audio_pop_time_ms', 0)
                        if first_pop_ms > 0:
                            timestamp = first_pop_ms / 1000.0
                            print(f"Extracting sample frame at first audio pop: {timestamp:.3f}s")

            subprocess.run(
                ['ffmpeg', '-ss', str(timestamp), '-i', str(self.recording_path),
                 '-frames:v', '1', '-q:v', '2', str(output_path), '-y'],
                capture_output=True, check=True
            )

            print(f"Extracted sample frame at {timestamp:.1f}s to {output_path.name}")
        except Exception as e:
            print(f"Warning: Failed to extract sample frame: {e}")

def main():
    if len(sys.argv) < 6:
        print("Usage: report_generator.py <output_dir> <scenario> <format> <frames> <project_root>")
        sys.exit(1)

    output_dir = Path(sys.argv[1])
    scenario = sys.argv[2]
    fmt = sys.argv[3]
    frames = int(sys.argv[4])
    root = Path(sys.argv[5])

    gen = ReportGenerator(output_dir, scenario, fmt, frames, root)
    gen.generate_playback_csv()
    gen.generate_markdown()

if __name__ == "__main__":
    main()
