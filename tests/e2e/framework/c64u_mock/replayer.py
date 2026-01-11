from __future__ import annotations
import glob
import time
import random
import threading
import subprocess
import logging
from pathlib import Path
from typing import Optional, Dict, List, Any, Tuple

from ..environment import Environment

logger = logging.getLogger(__name__)

class PacketReplayer:
    """Handles generation of packet timeline and execution of C binary replayer."""

    def __init__(self, env: Environment, video_format: str = 'PAL', network_simulation: Optional[Dict] = None):
        self.env = env
        self.format = video_format
        self.network_simulation = network_simulation or {}

    def replay(self,
               udp_replay_bin: Path,
               video_dest: Tuple[str, int],
               audio_dest: Tuple[str, int]) -> bool:
        """Replay packets to destination."""

        video_dir = (self.env.packet_dir / 'video' / self.format).resolve()
        audio_dir = (self.env.packet_dir / 'audio' / self.format).resolve()

        if not video_dir.exists() or not audio_dir.exists():
            raise FileNotFoundError(f"Packet directories not found: {video_dir}, {audio_dir}")

        # Load packet files
        video_files = sorted(glob.glob(str(video_dir / "*.bin")))
        audio_files = sorted(glob.glob(str(audio_dir / "*.bin")))

        if not video_files or not audio_files:
            logger.error("❌ No packet files found")
            return False

        logger.info(f"📦 Loaded {len(video_files)} video packets, {len(audio_files)} audio packets")

        # 1. Calculate intervals
        if self.format == 'PAL':
            video_interval_us = 293.384
            audio_interval_us = 4001.417
        else:  # NTSC
            video_interval_us = 278.586
            audio_interval_us = 4005.006

        # 2. Build Timeline
        timeline = []
        start_time_us = 0

        # Add video packets to timeline
        for i, video_file in enumerate(video_files):
            timeline.append({
                'time_us': start_time_us + i * video_interval_us,
                'type': 'video',
                'file': video_file,
                'dest': video_dest
            })

        # Add audio packets to timeline
        for i, audio_file in enumerate(audio_files):
            timeline.append({
                'time_us': start_time_us + i * audio_interval_us,
                'type': 'audio',
                'file': audio_file,
                'dest': audio_dest
            })

        timeline.sort(key=lambda x: x['time_us'])

        # 3. Apply Simulation
        self._apply_simulation(timeline)

        # 4. Generate Manifests
        logger.info(f"🎯 Generated {len(timeline)} interleaved packets over {timeline[-1]['time_us']/1000:.1f}ms")

        video_manifest = [e for e in timeline if e['type'] == 'video']
        audio_manifest = [e for e in timeline if e['type'] == 'audio']

        video_manifest_path = self.env.output_dir / 'video_manifest.csv'
        audio_manifest_path = self.env.output_dir / 'audio_manifest.csv'

        self._write_manifest(video_manifest_path, video_manifest)
        self._write_manifest(audio_manifest_path, audio_manifest)

        # 5. Execute Replay
        if not udp_replay_bin.exists():
            raise FileNotFoundError(f"udp_replay not found: {udp_replay_bin}")

        # Command construction
        # udp-replay --host <ip> --port <port> --manifest <csv> --dir <dir>

        video_cmd = [
            '--host', video_dest[0],
            '--port', str(video_dest[1]),
            '--manifest', str(video_manifest_path),
            '--dir', str(video_dir)
        ]

        audio_cmd = [
            '--host', audio_dest[0],
            '--port', str(audio_dest[1]),
            '--manifest', str(audio_manifest_path),
            '--dir', str(audio_dir)
        ]

        return self._execute_parallel_replay(udp_replay_bin, video_cmd, audio_cmd)

    def _apply_simulation(self, timeline: List[Dict]):
        """Apply jitter and reordering."""
        random.seed()

        jitter_percent = self.network_simulation.get('jitter_percent', 0)
        max_jitter_ms = self.network_simulation.get('max_jitter_ms', 0)

        if max_jitter_ms > 0:
            jitter_count = 0
            max_jitter_us = max_jitter_ms * 1000
            for i in range(1, len(timeline)):
                jitter = random.uniform(0, max_jitter_us)
                timeline[i]['time_us'] += jitter
                jitter_count += 1
            logger.info(f"📊 Jitter enabled: 0-{max_jitter_ms}ms positive delay applied to {jitter_count} packets")

        elif jitter_percent > 0:
            jitter_count = 0
            for i in range(1, len(timeline)):
                prev_time = timeline[i-1]['time_us']
                current_time = timeline[i]['time_us']
                base_interval = current_time - prev_time
                jitter_range = base_interval * jitter_percent / 100.0
                jitter = random.uniform(0, jitter_range)
                timeline[i]['time_us'] += jitter
                jitter_count += 1
            logger.info(f"📊 Jitter enabled: 0-{jitter_percent}% positive delay applied to {jitter_count} packet intervals")

        reorder_percent = self.network_simulation.get('reorder_percent', 0)
        reorder_max_delay_ms = self.network_simulation.get('reorder_max_delay_ms', 0)

        if reorder_percent > 0 and reorder_max_delay_ms > 0:
            reorder_count = 0
            for event in timeline:
                if random.randint(0, 99) < reorder_percent:
                    delay_us = random.randint(0, reorder_max_delay_ms * 1000)
                    event['time_us'] += delay_us
                    reorder_count += 1
            logger.info(f"🔀 Packet reordering enabled: {reorder_percent}% probability, 0-{reorder_max_delay_ms}ms buffer")

        timeline.sort(key=lambda x: x['time_us'])

    def _write_manifest(self, path: Path, events: List[Dict]):
        """Write manifest CSV."""
        MIN_PACKET_DELAY_US = 50 if self.env.is_ci else 1

        with open(path, 'w') as f:
            f.write("filename,delay_us\n")
            last_sent_time_us = 0

            for event in events:
                event_time_us = int(round(float(event['time_us'])))
                delta_us = event_time_us - last_sent_time_us
                if delta_us < MIN_PACKET_DELAY_US:
                    delta_us = MIN_PACKET_DELAY_US
                filename = Path(event['file']).name
                f.write(f"{filename},{int(delta_us)}\n")
                last_sent_time_us += int(delta_us)

    def _execute_parallel_replay(self, bin_path: Path, video_cmd: List[str], audio_cmd: List[str]) -> bool:
        """Execute video and audio replay in parallel."""
        replay_start_time = time.time()

        video_result = {'rc': None, 'lines': []}
        audio_result = {'rc': None, 'lines': []}

        def run_sender(args, label, result_dict):
            full_cmd = [str(bin_path)] + args
            try:
                proc = subprocess.Popen(
                    full_cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1
                )
                lines = []
                for line in proc.stdout:
                    line = line.rstrip('\n')
                    if line:
                        if len(lines) < 200: lines.append(line)
                        # logger.info(f"[{label}] {line}") # maybe verbose only?

                result_dict['rc'] = proc.wait()
                result_dict['lines'] = lines
            except Exception as e:
                result_dict['rc'] = -1
                result_dict['lines'] = [str(e)]

        t1 = threading.Thread(target=run_sender, args=(video_cmd, 'UDP-VIDEO', video_result))
        t2 = threading.Thread(target=run_sender, args=(audio_cmd, 'UDP-AUDIO', audio_result))

        t1.start()
        t2.start()
        t1.join()
        t2.join()

        elapsed_ms = (time.time() - replay_start_time) * 1000

        success = True
        if video_result['rc'] != 0:
            logger.error("❌ Video sender failed")
            for l in video_result['lines'][-10:]: logger.error(f"[UDP-VIDEO] {l}")
            success = False

        if audio_result['rc'] != 0:
            logger.error("❌ Audio sender failed")
            for l in audio_result['lines'][-10:]: logger.error(f"[UDP-AUDIO] {l}")
            success = False

        if success:
            logger.info(f"✅ Packet replay complete in {elapsed_ms:.1f}ms")

        return success
