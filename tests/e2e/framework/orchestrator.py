from __future__ import annotations
import os
import time
import logging
import sys
import shutil
import csv
import json
from pathlib import Path
from typing import Optional, Dict

# Import Framework Components
from .environment import Environment
from .xvfb import XvfbController
from .obs.config import OBSConfigManager
from .obs.logs import OBSLogManager
from .obs.process import OBSProcessManager
from .obs.websocket import OBSWebsocketClient, WEBSOCKET_AVAILABLE
from .c64u_mock.server import MockC64UServer
from .c64u_mock.replayer import PacketReplayer
from .monitoring.resources import ResourceManager
from .validation.recording import RecordingValidator
from .validation.results import ResultValidator
from .validation.network import NetworkTimingValidator # Import purely for logging/checking? ResultValidator uses it.
try:
    from util.network_analysis import analyze_network_jitter
    from util.constants import MEDIA_PREAMBLE_DURATION_S
except ImportError:
    # Fallback to absolute if needed (should not be needed if e2e.py adds tests/e2e to path)
    from util.network_analysis import analyze_network_jitter
    from util.constants import MEDIA_PREAMBLE_DURATION_S

logger = logging.getLogger(__name__)

class E2EOrchestrator:
    """Orchestrates the complete E2E test pipeline."""

    def __init__(self,
                 test_dir: Path,
                 output_dir: Optional[str] = None,
                 video_format: str = 'PAL',
                 frames: int = 180,
                 udp_replay_path: Optional[str] = None,
                 packet_source: str = 'mock',
                 scenario_overrides: Optional[Path] = None,
                 network_simulation: Optional[Dict] = None,
                 obs_start_recording: bool = True,
                 enable_websocket: bool = False,
                 enable_resource_monitoring: bool = False,
                 monitor_resource_interval_ms: int = 200,
                 control_port: int = 6400,
                 csv_max_rows: Optional[int] = None,
                 verbose: bool = False,
                 full_frame_pop: bool = False,
                 av_sync_tolerance_mode = None,
                 skip_frame_logic_validation: bool = False,
                 disable_pops: bool = False):

        # 1. Environment Setup
        self.env = Environment(test_dir, output_dir, csv_max_rows=csv_max_rows)
        self.format = video_format
        self.frames = frames
        self.av_sync_tolerance_mode = av_sync_tolerance_mode
        self.udp_replay_path = Path(udp_replay_path) if udp_replay_path else (self.env.test_dir / 'util' / 'udp_replay')
        self.packet_source = packet_source
        self.scenario_overrides = scenario_overrides
        self.network_simulation = network_simulation or {}
        self.obs_start_recording = obs_start_recording
        self.enable_websocket = enable_websocket
        self.enable_resource_monitoring = enable_resource_monitoring
        self.monitor_resource_interval_ms = monitor_resource_interval_ms
        self.verbose = verbose
        self.full_frame_pop = full_frame_pop
        self.skip_frame_logic_validation = skip_frame_logic_validation
        self.disable_pops = disable_pops

        # 2. Components
        # Use display from environment if set, otherwise default to :99
        display = os.environ.get('DISPLAY', ':99')
        self.xvfb = XvfbController(self.env, display=display)
        self.obs_config = OBSConfigManager(self.env)
        self.obs_logs = OBSLogManager(self.env)
        self.obs_process = OBSProcessManager(self.env, self.obs_logs)
        self.obs_ws = OBSWebsocketClient(self.env, enabled=enable_websocket)
        self.mock_server = MockC64UServer(self.env, control_port=control_port) if packet_source == 'mock' else None
        self.replayer = PacketReplayer(self.env, video_format, self.network_simulation) if packet_source == 'mock' else None
        self.resource_monitor = ResourceManager(self.env, pid=0, interval_ms=monitor_resource_interval_ms) # PID set later
        self.recording_validator = RecordingValidator(self.env)

        # Results
        self.results = {}

    def run(self) -> bool:
        """Execute the test scenario."""
        logger.info(f"🚀 Starting E2E Test: {self.format}, {self.frames} frames")

        try:
            # 1. Prepare Environment
            self.env.prepare()

            # 2. Configure OBS
            # Copy E2E properties (record_av_sync=true by default in properties_e2e_*.ini)
            # Scenarios can override via their overrides/plugins/c64stream/data/properties.ini
            if not self.obs_config.copy_e2e_properties():
                raise RuntimeError("Failed to setup plugin properties")

            profile = self.obs_config.create_obs_profile(self.format, self.scenario_overrides)

            # 3. Start Xvfb
            self.xvfb.start()

            # 4. Start Mock Server (if applicable)
            if self.mock_server:
                self.mock_server.start()

            # 5. Start OBS
            # Note: OBS process start needs to be robust
            if not self.obs_process.start(profile_name=profile.name, start_recording=self.obs_start_recording):
                raise RuntimeError("Failed to start OBS")

            # Update PID for resource monitor
            if self.obs_process.process:
                 self.resource_monitor.pid = self.obs_process.process.pid
                 if self.enable_resource_monitoring:
                      self.resource_monitor.start()

            # 6. WebSocket Setup (Optional)
            if self.enable_websocket:
                self.obs_ws.wait_for_server()

            # 7. Start Packet Flow
            replay_success = False

            if self.packet_source == 'mock' and self.mock_server and self.replayer:
                logger.info("⏳ Waiting for plugin to request streaming...")
                if self.mock_server.wait_for_trigger(timeout=30) or (self.env.is_ci and self.obs_logs.wait_for_initialization(10)):
                    # Get ports from mock server or fallback to defaults
                    vid_dest = self.mock_server.video_dest or ("127.0.0.1", 21000)
                    aud_dest = self.mock_server.audio_dest or ("127.0.0.1", 21001)

                    # Log readiness check (simplified, replayer doesn't wait strictly unless we tell it)
                    logger.info("▶️ Starting Packet Replay...")
                    replay_success = self.replayer.replay(self.udp_replay_path, vid_dest, aud_dest)
                else:
                    logger.error("❌ Timeout waiting for streaming request")

            elif self.packet_source == 'device':
                logger.info("📡 Device mode: Waiting for external packets...")
                # In device mode, we just wait for the duration
                frame_rate = 50.125 if self.format == 'PAL' else 59.826
                duration = self.frames / frame_rate
                time.sleep(duration + 5) # Safety buffer
                replay_success = True
            elif self.packet_source == 'media':
                logger.info("🎞️ Media mode: Waiting for OBS media playback...")
                frame_rate = 50.125 if self.format == 'PAL' else 59.826
                duration = self.frames / frame_rate
                # Media files have a preamble, so wait for preamble + content + buffer
                time.sleep(MEDIA_PREAMBLE_DURATION_S + duration + 2)
                replay_success = True

            # 8. Post-Run Wait (Allow flushing)
            time.sleep(2.0)

            # 9. Stop Recording/OBS
            # If we used websocket to start recording (not yet implemented in start_obs but flag checks it)
            # obs_process auto-starts recording via --startrecording argument? Yes.

            self.resource_monitor.stop()
            if self.enable_websocket and self.obs_ws.enabled:
                self.obs_ws.stop_recording()
                time.sleep(2.0)
                self.obs_ws.request_exit()
                time.sleep(2.0)
            self.obs_process.stop()
            if self.mock_server:
                self.mock_server.stop()
            self.xvfb.stop()

            # 10. Collect Outputs
            log_path = self.obs_logs.collect_latest_log(self.obs_process._start_time)
            recording_path = self.recording_validator.check_recording_output()

            # 11. Validate
            # Find and process CSVs from the session folder
            counts = self._process_csvs()

            validator = ResultValidator(
                self.env,
                self.format,
                self.frames,
                self.packet_source,
                self.network_simulation,
                self.full_frame_pop,
                self.av_sync_tolerance_mode,
                self.skip_frame_logic_validation,
                self.disable_pops,
            )
            success, results = validator.validate(replay_success, recording_path, counts)

            # Save validation results for report generation
            if self.env.output_dir:
                try:
                    with open(self.env.output_dir / 'validation_results.json', 'w') as f:
                        json.dump(results, f, indent=4, default=str)
                except Exception as e:
                    logger.warning(f"Failed to save validation_results.json: {e}")

            return success

        except Exception as e:
            logger.error(f"❌ Test Failed: {e}", exc_info=self.verbose)
            import traceback
            traceback.print_exc()
            return False
        finally:
            # Ensure cleanup happens even on error
            try:
                if self.enable_resource_monitoring: self.resource_monitor.stop()
                if self.enable_websocket and self.obs_ws.enabled:
                    self.obs_ws.stop_recording()
                    time.sleep(2.0)
                    self.obs_ws.request_exit()
                    time.sleep(2.0)
                self.obs_process.stop()
                if self.mock_server: self.mock_server.stop()
                self.xvfb.stop()
            except Exception:
                pass

            self.obs_config.restore_backup()

    def _process_csvs(self) -> Dict[str, int]:
        """Find session folder, process network.csv, and return counts."""
        counts = {'network_packets': 0, 'video_packets': 0, 'audio_packets': 0}

        try:
            # Look for CSVs in multiple locations
            # 1. Plugin default location (Documents/obs-studio/c64stream/recordings)
            # 2. Output directory (if configured to write there)

            candidates = []

            # Check default plugin location
            plugin_recordings_base = Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'
            if plugin_recordings_base.exists():
                session_folders = [f for f in plugin_recordings_base.glob('session_*') if f.is_dir()]
                for session in session_folders:
                     candidate = session / 'network.csv'
                     if candidate.exists():
                         candidates.append(candidate)

            # Check output directory
            candidates.extend(list(self.env.output_dir.glob('**/network.csv')))

            if not candidates:
                logger.warning("⚠️ No network.csv found in default location or output directory")
                return counts

            # Pick the most recent one
            candidates.sort(key=lambda p: p.stat().st_mtime, reverse=True)
            network_csv = candidates[0]
            logger.info(f"📁 Processing CSV from: {network_csv}")

            # Also copy obs.csv and av-sync.csv if present in the same folder
            session_dir = network_csv.parent
            for other_csv in ['obs.csv', 'av-sync.csv']:
                src = session_dir / other_csv
                if src.exists():
                    shutil.copy2(src, self.env.output_dir / other_csv)

            # Destination in main output dir
            dest_csv = self.env.output_dir / 'network.csv'

            # Process (copy & truncate & count)
            packet_count = 0
            video_count = 0
            audio_count = 0

            # First, perform analysis on the ORIGINAL full CSV before truncation/copying
            # This generates network.json which is needed for logging and validation
            try:
                analysis = analyze_network_jitter(network_csv)
                if analysis:
                    with open(self.env.output_dir / 'network.json', 'w') as f_json:
                        json.dump(analysis, f_json, indent=2)
                    logger.info(f"✅ Saved network analysis to: {self.env.output_dir / 'network.json'}")
            except Exception as e:
                logger.error(f"❌ Failed to analyze network jitter: {e}")

            if network_csv != dest_csv: # Avoid self-overwrite if paths match
                with open(network_csv, 'r', errors='replace') as f_in, \
                     open(dest_csv, 'w', newline='') as f_out:

                    reader = csv.DictReader(f_in)
                    if reader.fieldnames:
                        writer = csv.DictWriter(f_out, fieldnames=reader.fieldnames)
                        writer.writeheader()

                        row_idx = 0
                        max_rows = self.env.csv_max_rows or 0

                        for row in reader:
                            # Count packets
                            packet_count += 1
                            if row.get('packet_type') == 'video':
                                video_count += 1
                            elif row.get('packet_type') == 'audio':
                                audio_count += 1

                            # Write if within limit or unlimited
                            if max_rows == 0 or row_idx < max_rows:
                                writer.writerow(row)

                            row_idx += 1

            counts['network_packets'] = packet_count
            counts['video_packets'] = video_count
            counts['audio_packets'] = audio_count
            logger.info(f"📊 Processed network.csv: {packet_count} packets (Recv)")

        except Exception as e:
            logger.error(f"❌ Failed to process CSVs: {e}")

        return counts

if __name__ == "__main__":
    # Basic CLI for testing orchestrator directly
    logging.basicConfig(level=logging.INFO)
    orchestrator = E2EOrchestrator(Path("tests/e2e"))
    orchestrator.run()
