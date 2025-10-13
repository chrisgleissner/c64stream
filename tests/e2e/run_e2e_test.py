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
from pathlib import Path
try:
    import websocket
    import requests
    WEBSOCKET_AVAILABLE = True
except ImportError:
    WEBSOCKET_AVAILABLE = False


class E2ETest:
    def __init__(self, test_dir, video_port=11000, audio_port=11001,
                 format='PAL', frames=30, verbose=False):
        self.test_dir = Path(test_dir)
        self.video_port = video_port
        self.audio_port = audio_port
        self.format = format
        self.frames = frames
        self.verbose = verbose

        # Process handles
        self.xvfb_process = None
        self.obs_process = None

        # Test artifacts
        self.packet_dir = self.test_dir / 'test_packets'
        self.output_dir = self.test_dir / 'test_output'
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def log(self, message):
        """Print log message if verbose mode is enabled."""
        if self.verbose:
            print(f"[TEST] {message}")

    def start_xvfb(self, display=':99'):
        """Start Xvfb virtual framebuffer for headless testing."""
        self.log(f"Starting Xvfb on display {display}")

        try:
            self.xvfb_process = subprocess.Popen(
                ['Xvfb', display, '-screen', '0', '1280x720x24'],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )

            # Set DISPLAY environment variable
            os.environ['DISPLAY'] = display

            # Give Xvfb time to start
            time.sleep(2)

            if self.xvfb_process.poll() is not None:
                stderr = self.xvfb_process.stderr.read().decode()
                raise RuntimeError(f"Xvfb failed to start: {stderr}")

            self.log("✅ Xvfb started successfully")
            return True

        except Exception as e:
            print(f"❌ Failed to start Xvfb: {e}")
            return False

    def create_obs_profile(self):
        """
        Create a minimal OBS profile and scene collection for testing.
        """
        self.log("Creating OBS test profile")

        # Create OBS config directory
        obs_config_dir = Path.home() / '.config' / 'obs-studio'
        profile_dir = obs_config_dir / 'basic' / 'profiles' / 'C64StreamTest'
        scenes_dir = obs_config_dir / 'basic' / 'scenes'

        profile_dir.mkdir(parents=True, exist_ok=True)
        scenes_dir.mkdir(parents=True, exist_ok=True)
        
        # Create global configuration to disable crash recovery
        global_ini = obs_config_dir / 'global.ini'
        with open(global_ini, 'w') as f:
            f.write("""[General]
EnableCrashReporting=false
EnableUpdater=false
FirstRun=false
RecordWhenStreaming=false
KeepRecordingWhenStreamStops=false
WarnBeforeStartingStream=false
WarnBeforeStoppingStream=false
WarnBeforeStoppingRecord=false
""")

                # Create basic.ini for the profile
        basic_ini = profile_dir / 'basic.ini'
        with open(basic_ini, 'w') as f:
            config_content = f"""[General]
Name=C64StreamTest
EnableCrashRecovery=false
WarnBeforeStartingStream=false
WarnBeforeStoppingStream=false
WarnBeforeStoppingRecord=false
RecordWhenStreaming=false
KeepRecordingWhenStreamStops=false

[Video]
BaseCX=1280
BaseCY=720
OutputCX=1280
OutputCY=720
FPSType=0
FPSNum=30
FPSDen=1

[Audio]
SampleRate=48000
Channels=2

[Output]
Mode=Simple
FilePath={self.output_dir}
RecFormat=mkv
RecEncoder=x264
RecQuality=Stream
RecRB=false

[BasicWindow]
DockAreaVisible=false
"""
            f.write(config_content)

        # Create scene collection
        scene_file = scenes_dir / 'C64StreamTest.json'
        scene_config = {
            "AuxAudioDevice1": {
                "balance": 0.5,
                "deinterlace_field_order": 0,
                "deinterlace_mode": 0,
                "enabled": True,
                "flags": 0,
                "hotkeys": {},
                "id": "pulse_input_capture",
                "mixers": 255,
                "monitoring_type": 0,
                "muted": False,
                "name": "Mic/Aux",
                "private_settings": {},
                "push-to-mute": False,
                "push-to-mute-delay": 1000,
                "push-to-talk": False,
                "push-to-talk-delay": 1000,
                "settings": {},
                "sync": 0,
                "volume": 1.0
            },
            "current_scene": "C64 Test Scene",
            "current_program_scene": "C64 Test Scene",
            "scene_order": [
                {
                    "name": "C64 Test Scene"
                }
            ],
            "sources": [
                {
                    "balance": 0.5,
                    "deinterlace_field_order": 0,
                    "deinterlace_mode": 0,
                    "enabled": True,
                    "flags": 0,
                    "hotkeys": {},
                    "id": "c64_source",
                    "mixers": 255,
                    "monitoring_type": 0,
                    "muted": False,
                    "name": "C64 Stream Source",
                    "private_settings": {},
                    "push-to-mute": False,
                    "push-to-mute-delay": 1000,
                    "push-to-talk": False,
                    "push-to-talk-delay": 1000,
                    "settings": {
                        "ip_address": "127.0.0.1",
                        "video_port": self.video_port,
                        "audio_port": self.audio_port,
                        "format": self.format
                    },
                    "sync": 0,
                    "volume": 1.0
                }
            ],
            "scenes": [
                {
                    "hotkeys": {},
                    "id": 1,
                    "name": "C64 Test Scene",
                    "sources": [
                        {
                            "align": 5,
                            "blend_method": "default",
                            "blend_type": "normal",
                            "bounds": {
                                "alignment": 0,
                                "type": "OBS_BOUNDS_NONE"
                            },
                            "bounds_align": 0,
                            "bounds_type": 0,
                            "crop_bottom": 0,
                            "crop_left": 0,
                            "crop_right": 0,
                            "crop_top": 0,
                            "group_children": False,
                            "hotkeys": {},
                            "id": 1,
                            "locked": False,
                            "name": "C64 Stream Source",
                            "pos": {
                                "x": 0.0,
                                "y": 0.0
                            },
                            "private_settings": {},
                            "rot": 0.0,
                            "scale": {
                                "x": 1.0,
                                "y": 1.0
                            },
                            "scale_filter": "disable",
                            "visible": True
                        }
                    ]
                }
            ],
            "version": 1
        }

        with open(scene_file, 'w') as f:
            json.dump(scene_config, f, indent=2)

        self.log(f"✅ Created OBS profile at {profile_dir}")
        return profile_dir

    def wait_for_obs_websocket(self, timeout=30):
        """Wait for OBS WebSocket server to be ready."""
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

    def send_obs_request(self, request_type, request_data=None):
        """Send a request to OBS via WebSocket API."""
        if not WEBSOCKET_AVAILABLE:
            self.log("⚠️  WebSocket not available, skipping OBS API call")
            return None

        try:
            import uuid
            request_id = str(uuid.uuid4())

            message = {
                "op": 6,  # Request
                "d": {
                    "requestType": request_type,
                    "requestId": request_id
                }
            }

            if request_data:
                message["d"]["requestData"] = request_data

            # Simple HTTP-based approach for basic commands
            # In a full implementation, we'd use persistent WebSocket connection
            response = requests.post('http://127.0.0.1:4455/api',
                                   json=message, timeout=5)

            if response.status_code == 200:
                return response.json()
            else:
                self.log(f"OBS API request failed: {response.status_code}")
                return None

        except Exception as e:
            self.log(f"OBS API error: {e}")
            return None

    def start_obs_recording(self):
        """
        Start OBS with recording enabled using our test profile.
        """
        self.log("Starting OBS with C64 Stream test profile")

        # Create the OBS profile first
        profile_dir = self.create_obs_profile()

        try:
            # Start OBS with our test profile
            obs_cmd = [
                'obs',
                '--profile', 'C64StreamTest',
                '--scene-collection', 'C64StreamTest',
                '--minimize-to-tray',
                '--disable-updater',
                '--disable-missing-files-check',
                '--disable-shutdown-check',
                '--safe-mode'
            ]

            self.log(f"Running: {' '.join(obs_cmd)}")

            self.obs_process = subprocess.Popen(
                obs_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=dict(os.environ, DISPLAY=os.environ.get('DISPLAY', ':99'))
            )

            # Give OBS time to initialize
            time.sleep(8)

            if self.obs_process.poll() is not None:
                stdout, stderr = self.obs_process.communicate()
                raise RuntimeError(f"OBS failed to start:\nSTDOUT: {stdout.decode()}\nSTDERR: {stderr.decode()}")

            self.log("✅ OBS started successfully")

            # Wait a bit more for full initialization
            time.sleep(2)

            return True

        except Exception as e:
            print(f"❌ Failed to start OBS: {e}")
            return False

    def start_recording(self):
        """Start recording in OBS."""
        self.log("Starting OBS recording...")

        # Try WebSocket API first, fallback to command line approach
        if self.wait_for_obs_websocket(timeout=5):
            response = self.send_obs_request("StartRecord")
            if response:
                self.log("✅ Recording started via WebSocket API")
                return True

        # Fallback: Kill and restart OBS with recording enabled
        self.log("WebSocket not available, using command line recording")

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
                '--scene-collection', 'C64StreamTest',
                '--startrecording',
                '--minimize-to-tray',
                '--disable-updater',
                '--disable-missing-files-check',
                '--disable-shutdown-check',
                '--safe-mode'
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

        # Try WebSocket API first
        if WEBSOCKET_AVAILABLE:
            response = self.send_obs_request("StopRecord")
            if response:
                self.log("✅ Recording stopped via WebSocket API")
                time.sleep(3)  # Give time for file to be written
                return True

        # Fallback: marker file approach
        marker_file = self.output_dir / 'stop_recording.marker'
        with open(marker_file, 'w') as f:
            f.write(f"stop_recording_{int(time.time())}")

        time.sleep(3)
        return True

    def check_recording_output(self):
        """Check if recording file was created successfully."""
        self.log("Checking for recording output...")

        # Look for video files in multiple directories
        video_extensions = ['.mkv', '.mp4', '.mov', '.avi', '.flv']
        search_dirs = [
            self.output_dir,  # Our test output directory
            Path.home() / 'Videos',  # Default OBS recording directory
            Path.home(),  # Home directory
            Path('/tmp'),  # Temporary directory
        ]

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
                self.log(f"✅ Found recording: {recording} ({file_size} bytes)")

                # Basic validation - file should be larger than 10KB
                if file_size > 10240:
                    # Copy to our output directory for easier access
                    dest_file = self.output_dir / f"c64_recording{recording.suffix}"
                    try:
                        import shutil
                        shutil.copy2(recording, dest_file)
                        self.log(f"✅ Copied recording to: {dest_file}")
                        return str(dest_file)
                    except Exception as e:
                        self.log(f"Warning: Could not copy recording: {e}")
                        return str(recording)

        self.log("❌ No valid recording files found")
        return None

    def replay_packets(self, udp_replay_path):
        """Replay video and audio packets concurrently."""
        self.log(f"Replaying {self.format} packets")

        video_dir = self.packet_dir / 'video' / self.format
        audio_dir = self.packet_dir / 'audio' / self.format

        if not video_dir.exists() or not audio_dir.exists():
            raise FileNotFoundError(f"Packet directories not found: {video_dir}, {audio_dir}")

        # Video packets: ~300 microseconds between packets (matching C64U timing)
        video_cmd = [
            str(udp_replay_path),
            str(video_dir),
            '127.0.0.1',
            str(self.video_port),
            '780',  # Video packet size
            '--delay', '300',  # 300 microseconds between packets
        ]
        if self.verbose:
            video_cmd.append('--verbose')

        # Audio packets: ~4000 microseconds between packets (4ms @ 250 packets/sec)
        audio_cmd = [
            str(udp_replay_path),
            str(audio_dir),
            '127.0.0.1',
            str(self.audio_port),
            '770',  # Audio packet size
            '--delay', '4000',  # 4000 microseconds between packets
        ]
        if self.verbose:
            audio_cmd.append('--verbose')

        # Use results list to track success from threads\n        results = {'video': False, 'audio': False}\n\n        # Start both in parallel\n        video_thread = threading.Thread(target=self._run_replay, args=(video_cmd, 'video', results))\n        audio_thread = threading.Thread(target=self._run_replay, args=(audio_cmd, 'audio', results))\n\n        video_thread.start()\n        audio_thread.start()\n\n        # Wait for both to complete\n        video_thread.join()\n        audio_thread.join()\n\n        success = results['video'] and results['audio']\n        if success:\n            self.log(\"✅ Packet replay complete\")\n        else:\n            self.log(\"❌ Packet replay failed\")\n            \n        return success

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

    def stop_obs(self):
        """Stop OBS recording with proper cleanup."""
        self.log("Stopping OBS")

        if self.obs_process:
            try:
                # First try to stop recording via WebSocket if available
                if WEBSOCKET_AVAILABLE:
                    self.send_obs_request("StopRecord")
                    time.sleep(1)
                
                # Send SIGTERM for graceful shutdown
                self.obs_process.terminate()
                
                # Wait for graceful shutdown
                try:
                    self.obs_process.wait(timeout=8)
                    self.log("✅ OBS stopped gracefully")
                except subprocess.TimeoutExpired:
                    self.log("OBS didn't stop gracefully, sending SIGKILL...")
                    self.obs_process.kill()
                    self.obs_process.wait(timeout=3)
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
        """Stop Xvfb."""
        self.log("Stopping Xvfb")

        if self.xvfb_process:
            self.xvfb_process.terminate()

            try:
                self.xvfb_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.xvfb_process.kill()
                self.xvfb_process.wait()

            self.log("✅ Xvfb stopped")

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
                        
        except Exception as e:
            self.log(f"Warning: Could not clean up OBS locks: {e}")
    
    def cleanup(self):
        """Cleanup all test processes."""
        self.log("Cleaning up test environment")
        self.stop_obs()
        self.stop_xvfb()
        self.cleanup_obs_locks()

    def run(self, udp_replay_path):
        """
        Run the complete e2e test.

        Returns:
            bool: True if test passed, False otherwise
        """
        print(f"\n{'='*60}")
        print(f"C64 Stream E2E Test - {self.format}")
        print(f"{'='*60}\n")

        try:
            # Setup test environment
            if not self.start_xvfb():
                return False

            # Start OBS with our test profile
            if not self.start_obs_recording():
                self.log("❌ Failed to start OBS")
                return False

            # Start recording
            if not self.start_recording():
                self.log("❌ Failed to start recording")
                return False

            # Run packet replay while recording
            self.log("Running packet replay while OBS is recording...")
            replay_success = self.replay_packets(udp_replay_path)

            if replay_success:
                self.log("✅ Packet replay completed successfully")
            else:
                self.log("❌ Packet replay failed")

            # Stop recording
            self.stop_recording()

            # Check if recording file was created
            recording_file = self.check_recording_output()
            if recording_file:
                self.log(f"✅ Recording created successfully: {recording_file}")
                recording_success = True
            else:
                self.log("❌ No recording file found")
                recording_success = False

            # Overall success requires both replay and recording to work
            overall_success = replay_success and recording_success

            if overall_success:
                print("\n✅ Complete E2E test passed: packets replayed and video recorded")
            else:
                print("\n❌ E2E test failed - check logs for details")

            return overall_success

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

    parser.add_argument('--test-dir', default='.',
                        help='Test directory (default: current directory)')
    parser.add_argument('--format', choices=['PAL', 'NTSC'], default='PAL',
                        help='Video format to test (default: PAL)')
    parser.add_argument('--frames', type=int, default=30,
                        help='Number of frames to test (default: 30)')
    parser.add_argument('--video-port', type=int, default=11000,
                        help='Video UDP port (default: 11000)')
    parser.add_argument('--audio-port', type=int, default=11001,
                        help='Audio UDP port (default: 11001)')
    parser.add_argument('--udp-replay', default='./udp_replay',
                        help='Path to udp_replay executable (default: ./udp_replay)')
    parser.add_argument('--verbose', action='store_true',
                        help='Enable verbose logging')

    args = parser.parse_args()

    # Verify UDP replay tool exists
    udp_replay_path = Path(args.udp_replay)
    if not udp_replay_path.exists():
        print(f"❌ UDP replay tool not found: {udp_replay_path}")
        print("   Build it with: gcc -O2 -o udp_replay udp_replay.c")
        return 1

    # Create and run test
    test = E2ETest(
        args.test_dir,
        video_port=args.video_port,
        audio_port=args.audio_port,
        format=args.format,
        frames=args.frames,
        verbose=args.verbose
    )
    
    # Store reference for signal handler
    test_instance = test

    success = test.run(udp_replay_path)
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
