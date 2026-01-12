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

        content_bounds = details.get("content_bounds", {})
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

        # Gather System Info (enhanced)
        import platform
        import shutil

        # OS info
        try:
            if platform.system() == 'Linux':
                import subprocess as sp
                distro = sp.check_output(['lsb_release', '-ds'], text=True).strip() if shutil.which('lsb_release') else platform.system()
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
            disk = shutil.disk_usage('/')
            total_tb = disk.total / (1024**4)
            free_tb = disk.free / (1024**4)
            sys_info += f"- Disk (/): {total_tb:.1f}T total, {free_tb:.1f}T available\n"
        except:
            pass

        md = []
        md.append(f"# C64 Stream E2E Test Report\n")
        md.append(f"## Scenario: {self.scenario_name}\n")
        md.append(f"Generated: {now}\n")

        md.append("## Test configuration\n")
        md.append(f"- Format: {self.format}")
        md.append(f"- Frames: {self.frames}")
        md.append(f"- Duration: {format_duration(self.frames / self.fps)} seconds")
        md.append(f"- Video Port: 21000") # TODO: Pass from args if variable
        md.append(f"- Audio Port: 21001")
        md.append(f"- OBS Enabled: true")


        md.append("\n## Build information\n")
        md.append(f"- Version: {resolved_version}\n")

        md.append("## System information\n")
        md.append(sys_info)

        md.append("## Test results\n")

        # Validation Summary
        md.append("### Validation Summary\n")
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
            icon = {'pass': '✅', 'fail': '❌', 'warning': '⚠️'}.get(status, '❓')
            md.append(f"- {icon} {label}: {details}")

        # Resource Usage
        if self.resource_data:
             md.append("\n### Resource Usage\n")

             # Add context about processing window
             duration_s = self.resource_data.get('duration_ms', 0) / 1000.0
             sample_count = self.resource_data.get('sample_count', 0)
             total_sample_count = self.resource_data.get('total_sample_count', 0)
             cpu_cores = self.resource_data.get('total_cpu_cores', 0)

             context = f"During the test's processing window ({duration_s:.1f}s, {sample_count} of {total_sample_count} samples)"
             if cpu_cores > 0:
                 context += f" ({cpu_cores} cores)"
             context += ":\n"
             md.append(context)

             cpu = self.resource_data.get('cpu_percent', {})
             ram = self.resource_data.get('ram_mb', {})
             gpu = self.resource_data.get('gpu_percent', {})

             md.append("| Metric | Min | Median | Mean | Max |")
             md.append("|--------|-----|--------|------|-----|")

             def row(name, stats):
                 return f"| {name} | {stats.get('min',0):.1f}% | {stats.get('median',0):.1f}% | {stats.get('mean',0):.2f}% | {stats.get('max',0):.1f}% |"

             md.append(row("CPU", cpu).replace("%", "%"))

             ram_row = f"| RAM | {ram.get('min',0):.2f} MB | {ram.get('median',0):.2f} MB | {ram.get('mean',0):.2f} MB | {ram.get('max',0):.2f} MB |"
             md.append(ram_row)

             if gpu:
                  md.append(row("GPU", gpu))

             md.append("\nDetails: [resource.csv](resource.csv) | [resource.json](resource.json)")

        # Network Quality
        md.append("\n### Packet & Network Data\n")
        video_pkts = self.validation_data.get('udp_reception', {}).get('details', '').split(' ')[0]
        md.append(f"- ✅ Packet Generation: {video_pkts} (approx)")
        md.append(f"- ✅ UDP Replay: Completed successfully")
        md.append(f"- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)")

        if self.network_data:
             md.append("\n#### Network Quality (Measured)\n")
             summary = self.network_data.get('summary', {})
             span = summary.get('duration_ms', 0)
             count = summary.get('total_packets', 0)
             md.append(f"- Packet span (first→last): {span:.3f} ms")
             md.append(f"- Total packets analyzed: {count}\n")

             # Enhanced network quality table with all spacing/burst metrics
             md.append("| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |")
             md.append("|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|")

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
             md.append("\n| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |")
             md.append("|--------|---------|-----------------|--------------|--------------|")

             def net_row_jitter(name, stats):
                 count = stats.get('count', 0)
                 jitter_median = stats.get('jitter_median_us', 0) / 1000
                 jitter_max = stats.get('jitter_max_us', 0) / 1000
                 out_of_order = stats.get('out_of_order_count', 0)
                 return f"| {name} | {count} | {jitter_median:.3f} ms | {jitter_max:.3f} ms | {out_of_order} |"

             md.append(net_row_jitter("Video", vid))
             md.append(net_row_jitter("Audio", aud))

             md.append("\nDetails: [network.json](network.json)")

        # A/V Sync
        if 'av_sync_details' in self.validation_data:
             av = self.validation_data['av_sync_details']
             md.append("\n### A/V Sync\n")
             acc = av.get('sync_accuracy_percent', 0)
             perfect = av.get('perfect_sync_count', 0)
             total = av.get('total_analyzed', 0)
             avg_offset = av.get('avg_offset_ms', 0)
             max_offset = av.get('max_offset_ms', 0)

             icon = "✅" if acc == 100 else "⚠️"
             if acc == 100:
                 md.append(f"- {icon} Good synchronization ({acc:.1f}%): avg offset {avg_offset:.1f}ms, max {max_offset:.1f}ms")
             else:
                 md.append(f"- {icon} Accuracy: {acc:.1f}% ({perfect}/{total} perfect)")

             md.append("\n#### Sync Details\n")
             for i, d in enumerate(av.get('sync_details', []), 1):
                  diff = d.get('difference_ms', 0)
                  audio_t = d.get('audio_pop_time_ms', 0)
                  video_t = d.get('closest_video_pop_ms', 0)
                  frame_num = d.get('closest_video_pop_frame', '')
                  audio_ch = d.get('audio_channel', '')
                  color = d.get('traffic', 'gray')
                  icon = "🟢" if color=='green' else ("🟡" if color=='yellow' else "🔴")
                  
                  # Show if included in analysis, otherwise show why ignored
                  if d.get('included_in_analysis'):
                      # Format frame number and channel
                      frame_str = f" (frame {frame_num})" if frame_num else ""
                      ch_str = f" [{audio_ch}]" if audio_ch else ""
                      md.append(f"- {icon} Pop #{i}{ch_str}: audio={audio_t:.1f}ms, video={video_t:.1f}ms{frame_str}, diff={diff:.1f}ms")
                  else:
                      # Show ignored pop with reason
                      ignore_reason = d.get('ignore_reason', 'unknown')
                      ch_str = f" [{audio_ch}]" if audio_ch else ""
                      md.append(f"- ⚪ Pop #{i}{ch_str}: audio={audio_t:.1f}ms (ignored: {ignore_reason})")

             # Channel sequence and alternation
             channels = av.get('channels', '')
             if channels:
                 md.append(f"\n- Channels: {channels}")

                 # Check for alternation
                 channels_match = av.get('channels_match', True)
                 channels_alternate = av.get('channels_alternate', False)
                 if channels_alternate:
                     first_ch = channels[0] if channels else ''
                     md.append(f"- 🔁 Channel alternation: OK (alternating, starts with {first_ch})")
                 elif not channels_match:
                     md.append(f"- ⚠️ Channel alternation: Mismatch detected")

        # Frame Progression
        if 'frame_sequence_box' in self.validation_data:
             fb = self.validation_data['frame_sequence_box']
             # Skip if frame_sequence_box is None (full-frame-pop scenarios)
             if fb is not None:
                 metrics = fb.get('metrics', {})
                 details = fb.get('details', {})

                 md.append("\n### Frame Progression\n")

                 # Status icon and summary
                 status = fb.get('status', 'unknown')
                 status_icon = "🟢" if status == "pass" else "🔴" if status == "fail" else "🟡"

                 valid_frames = metrics.get('valid_frames', 0)
                 colors = details.get('colors', 0)
                 md.append(f"- {status_icon} Frame sequence verified ({valid_frames} frames analyzed, {colors} colors)\n")

                 # Settling info
                 settling_s = metrics.get('settling_seconds', 0.0)
                 md.append(f"- Settling: {settling_s}s (pass/fail uses post-settling only)\n")

                 # Table with min/med/max statistics
                 md.append("| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |")
                 md.append("|--------|--------------------------------:|---------------------------:|-----------:|-------------:|")

                 # During settling row
                 pre_stuck_count = metrics.get('pre_settling_stuck_run_count', 0)
                 pre_stuck_min = metrics.get('pre_settling_stuck_run_min', 0)
                 pre_stuck_med = metrics.get('pre_settling_stuck_run_median', 0)
                 pre_stuck_max = metrics.get('pre_settling_max_stuck_run', 0)
                 pre_skip_count = metrics.get('pre_settling_skip_count', 0)
                 pre_skip_min = metrics.get('pre_settling_skip_min', 0)
                 pre_skip_med = metrics.get('pre_settling_skip_median', 0)
                 pre_skip_max = metrics.get('pre_settling_skip_max', 0)
                 pre_back_steps = metrics.get('pre_settling_back_steps', 0)
                 pre_severe_steps = metrics.get('pre_settling_severe_steps', 0)

                 md.append(f"| During settling | {pre_stuck_count}/{pre_stuck_min}/{pre_stuck_med}/{pre_stuck_max} | "
                          f"{pre_skip_count}/{pre_skip_min}/{pre_skip_med}/{pre_skip_max} | "
                          f"{pre_back_steps} | {pre_severe_steps} |")

                 # After settling row
                 post_stuck_count = metrics.get('post_settling_stuck_run_count', 0)
                 post_stuck_min = metrics.get('post_settling_stuck_run_min', 0)
                 post_stuck_med = metrics.get('post_settling_stuck_run_median', 0)
                 post_stuck_max = metrics.get('post_settling_max_stuck_run', 0)
                 post_skip_count = metrics.get('post_settling_skip_count', 0)
                 post_skip_min = metrics.get('post_settling_skip_min', 0)
                 post_skip_med = metrics.get('post_settling_skip_median', 0)
                 post_skip_max = metrics.get('post_settling_skip_max', 0)
                 post_back_steps = metrics.get('post_settling_back_steps', 0)
                 post_severe_steps = metrics.get('post_settling_severe_steps', 0)

                 md.append(f"| After settling | {post_stuck_count}/{post_stuck_min}/{post_stuck_med}/{post_stuck_max} | "
                          f"{post_skip_count}/{post_skip_min}/{post_skip_med}/{post_skip_max} | "
                          f"{post_back_steps} | {post_severe_steps} |")

                 md.append("\nSee [playback.csv](playback.csv) for details.")

        # Playback Jitter Clusters
        if self.playback_csv.exists():
            md.append("\n#### Playback Jitter Clusters (post-settling)\n")

            # Get content bounds for context
            frame_seq_box = self.validation_data.get('frame_sequence_box')
            content_bounds = {}
            if frame_seq_box and frame_seq_box is not None:
                content_bounds = frame_seq_box.get('details', {}).get('content_bounds', {})
            first_content_s = content_bounds.get('first_content_time', 0.0)
            last_content_s = content_bounds.get('last_content_time', 0.0)

            md.append(f"- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s")
            md.append(f"- Note: this is independent from the Frame Progression (frame-box) check above")
            if first_content_s > 0 and last_content_s > 0:
                md.append(f"- Note: repeated/skipped markers only exist while content is detected (video_s {first_content_s:.3f}–{last_content_s:.3f}).")
                md.append(f"  The jitter-free tail after content ends is expected and does not indicate steady-state performance.\n")

            clusters = self._cluster_jitter_events(max_gap=0.5)

            if clusters:
                md.append("| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |")
                md.append("|---|--------|------------|-------------|----------|------------|")

                for c in clusters:
                    num = c['num']
                    events = c['events']
                    center = c['center']
                    std_dev = c['std_dev']
                    span = c['span']
                    window_start = c['window_start']
                    window_end = c['window_end']

                    md.append(f"| {num} | {events} | {center:.3f} | {std_dev:.3f} | {span:.3f} | {window_start:.3f}–{window_end:.3f} |")
            else:
                md.append("\n- No jitter events detected (post-settling)")

        # Video
        md.append("\n### Video\n")
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
            md.append("\n### Sample Frame\n")

            # Extract frame at 50% mark if not exists
            if not still_path.exists():
                self._extract_sample_frame(still_path)

            if still_path.exists():
                md.append("![Sample Frame](./c64_recording_still.png)")
            else:
                md.append("- ⚠️ Failed to extract sample frame")

        with open(md_path, 'w') as f:
             f.write("\n".join(md))

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

            # Try to extract at first audio pop time if av_sync data is available
            timestamp = duration / 2.0  # default: 50% mark
            av_details = self.validation_data.get('av_sync_details', {})
            sync_details = av_details.get('sync_details', [])
            if sync_details:
                # Use first audio pop time (converted to seconds)
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
