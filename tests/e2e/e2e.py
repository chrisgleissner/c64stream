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
import shutil
from pathlib import Path

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


class E2ETest:
    def __init__(self, test_dir, video_port=21000, audio_port=21001, control_port=6400,
                 format='NTSC', frames=30, verbose=False, enable_websocket=False):  # Default to NTSC for faster testing
        self.test_dir = Path(test_dir)
        self.video_port = video_port
        self.audio_port = audio_port
        self.control_port = control_port
        self.format = format
        self.frames = frames
        self.verbose = verbose
        self.enable_websocket = enable_websocket  # Disable WebSocket by default for performance

        # Detect CI environment and set appropriate timeouts
        self.is_ci = self._detect_ci_environment()
        self._configure_timeouts()

        # Process handles
        self.xvfb_process = None
        self.obs_process = None
        self.tcp_server_thread = None
        self.tcp_server_socket = None
        self.tcp_server_running = False
        self.udp_replay_triggered = threading.Event()

        # UDP destination addresses (updated from TCP commands)
        self.video_dest_ip = '127.0.0.1'
        self.video_dest_port = self.video_port
        self.audio_dest_ip = '127.0.0.1'
        self.audio_dest_port = self.audio_port

        # Test artifacts
        self.packet_dir = self.test_dir / 'test_packets'
        self.output_dir = self.test_dir / 'test_output'
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def _detect_ci_environment(self):
        """Detect if running in CI environment."""
        ci_indicators = [
            'CI', 'CONTINUOUS_INTEGRATION', 'GITHUB_ACTIONS',
            'GITLAB_CI', 'JENKINS_URL', 'TRAVIS', 'CIRCLECI',
            'BUILDKITE', 'DRONE', 'TEAMCITY_VERSION'
        ]
        return any(os.environ.get(indicator) for indicator in ci_indicators)

    def _configure_timeouts(self):
        """Configure timeouts based on environment."""
        if self.is_ci:
            # CI environment: longer timeouts for resource-constrained environments
            self.plugin_init_timeout = 45  # Increased from 30s for more robust CI
            self.obs_startup_delay = 4     # Increased from 3s
            self.async_task_delay = 6      # Increased from 5s
            self.websocket_settings_delay = 3  # Increased from 2s
            self.udp_socket_delay = 1.0    # Increased from 0.5s
            self.buffer_setup_delay = 0.5  # Increased from 0.2s
            self.log("🏗️ CI environment detected - using extended timeouts")
        else:
            # Local environment: ultra-minimal timeouts for 6-second target
            self.plugin_init_timeout = 6
            self.obs_startup_delay = 0.5
            self.async_task_delay = 0.3
            self.websocket_settings_delay = 0.2
            self.udp_socket_delay = 0.05
            self.buffer_setup_delay = 0.05
            self.log("🚀 Local environment detected - using ultra-minimal timeouts")

    def log(self, message):
        """Print log message if verbose mode is enabled."""
        if self.verbose:
            print(f"[TEST] {message}")

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
                    # Only set CI-specific Qt/GL environment variables in CI environment
                    if self.is_ci:
                        os.environ.setdefault('QT_QPA_PLATFORM', 'xcb')
                        os.environ.setdefault('QT_X11_NO_MITSHM', '1')
                        os.environ.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
                        self.log("🏗️ Applied CI-specific Qt/GL environment variables")
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
            # Only set CI-specific Qt/GL environment variables in CI environment
            if self.is_ci:
                os.environ.setdefault('QT_QPA_PLATFORM', 'xcb')
                os.environ.setdefault('QT_X11_NO_MITSHM', '1')
                os.environ.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
                self.log("🏗️ Applied CI-specific Qt/GL environment variables")

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

        # Replace variables in the copied configuration
        self._replace_config_variables(obs_config_dir)

        # Clean up state files that could trigger dialogs
        self._cleanup_obs_state_files(obs_config_dir)

        self.log("✅ OBS configuration copied from baseline")
        return obs_config_dir / 'basic' / 'profiles' / 'C64StreamTest'

    def _replace_config_variables(self, obs_config_dir):
        """Replace variables in OBS configuration files with actual values."""
        # Define variable replacements
        variables = {
            '$OUTPUT_DIR': str(self.output_dir)
        }

        # Process basic.ini profile file
        basic_ini = obs_config_dir / 'basic' / 'profiles' / 'C64StreamTest' / 'basic.ini'
        if basic_ini.exists():
            content = basic_ini.read_text()
            for var, value in variables.items():
                content = content.replace(var, value)
            basic_ini.write_text(content)
            self.log(f"Updated configuration variables in {basic_ini}")

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
            timeout = 20 if self.is_ci else 5

        self.log(f"⏳ Waiting for receiver readiness in OBS logs (timeout: {timeout}s)...")

        obs_config_dir = Path.home() / '.config' / 'obs-studio'
        logs_dir = obs_config_dir / 'logs'

        start_time = time.time()

        # Primary thread-start patterns (debug level)
        primary_video = b"Video receiver thread started on port"
        primary_audio = b"Audio receiver thread started on port"

        # Fallback socket creation patterns (info level)
        fallback_video = f"Created optimized UDP socket on port {self.video_port}".encode()
        fallback_audio = f"Created optimized UDP socket on port {self.audio_port}".encode()

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

            # Receive Hello message with authentication challenge
            hello_msg = json.loads(ws.recv())
            if hello_msg.get("op") != 0:  # Hello opcode
                raise Exception(f"Expected Hello message, got: {hello_msg}")

            # Authenticate using the challenge
            auth_data = hello_msg["d"]["authentication"]
            challenge = auth_data["challenge"]
            salt = auth_data["salt"]

            # Generate authentication response
            secret = base64.b64encode(hashlib.sha256((password + salt).encode()).digest()).decode()
            auth_response = base64.b64encode(hashlib.sha256((secret + challenge).encode()).digest()).decode()

            # Send Identify message with authentication
            identify_msg = {
                "op": 1,  # Identify
                "d": {
                    "rpcVersion": 1,
                    "authentication": auth_response
                }
            }
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
            obs_cmd = [
                'obs',
                '--profile', 'C64StreamTest',
                collection_flag, 'C64StreamTest',
                '--scene', 'C64 Test Scene',
                '--startrecording',
                '--minimize-to-tray',
                '--disable-updater',
                '--disable-missing-files-check',
                '--multi'
            ]

            # Add verbose logging on CI
            if self.is_ci:
                obs_cmd.append('--verbose')
                self.log("🏗️ Added --verbose flag for CI debugging")

            self.log(f"Running: {' '.join(obs_cmd)}")

            env_vars = dict(os.environ, DISPLAY=os.environ.get('DISPLAY', ':99'))
            # Only set CI-specific Qt/GL environment variables in CI environment
            if self.is_ci:
                env_vars.setdefault('QT_QPA_PLATFORM', 'xcb')
                env_vars.setdefault('QT_X11_NO_MITSHM', '1')
                env_vars.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
                self.log("🏗️ Applied CI-specific Qt/GL environment variables to OBS subprocess")

            # Start OBS process
            self.obs_process = subprocess.Popen(
                obs_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env_vars
            )

            # Give OBS time to initialize
            time.sleep(self.obs_startup_delay)

            if self.obs_process.poll() is not None:
                stdout, stderr = self.obs_process.communicate()
                raise RuntimeError(f"OBS failed to start with {collection_flag}:\nSTDOUT: {stdout.decode()}\nSTDERR: {stderr.decode()}")

            self.log(f"✅ OBS started successfully with {collection_flag}")
            return True

        try:
            # Try preferred flag first, then fallback
            # For CI: prefer '--collection' (OBS 32+), fallback to '--scene-collection' (older OBS)
            # For local: prefer '--scene-collection' for wider compatibility, fallback to '--collection'
            if self.is_ci:
                collection_flags = ['--collection', '--scene-collection']
                self.log("🏗️ CI environment: trying --collection first (OBS 32+)")
            else:
                collection_flags = ['--scene-collection', '--collection']
                self.log("🚀 Local environment: trying --scene-collection first (wider compatibility)")

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
                self.log(f"✅ Found recording: {recording} ({file_size} bytes)")

                # Basic validation - file should be larger than 10KB
                if file_size > 10240:
                    # Move to our output directory for easier access (avoid duplicates)
                    dest_file = self.output_dir / f"c64_recording{recording.suffix}"
                    try:
                        import shutil
                        shutil.move(str(recording), str(dest_file))
                        self.log(f"✅ Moved recording to: {dest_file}")
                        return str(dest_file)
                    except Exception as e:
                        self.log(f"Warning: Could not move recording: {e}")
                        return str(recording)

        self.log("❌ No valid recording files found")
        return None

    def check_csv_recordings(self):
        """Check if CSV recordings were created and analyze their content."""
        self.log("🔍 Checking for CSV recordings...")

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
                    self.log(f"📊 Network CSV contains {csv_results['network_packets']} packet entries")

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

        # Copy CSV files to test output for analysis
        try:
            if network_csv.exists():
                import shutil
                dest_network = self.output_dir / 'network.csv'
                shutil.copy2(network_csv, dest_network)
                self.log(f"✅ Copied network.csv to: {dest_network}")

            if obs_csv.exists():
                import shutil
                dest_obs = self.output_dir / 'obs.csv'
                shutil.copy2(obs_csv, dest_obs)
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
            self.log(f"  - Expected TCP connection to: 127.0.0.1:{self.control_port}")
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
                        if f':{self.control_port}' in line or f'127.0.0.1:{self.control_port}' in line:
                            self.log(f"    {line}")
            except Exception as e:
                self.log(f"  - Could not check netstat: {e}")

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

        return self._replay_interleaved_packets()

    def _replay_interleaved_packets(self):
        """Replay packets with proper interleaving and precise timing."""
        import socket
        import time
        import glob
        import os

        video_dir = self.packet_dir / 'video' / self.format
        audio_dir = self.packet_dir / 'audio' / self.format

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
        if self.format == 'PAL':
            video_interval_us = 293  # PAL: 0.293 ms = 293 μs between video packets
            audio_interval_us = 4000  # PAL: 4.000 ms = 4000 μs between audio packets
        else:  # NTSC
            video_interval_us = 279  # NTSC: 0.279 ms = 279 μs between video packets
            audio_interval_us = 4004  # NTSC: 4.004 ms = 4004 μs between audio packets

        # Create UDP sockets
        video_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        audio_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # Skip test packets to avoid interference with real packet reception
        # The plugin might be processing test packets when real packets arrive
        self.log(f"🔍 UDP sockets ready for {self.video_dest_ip}:{self.video_dest_port} and {self.audio_dest_ip}:{self.audio_dest_port}")
        self.log(f"  - Video socket: {video_sock}")
        self.log(f"  - Audio socket: {audio_sock}")

        # Test UDP connectivity
        try:
            test_data = b"test"
            video_sock.sendto(test_data, (self.video_dest_ip, self.video_dest_port))
            audio_sock.sendto(test_data, (self.audio_dest_ip, self.audio_dest_port))
            self.log(f"  - Test packets sent successfully")
        except Exception as e:
            self.log(f"  - Failed to send test packets: {e}")

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

            self.log(f"🎯 Generated {len(timeline)} interleaved packets over {timeline[-1]['time_us']/1000:.1f}ms")

            # Send packets with precise timing and better error handling
            replay_start_time = time.time()
            packets_sent = 0
            failed_packets = 0

            for event in timeline:
                # Calculate when this packet should be sent
                target_time = replay_start_time + event['time_us'] / 1_000_000.0

                # Wait until the precise moment
                current_time = time.time()
                if current_time < target_time:
                    time.sleep(target_time - current_time)

                # Read and send packet
                try:
                    with open(event['file'], 'rb') as f:
                        packet_data = f.read()

                    if len(packet_data) == 0:
                        self.log(f"❌ Empty packet file: {event['file']}")
                        failed_packets += 1
                        continue

                    bytes_sent = event['sock'].sendto(packet_data, event['dest'])
                    packets_sent += 1

                    # Log first few packets for debugging
                    if packets_sent <= 3:
                        self.log(f"📤 Sent {event['type']} packet #{packets_sent}: {len(packet_data)} bytes to {event['dest']}")

                    # Verify socket operation was successful
                    if bytes_sent != len(packet_data):
                        self.log(f"⚠️ Partial send: {bytes_sent}/{len(packet_data)} bytes for {event['type']} packet #{packets_sent}")

                    # Debug first few packets to verify sending
                    if packets_sent <= 5:
                        self.log(f"🔍 DEBUG: Sent {event['type']} packet #{packets_sent}: {len(packet_data)} bytes to {event['dest']}, socket returned {bytes_sent}")
                        # Add small delay after first few packets to ensure plugin processes them
                        if packets_sent <= 3:
                            time.sleep(0.001)  # 1ms delay

                    # Show progress every 500 packets (always visible)
                    if packets_sent % 500 == 0:
                        elapsed_ms = (time.time() - replay_start_time) * 1000
                        print(f"📡 Sent {packets_sent}/{len(timeline)} packets ({elapsed_ms:.1f}ms elapsed)")

                except Exception as e:
                    self.log(f"❌ Failed to send {event['type']} packet {event['file']}: {e}")
                    failed_packets += 1
                    continue

            elapsed_ms = (time.time() - replay_start_time) * 1000
            self.log(f"✅ Packet replay complete: {packets_sent} packets sent, {failed_packets} failed in {elapsed_ms:.1f}ms")

            # Give plugin time to process the packets (increased from 1.0s to prevent packet loss)
            time.sleep(1.5)
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
            self.log(f"  - Binding to 127.0.0.1:{self.control_port}")
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

            self.tcp_server_socket.bind(('127.0.0.1', self.control_port))
            self.tcp_server_socket.listen(5)
            self.tcp_server_running = True

            # Verify binding worked
            actual_addr = self.tcp_server_socket.getsockname()
            self.log(f"  - Successfully bound to {actual_addr}")

            self.tcp_server_thread = threading.Thread(target=self._tcp_server_worker)
            self.tcp_server_thread.daemon = True
            self.tcp_server_thread.start()

            self.log("✅ Mock C64 Ultimate TCP server started")
            return True

        except Exception as e:
            self.log(f"❌ Failed to start mock C64 Ultimate TCP server: {e}")
            self.log(f"  - Error type: {type(e).__name__}")
            self.log(f"  - Error details: {str(e)}")
            return False

    def _tcp_server_worker(self):
        """TCP server worker thread - handles incoming connections."""
        self.log("TCP server worker started, waiting for connections...")

        while self.tcp_server_running:
            try:
                self.tcp_server_socket.settimeout(1.0)  # Non-blocking accept
                conn, addr = self.tcp_server_socket.accept()
                self.log(f"TCP connection received from {addr}")

                # Handle the connection in a separate thread
                conn_thread = threading.Thread(target=self._handle_tcp_connection, args=(conn, addr))
                conn_thread.daemon = True
                conn_thread.start()

            except socket.timeout:
                continue  # Check if we should still be running
            except Exception as e:
                if self.tcp_server_running:
                    self.log(f"TCP server error: {e}")
                break

        self.log("TCP server worker stopped")

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
                                # For E2E testing, force localhost destination regardless of requested IP
                                if ':' in dest_str:
                                    dest_ip, dest_port_str = dest_str.split(':', 1)
                                    try:
                                        dest_port = int(dest_port_str)
                                        # Force localhost for E2E testing to avoid network routing issues
                                        force_dest_ip = "127.0.0.1"
                                        if stream_id == 0:  # Video
                                            self.video_dest_ip = force_dest_ip
                                            self.video_dest_port = dest_port
                                            self.log(f"Updated video destination: {force_dest_ip}:{dest_port} (forced localhost)")
                                        elif stream_id == 1:  # Audio
                                            self.audio_dest_ip = force_dest_ip
                                            self.audio_dest_port = dest_port
                                            self.log(f"Updated audio destination: {force_dest_ip}:{dest_port} (forced localhost)")
                                    except ValueError:
                                        self.log(f"Invalid port in destination: {dest_str}")

                        # Signal that we should start UDP packet replay
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

        if self.tcp_server_thread:
            self.tcp_server_thread.join(timeout=2)
            self.tcp_server_thread = None

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

        # Track individual validation results
        validation_results = {
            'udp_reception': {'status': 'unknown', 'details': ''},
            'frame_processing': {'status': 'unknown', 'details': ''},
            'video_recording': {'status': 'unknown', 'details': ''},
            'packet_integrity': {'status': 'unknown', 'details': ''}
        }        # Calculate expected packet counts using actual generation logic
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
        network_csv = self.output_dir / 'network.csv'
        if network_csv.exists():
            try:
                with open(network_csv, 'r') as f:
                    lines = f.readlines()
                    received_packets = len(lines) - 1  # Subtract header

                video_packets = sum(1 for line in lines[1:] if line.startswith('video,'))
                audio_packets = sum(1 for line in lines[1:] if line.startswith('audio,'))

                if received_packets == expected_total_packets:
                    print(f"✅ UDP Reception: {received_packets}/{expected_total_packets} packets ({video_packets} video, {audio_packets} audio)")
                    validation_results['udp_reception'] = {'status': 'pass', 'details': f"{received_packets}/{expected_total_packets} packets"}
                elif received_packets >= expected_total_packets * 0.95:  # 95% threshold
                    print(f"⚠️  UDP Reception: {received_packets}/{expected_total_packets} packets ({video_packets} video, {audio_packets} audio)")
                    validation_warnings.append(f"Packet loss: {expected_total_packets - received_packets} packets missing")
                    validation_results['udp_reception'] = {'status': 'warning', 'details': f"{received_packets}/{expected_total_packets} packets (minor loss)"}
                else:
                    print(f"❌ UDP Reception: {received_packets}/{expected_total_packets} packets ({video_packets} video, {audio_packets} audio)")
                    validation_errors.append(f"Significant packet loss: {expected_total_packets - received_packets} packets missing")
                    validation_results['udp_reception'] = {'status': 'fail', 'details': f"{received_packets}/{expected_total_packets} packets (major loss)"}

            except Exception as e:
                print(f"❌ UDP Reception: Failed to validate network.csv - {e}")
                validation_errors.append(f"Network CSV validation failed: {e}")
                validation_results['udp_reception'] = {'status': 'fail', 'details': 'CSV validation error'}
        else:
            print("❌ UDP Reception: No network.csv found")
            validation_errors.append("Missing network.csv - plugin may not be receiving UDP packets")
            validation_results['udp_reception'] = {'status': 'fail', 'details': 'No CSV file found'}

        # 2. Frame Processing Validation
        obs_csv = self.output_dir / 'obs.csv'
        if obs_csv.exists():
            try:
                with open(obs_csv, 'r') as f:
                    lines = f.readlines()
                    processed_frames = len(lines) - 1  # Subtract header

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
        else:
            print("❌ Frame Processing: No obs.csv found")
            validation_errors.append("Missing obs.csv - plugin may not be processing frames")
            validation_results['frame_processing'] = {'status': 'fail', 'details': 'No CSV file found'}        # 3. Video Recording Validation
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

        # 4. A/V Synchronization Validation
        # A/V sync is a critical component of the streaming functionality
        av_validation = True
        if recording_file and Path(recording_file).exists() and verify_av_sync:
            try:
                print("🎵 A/V Sync: Analyzing synchronization...")
                sync_results = verify_av_sync(recording_file, tolerance_ms=150)

                if sync_results['is_perfectly_synced']:
                    print(f"✅ A/V Sync: Perfect synchronization ({sync_results['sync_accuracy_percent']:.1f}%)")
                    validation_results['av_sync'] = {'status': 'pass', 'details': f"{sync_results['perfect_sync_count']}/{sync_results['total_analyzed']} analyzed beeps synced"}
                elif sync_results['sync_accuracy_percent'] >= 60.0:  # 60% threshold for pass
                    print(f"✅ A/V Sync: Good synchronization ({sync_results['sync_accuracy_percent']:.1f}%)")
                    validation_results['av_sync'] = {'status': 'pass', 'details': f"{sync_results['perfect_sync_count']}/{sync_results['total_analyzed']} analyzed beeps synced"}
                else:
                    print(f"❌ A/V Sync: Poor synchronization ({sync_results['sync_accuracy_percent']:.1f}%)")
                    validation_errors.append(f"A/V sync accuracy too low: {sync_results['sync_accuracy_percent']:.1f}% (minimum 60% required)")
                    validation_results['av_sync'] = {'status': 'fail', 'details': f"Only {sync_results['perfect_sync_count']}/{sync_results['total_analyzed']} beeps synced"}
                    av_validation = False
            except Exception as e:
                print(f"❌ A/V Sync: Analysis failed - {e}")
                validation_errors.append(f"A/V sync analysis error: {e}")
                validation_results['av_sync'] = {'status': 'fail', 'details': 'Analysis failed'}
                av_validation = False
        else:
            if not verify_av_sync:
                print("❌ A/V Sync: Analysis not available (missing dependencies)")
                validation_errors.append("A/V sync analysis unavailable")
                validation_results['av_sync'] = {'status': 'fail', 'details': 'Analysis unavailable'}
                av_validation = False

        # Summary
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

        print(f"{'='*60}\n")
        return overall_success, validation_results

    def cleanup(self):
        """Cleanup all test processes."""
        self.log("Cleaning up test environment")
        self.stop_mock_c64_server()
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

            # Start OBS - plugin will auto-connect to TCP server on initialization
            if not self.start_obs_recording():
                self.log("❌ Failed to start OBS")
                return False

            # Wait for plugin to connect to TCP server via async task
            self.log(f"⏳ Allowing plugin to connect to mock server via async task ({self.async_task_delay}s)...")
            self.log("  - Plugin should auto-connect when source is created")
            self.log("  - Async retry task should call c64_start_streaming()")
            time.sleep(self.async_task_delay)  # Environment-optimized async task delay

            # If plugin didn't trigger replay yet, nudge OBS by reopening scene collection
            if not self.udp_replay_triggered.is_set():
                try:
                    self.log("🔧 Nudge: reopen collection to force source initialization on CI")
                    env_vars = dict(os.environ)
                    # Only set CI-specific Qt/GL environment variables in CI environment
                    if self.is_ci:
                        env_vars.setdefault('QT_QPA_PLATFORM', 'xcb')
                        env_vars.setdefault('QT_X11_NO_MITSHM', '1')
                        env_vars.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
                    subprocess.run([
                        'obs', '--profile', 'C64StreamTest', '--collection', 'C64StreamTest', '--minimize-to-tray', '--disable-updater', '--disable-missing-files-check', '--multi', '--startrecording'
                    ], env=env_vars, timeout=3)
                except Exception:
                    pass

            # Optional WebSocket connection attempt (disabled by default for performance)
            if self.enable_websocket:
                self.log("🔧 Attempting to manually trigger plugin connection via WebSocket...")
                try:
                    # Wait for OBS WebSocket to be ready
                    if self.wait_for_obs_websocket(timeout=5):  # Reduced timeout
                        # Update the C64 Stream source settings to trigger connection
                        source_settings = {
                            "c64_host": "localhost",
                            "video_port": self.video_port,
                            "audio_port": self.audio_port,
                            "control_port": self.control_port,
                            "record_csv": True
                        }

                        response = self.send_obs_request("SetSourceSettings", {
                            "sourceName": "C64 Stream",
                            "sourceSettings": source_settings
                        })

                        if response:
                            self.log("  - ✅ Updated source settings via WebSocket")
                            time.sleep(self.websocket_settings_delay)
                        else:
                            self.log("  - ❌ Failed to update source settings")
                    else:
                        self.log("  - ❌ OBS WebSocket not available")
                except Exception as e:
                    self.log(f"  - ❌ WebSocket error: {e}")
            else:
                self.log("⚡ Skipping WebSocket checks for optimal performance")

            # OBS is already recording (started with --startrecording flag)
            self.log("✅ OBS recording already active")

            # Run packet replay while recording
            self.log("Running packet replay while OBS is recording...")
            replay_success = self.replay_packets(udp_replay_path)

            if replay_success:
                self.log("✅ Packet replay completed successfully")
            else:
                self.log("❌ Packet replay failed")

            # Stop recording
            self.stop_recording()

            # Wait a moment for files to be written (increased from 2s to prevent data loss)
            time.sleep(3)

            # Check CSV recordings first (crucial for debugging packet reception)
            csv_found = self.check_csv_recordings()
            if csv_found:
                self.log("✅ CSV recordings found and analyzed")
                csv_success = True
            else:
                self.log("⚠️ No CSV recordings found - may indicate packet reception issues")
                csv_success = False

            # Check if recording file was created
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

    args = parser.parse_args()

    # Verify UDP replay tool exists, build if needed
    udp_replay_path = Path(args.udp_replay)
    if not udp_replay_path.exists():
        print(f"⚠️  UDP replay tool not found: {udp_replay_path}")
        print("🔨 Building UDP replay tool...")

        # Find the udp_replay.c source file
        script_dir = Path(__file__).parent
        udp_replay_src = script_dir / "udp_replay.c"

        if not udp_replay_src.exists():
            print(f"❌ UDP replay source not found: {udp_replay_src}")
            return 1

        # Build the tool
        build_cmd = ["gcc", "-O2", "-o", str(udp_replay_path), str(udp_replay_src)]
        try:
            result = subprocess.run(build_cmd, check=True, capture_output=True, text=True)
            print(f"✅ Successfully built UDP replay tool: {udp_replay_path}")
        except subprocess.CalledProcessError as e:
            print(f"❌ Failed to build UDP replay tool:")
            print(f"   Command: {' '.join(build_cmd)}")
            print(f"   Error: {e.stderr}")
            return 1
        except FileNotFoundError:
            print("❌ gcc compiler not found. Install build-essential package.")
            return 1

    # Create and run test
    test = E2ETest(
        args.test_dir,
        video_port=args.video_port,
        audio_port=args.audio_port,
        control_port=args.control_port,
        format=args.format,
        frames=args.frames,
        verbose=args.verbose,
        enable_websocket=args.enable_websocket
    )

    # Store reference for signal handler
    test_instance = test

    success = test.run(udp_replay_path)
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
