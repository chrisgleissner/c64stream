#!/usr/bin/env python3
"""
C64 Stream - Scanline Evenness E2E Test
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

This test validates that scanlines render with even spacing regardless of
how OBS scales the source to the canvas.

Workflow:
1. Build and install the plugin
2. Start Xvfb (virtual display)
3. Start OBS with scanline-enabled preset
4. Replay test packets to generate video
5. Take screenshot via OBS WebSocket
6. Analyze screenshot for scanline evenness
7. Report pass/fail with measurements
"""

import os
import sys
import subprocess
import time
import json
import base64
import hashlib
import signal
import argparse
import shutil
import uuid
from pathlib import Path

try:
    import websocket
    WEBSOCKET_AVAILABLE = True
except ImportError:
    WEBSOCKET_AVAILABLE = False

try:
    from PIL import Image
    import numpy as np
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False


class ScanlineTest:
    def __init__(self, test_dir, verbose=False, skip_build=False, keep_obs=False):
        self.test_dir = Path(test_dir)
        self.project_root = self.test_dir.parent.parent
        self.build_dir = self.project_root / 'build_x86_64'
        self.verbose = verbose
        self.skip_build = skip_build
        self.keep_obs = keep_obs

        self.output_dir = self.test_dir / 'scanline_output'
        self.output_dir.mkdir(parents=True, exist_ok=True)

        self.xvfb_process = None
        self.obs_process = None
        self.tcp_server_thread = None
        self.tcp_server_socket = None
        self.tcp_server_running = False
        self.streaming_triggered = None  # Will be set in start_tcp_server
        self.display = ':99'
        self.obs_ws_port = 4455

        # Test parameters
        self.video_port = 21000
        self.audio_port = 21001
        self.control_port = 6400

    def log(self, message):
        if self.verbose:
            print(f"[SCANLINE] {message}")

    def log_always(self, message):
        print(f"[SCANLINE] {message}")

    def build_and_install(self):
        """Build and install the plugin."""
        if self.skip_build:
            self.log("Skipping build (--skip-build)")
            return True

        self.log("Building plugin...")
        result = subprocess.run(
            ['cmake', '--build', str(self.build_dir)],
            cwd=self.project_root,
            capture_output=True,
            text=True
        )
        if result.returncode != 0:
            self.log_always(f"Build failed: {result.stderr}")
            return False

        self.log("Installing plugin...")
        home = Path.home()
        plugin_dir = home / '.config/obs-studio/plugins/c64stream'
        (plugin_dir / 'bin/64bit').mkdir(parents=True, exist_ok=True)
        (plugin_dir / 'data').mkdir(parents=True, exist_ok=True)

        shutil.copy(self.build_dir / 'c64stream.so', plugin_dir / 'bin/64bit/')
        for item in (self.project_root / 'data').iterdir():
            dest = plugin_dir / 'data' / item.name
            if item.is_dir():
                if dest.exists():
                    shutil.rmtree(dest)
                shutil.copytree(item, dest)
            else:
                shutil.copy(item, dest)

        self.log("Plugin installed.")
        return True

    def apply_scenario_overrides(self):
        """Apply scanline scenario overrides."""
        scenario_dir = self.test_dir / 'scenarios/scanlines/overrides'
        if not scenario_dir.exists():
            self.log(f"Scenario overrides not found: {scenario_dir}")
            return

        obs_config_dir = Path.home() / '.config/obs-studio'
        self.log(f"Applying scanline scenario overrides...")

        for src in scenario_dir.rglob('*'):
            if src.is_dir():
                continue
            rel = src.relative_to(scenario_dir)
            dst = obs_config_dir / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            self.log(f"  Copied: {rel}")

    def start_xvfb(self):
        """Start Xvfb virtual framebuffer."""
        self.log(f"Starting Xvfb on display {self.display}")

        display_num = self.display.lstrip(':')
        lock_file = f"/tmp/.X{display_num}-lock"
        try:
            if os.path.exists(lock_file):
                os.remove(lock_file)
        except OSError:
            pass

        try:
            subprocess.run(['pkill', '-f', f'Xvfb.*{self.display}'],
                           capture_output=True, check=False)
            time.sleep(0.5)
        except Exception:
            pass

        self.xvfb_process = subprocess.Popen(
            ['Xvfb', self.display, '-screen', '0', '1920x1080x24'],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL
        )
        os.environ['DISPLAY'] = self.display
        time.sleep(1)

        if self.xvfb_process.poll() is not None:
            self.log_always("Failed to start Xvfb")
            return False

        self.log("Xvfb started successfully")
        return True

    def start_tcp_server(self):
        """Start mock TCP server for C64 control commands."""
        import socket
        import threading

        self.tcp_server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp_server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp_server_socket.bind(('127.0.0.1', self.control_port))
        self.tcp_server_socket.listen(1)
        self.tcp_server_socket.settimeout(1.0)
        self.tcp_server_running = True
        self.streaming_triggered = threading.Event()

        def handle_connection(conn, addr):
            try:
                conn.settimeout(5.0)
                data = conn.recv(1024)
                if len(data) >= 4:
                    self.log(f"TCP received: {data.hex()[:50]}...")
                    cmd_byte = data[0]
                    if data[1] == 0xFF:
                        stream_id = cmd_byte & 0x0F
                        is_start = (cmd_byte & 0xF0) == 0x20
                        if is_start:
                            self.log(f"Received START command for stream {stream_id}")
                            # Extract and parse destination
                            if len(data) > 6:
                                param_len = data[2]
                                if param_len > 2 and len(data) >= 6 + param_len - 2:
                                    dest_str = data[6:6+param_len-2].decode('ascii', errors='ignore')
                                    self.log(f"Stream destination: {dest_str}")
                                    if ':' in dest_str:
                                        _, port_str = dest_str.split(':', 1)
                                        try:
                                            port = int(port_str)
                                            if stream_id == 0:
                                                self.video_port = port
                                            elif stream_id == 1:
                                                self.audio_port = port
                                        except ValueError:
                                            pass
                            self.streaming_triggered.set()
                conn.close()
            except Exception as e:
                self.log(f"TCP connection error: {e}")

        def server_loop():
            while self.tcp_server_running:
                try:
                    conn, addr = self.tcp_server_socket.accept()
                    threading.Thread(target=handle_connection, args=(conn, addr), daemon=True).start()
                except socket.timeout:
                    continue
                except Exception:
                    break

        self.tcp_server_thread = threading.Thread(target=server_loop, daemon=True)
        self.tcp_server_thread.start()
        self.log("TCP mock server started")

    def create_obs_profile(self):
        """Create OBS profile with WebSocket enabled and scanline test scene."""
        self.log("Creating OBS test profile for scanline test")

        obs_config_dir = Path.home() / '.config/obs-studio'
        profile_dir = obs_config_dir / 'basic/profiles/C64StreamTest'
        scenes_dir = obs_config_dir / 'basic/scenes'

        profile_dir.mkdir(parents=True, exist_ok=True)
        scenes_dir.mkdir(parents=True, exist_ok=True)

        # Global config
        global_ini = obs_config_dir / 'global.ini'
        with open(global_ini, 'w') as f:
            f.write("""[General]
EnableCrashReporting=false
EnableUpdater=false
FirstRun=false
SafeMode=false
DisableSafeMode=true
""")

        # Enable WebSocket server
        ws_cfg_dir = obs_config_dir / 'plugin_config/obs-websocket'
        ws_cfg_dir.mkdir(parents=True, exist_ok=True)
        ws_cfg = ws_cfg_dir / 'config.json'
        with open(ws_cfg, 'w') as f:
            json.dump({
                "alerts_enabled": False,
                "auth_required": False,
                "first_load": False,
                "server_enabled": True,
                "server_password": "",
                "server_port": self.obs_ws_port,
            }, f, indent=2)

        # Profile basic.ini (PAL 50fps)
        basic_ini = profile_dir / 'basic.ini'
        with open(basic_ini, 'w') as f:
            f.write(f"""[General]
Name=C64StreamTest
EnableCrashRecovery=false

[Video]
BaseCX=1920
BaseCY=1080
OutputCX=1920
OutputCY=1080
FPSType=0
FPSCommon=50 PAL
FPSInt=50
FPSNum=50
FPSDen=1
ScaleType=bicubic
ColorFormat=NV12
ColorSpace=709
ColorRange=Partial

[Audio]
SampleRate=48000
ChannelSetup=Stereo

[Output]
Mode=Simple

[SimpleOutput]
FilePath={self.output_dir}
RecFormat2=mp4
VBitrate=6000
ABitrate=160
UseAdvanced=false
""")

        # Create scene collection with scanline settings
        scene_uuid = str(uuid.uuid4())
        source_uuid = str(uuid.uuid4())
        canvas_uuid = "6c69626f-6273-4c00-9d88-c5136d61696e"

        # PAL: 384x272
        # Use integer 4x scaling for perfect scanlines with full canvas fill
        # 272 × 4 = 1088 height → 8 pixels taller than 1080 (slight crop of C64 border)
        # This gives pixel-perfect scanlines with 0% variance
        source_width = 384.0
        source_height = 272.0
        scale_factor = 4.0  # Integer 4x scaling (Wide scanlines)
        scaled_width = source_width * scale_factor   # 1536
        scaled_height = source_height * scale_factor  # 1088
        # Center on canvas - content will be cropped by 4px top and 4px bottom
        pos_x = (1920.0 - scaled_width) / 2.0   # 192
        pos_y = (1080.0 - scaled_height) / 2.0  # -4 (crops 4px top)

        scene_config = {
            "current_scene": "C64 Test Scene",
            "current_program_scene": "C64 Test Scene",
            "scene_order": [{"name": "C64 Test Scene"}],
            "name": "C64StreamTest",
            "sources": [
                {
                    "prev_ver": 536870914,
                    "name": "C64 Test Scene",
                    "uuid": scene_uuid,
                    "id": "scene",
                    "versioned_id": "scene",
                    "settings": {
                        "id_counter": 1,
                        "custom_size": False,
                        "items": [{
                            "name": "C64 Stream",
                            "source_uuid": source_uuid,
                            "visible": True,
                            "locked": False,
                            "rot": 0.0,
                            "scale_ref": {"x": 1920.0, "y": 1080.0},
                            "align": 5,
                            # Use bounds_type 2 (Scale to inner bounds) to force exact size
                            "bounds_type": 2,
                            "bounds_align": 0,
                            "bounds_crop": False,
                            "crop_left": 0,
                            "crop_top": 0,
                            "crop_right": 0,
                            "crop_bottom": 0,
                            "id": 1,
                            "group_item_backup": False,
                            "pos": {"x": pos_x, "y": pos_y},
                            "scale": {"x": scale_factor, "y": scale_factor},
                            # Set bounds to exact 3x scaled size
                            "bounds": {"x": scaled_width, "y": scaled_height},
                            "scale_filter": "point",
                            "blend_method": "default",
                            "blend_type": "normal",
                            "show_transition": {"duration": 0},
                            "hide_transition": {"duration": 0},
                            "private_settings": {}
                        }]
                    },
                    "mixers": 255,
                    "sync": 0,
                    "flags": 0,
                    "volume": 1.0,
                    "balance": 0.5,
                    "enabled": True,
                    "muted": False,
                    "push-to-mute": False,
                    "push-to-mute-delay": 0,
                    "push-to-talk": False,
                    "push-to-talk-delay": 0,
                    "hotkeys": {},
                    "deinterlace_mode": 0,
                    "deinterlace_field_order": 0,
                    "monitoring_type": 0,
                    "private_settings": {}
                },
                {
                    "prev_ver": 536870914,
                    "name": "C64 Stream",
                    "uuid": source_uuid,
                    "id": "c64_source",
                    "versioned_id": "c64_source",
                    "settings": {
                        "c64_host": "localhost",
                        "control_port": self.control_port,
                        "video_port": self.video_port,
                        "audio_port": self.audio_port,
                        "obs_ip_address": "127.0.0.1",
                        "auto_detect_ip": False,
                        # Wide scanlines (1.0): 4x scaling → 2 bright + 2 dark pattern
                        # 272 × 4 = 1088 height → fills 1080p with 8px crop (4 top, 4 bottom)
                        "scan_line_distance": 1.0,
                        "scan_line_strength": 0.7,
                        "pixel_width": 1.0,
                        "pixel_height": 1.0,
                        "blur_strength": 0.0,
                        "bloom_strength": 0.0,
                        "afterglow_duration_ms": 0,
                        "tint_mode": 0,
                        "tint_strength": 0.0
                    },
                    "mixers": 255,
                    "sync": 0,
                    "flags": 0,
                    "volume": 1.0,
                    "balance": 0.5,
                    "enabled": True,
                    "muted": False,
                    "push-to-mute": False,
                    "push-to-mute-delay": 0,
                    "push-to-talk": False,
                    "push-to-talk-delay": 0,
                    "hotkeys": {},
                    "deinterlace_mode": 0,
                    "deinterlace_field_order": 0,
                    "monitoring_type": 0,
                    "private_settings": {}
                }
            ],
            "groups": [],
            "quick_transitions": [],
            "transitions": [],
            "saved_projectors": [],
            "saved_preview_projectors": [],
            "saved_multiview_projectors": [],
            "current_transition": "Fade",
            "transition_duration": 300,
            "preview_locked": False,
            "scaling_enabled": False,
            "scaling_level": 0,
            "scaling_off_x": 0.0,
            "scaling_off_y": 0.0,
            "virtual_cam_internal": False,
            "canvases": [{"uuid": canvas_uuid, "name": "", "width": 1920, "height": 1080}]
        }

        scene_file = scenes_dir / 'C64StreamTest.json'
        # Remove backup file to ensure OBS uses our fresh config
        backup_file = scenes_dir / 'C64StreamTest.json.bak'
        if backup_file.exists():
            backup_file.unlink()
        with open(scene_file, 'w') as f:
            json.dump(scene_config, f, indent=2)

        self.log("OBS profile created")

    def start_obs(self):
        """Start OBS with the test configuration."""
        self.log("Starting OBS...")

        # Create profile first
        self.create_obs_profile()

        obs_cmd = [
            'obs',
            '--minimize-to-tray',
            '--collection', 'C64StreamTest',
            '--profile', 'C64StreamTest',
            '--disable-updater',
            '--disable-missing-files-check',
            '--multi'
        ]

        env = dict(os.environ)
        env['DISPLAY'] = self.display
        env.setdefault('QT_QPA_PLATFORM', 'xcb')
        env.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')

        self.obs_process = subprocess.Popen(
            obs_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env
        )

        # Wait for OBS to start
        time.sleep(3)

        if self.obs_process.poll() is not None:
            stdout, stderr = self.obs_process.communicate()
            self.log_always(f"OBS failed: {stderr.decode()[:500]}")
            return False

        # Wait for WebSocket
        self.log("Waiting for OBS WebSocket...")
        for i in range(30):
            time.sleep(1)
            ws = self.connect_obs_websocket()
            if ws:
                ws.close()
                self.log("OBS WebSocket connected")
                return True

        self.log_always("Failed to connect to OBS WebSocket after 30 seconds")
        return False

    def connect_obs_websocket(self):
        """Connect to OBS WebSocket."""
        if not WEBSOCKET_AVAILABLE:
            return None

        try:
            ws = websocket.WebSocket()
            ws.settimeout(2.0)
            ws.connect(f"ws://localhost:{self.obs_ws_port}")

            # Wait for Hello
            hello = json.loads(ws.recv())
            if hello.get('op') != 0:
                return None

            # Send Identify
            identify = {"op": 1, "d": {"rpcVersion": 1}}
            ws.send(json.dumps(identify))

            # Wait for Identified
            identified = json.loads(ws.recv())
            if identified.get('op') != 2:
                return None

            return ws
        except Exception as e:
            self.log(f"WebSocket connection failed: {e}")
            return None

    def generate_and_send_packets(self):
        """Load and send test video packets using pre-generated files."""
        import socket
        import glob

        # Wait for plugin to request streaming
        self.log("Waiting for plugin to request streaming...")
        if not self.streaming_triggered.wait(timeout=15):
            self.log_always("Timeout waiting for plugin to request streaming")
            return False

        # Give more time for UDP sockets to be bound after TCP commands
        time.sleep(1.0)  # Increased delay for socket binding
        time.sleep(0.5)  # Additional buffer time
        self.log(f"Sending packets to 127.0.0.1:{self.video_port}")

        # Generate solid color test packets for scanline analysis
        # Solid color makes scanline gaps clearly visible
        packet_dir = self.output_dir / 'packets' / 'video' / 'PAL'
        if not packet_dir.exists():
            self.log("Generating solid color test packets...")
            generator = (Path(__file__).resolve().parent / 'generate_packets.py')
            result = subprocess.run(
                ['python3', str(generator), '--format', 'PAL', '--frames', '60',
                 '--output', str(self.output_dir / 'packets'), '--pattern', 'solid'],
                cwd=self.test_dir,
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                self.log_always(f"Packet generation failed: {result.stderr}")
                return False

        video_files = sorted(glob.glob(str(packet_dir / "*.bin")))
        if not video_files:
            self.log_always("No video packet files found")
            return False

        self.log(f"Loaded {len(video_files)} video packets")

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # PAL timing: ~0.293ms between packets
        video_interval_us = 293
        import time as time_module
        start_time = time_module.time()

        for i, video_file in enumerate(video_files):
            # Calculate target time for this packet
            target_time = start_time + (i * video_interval_us) / 1_000_000.0

            # Wait until target time
            current = time_module.time()
            if current < target_time:
                time_module.sleep(target_time - current)

            # Read and send packet
            try:
                with open(video_file, 'rb') as f:
                    packet_data = f.read()
                sock.sendto(packet_data, ('127.0.0.1', self.video_port))
            except Exception as e:
                self.log(f"Failed to send packet: {e}")

        sock.close()
        self.log(f"Sent {len(video_files)} video packets")
        time.sleep(1.0)  # Let OBS render
        return True

    def take_screenshot(self, filename):
        """Take screenshot via OBS WebSocket."""
        ws = self.connect_obs_websocket()
        if not ws:
            self.log_always("Failed to connect to OBS WebSocket for screenshot")
            return None

        try:
            request_id = hashlib.md5(str(time.time()).encode()).hexdigest()[:8]

            request = {
                "op": 6,
                "d": {
                    "requestType": "GetSourceScreenshot",
                    "requestId": request_id,
                    "requestData": {
                        # Screenshot the SCENE (not source) to get the scaled result
                        "sourceName": "C64 Test Scene",
                        "imageFormat": "png",
                        "imageWidth": 1920,
                        "imageHeight": 1080
                    }
                }
            }

            ws.send(json.dumps(request))

            for _ in range(50):
                try:
                    response = ws.recv()
                    data = json.loads(response)
                    if data.get('op') == 7:
                        resp_data = data.get('d', {})
                        if resp_data.get('requestId') == request_id:
                            status = resp_data.get('requestStatus', {})
                            if status.get('result'):
                                img_data = resp_data.get('responseData', {}).get('imageData', '')
                                if img_data.startswith('data:image/png;base64,'):
                                    img_data = img_data[22:]

                                output_path = self.output_dir / filename
                                with open(output_path, 'wb') as f:
                                    f.write(base64.b64decode(img_data))
                                self.log(f"Screenshot saved: {output_path}")
                                return output_path
                            else:
                                self.log_always(f"Screenshot failed: {status.get('comment', 'Unknown')}")
                                return None
                except websocket.WebSocketTimeoutException:
                    pass
                time.sleep(0.1)

            self.log_always("Screenshot timeout")
            return None
        finally:
            ws.close()

    def analyze_scanlines(self, image_path):
        """
        Analyze screenshot for:
        1. Scanline spacing evenness (variance < 15%)
        2. Correct number of blank scanlines (one between each C64 pixel row)
        3. Square pixel aspect ratio (within 2% tolerance)
        """
        if not PIL_AVAILABLE:
            self.log_always("PIL/numpy not available - cannot analyze scanlines")
            return None

        self.log(f"Analyzing scanlines in {image_path}")

        img = Image.open(image_path).convert('L')  # Grayscale
        data = np.array(img)

        height, width = data.shape
        self.log(f"Image size: {width}x{height}")

        # Find the content area (non-black region)
        col_sums = np.sum(data, axis=0)
        row_sums = np.sum(data, axis=1)

        # Find content bounds with better threshold
        col_threshold = np.max(col_sums) * 0.05
        row_threshold = np.max(row_sums) * 0.05
        content_cols = np.where(col_sums > col_threshold)[0]
        content_rows = np.where(row_sums > row_threshold)[0]

        if len(content_cols) == 0 or len(content_rows) == 0:
            self.log_always("No content detected in screenshot")
            return {'pass': False, 'reason': 'No content detected'}

        x_start, x_end = content_cols[0], content_cols[-1]
        y_start, y_end = content_rows[0], content_rows[-1]
        content_width = x_end - x_start + 1
        content_height = y_end - y_start + 1
        self.log(f"Content bounds: ({x_start},{y_start}) to ({x_end},{y_end})")
        self.log(f"Content dimensions: {content_width}x{content_height}")

        # === SCANLINE ANALYSIS ===
        # Extract center column for vertical scanline analysis
        center_x = (x_start + x_end) // 2
        column = data[y_start:y_end+1, center_x]

        # Classify each row as bright (pixel data) or dark (scanline gap)
        # Use a threshold based on the column's brightness distribution
        # For solid color with scanlines, we have two distinct values (e.g., 37 and 122)
        col_min = np.min(column)
        col_max = np.max(column)
        # Threshold at midpoint between min and max for clean separation
        brightness_threshold = (col_min + col_max) / 2

        is_bright = column >= brightness_threshold  # Use >= to include max value

        # Find transitions
        transitions = np.diff(is_bright.astype(int))
        bright_to_dark = np.where(transitions == -1)[0]  # End of bright region
        dark_to_bright = np.where(transitions == 1)[0]   # Start of bright region

        self.log(f"Found {len(bright_to_dark)} bright->dark transitions")
        self.log(f"Found {len(dark_to_bright)} dark->bright transitions")

        # Count bright regions (pixel rows) and dark regions (blank scanlines)
        # A bright region is a contiguous sequence of bright pixels
        num_bright_regions = 0
        num_dark_regions = 0
        in_bright = is_bright[0]

        for i in range(1, len(is_bright)):
            if is_bright[i] != in_bright:
                if in_bright:
                    num_bright_regions += 1
                else:
                    num_dark_regions += 1
                in_bright = is_bright[i]
        # Count the last region
        if in_bright:
            num_bright_regions += 1
        else:
            num_dark_regions += 1

        self.log(f"Bright regions (pixel rows): {num_bright_regions}")
        self.log(f"Dark regions (blank scanlines): {num_dark_regions}")

        # === CHECK 1: Scanline spacing variance ===
        if len(bright_to_dark) > 1:
            # Calculate spacing between consecutive scanline starts
            spacings = np.diff(bright_to_dark)
            mean_spacing = np.mean(spacings)
            std_spacing = np.std(spacings)
            variance_pct = (std_spacing / mean_spacing) * 100 if mean_spacing > 0 else 100
        else:
            variance_pct = 100
            mean_spacing = 0
            std_spacing = 0

        self.log(f"Scanline spacing: mean={mean_spacing:.2f}px, std={std_spacing:.2f}px, variance={variance_pct:.1f}%")

        # === CHECK 2: Correct number of blank scanlines ===
        # With scan_line_distance=1.0 (Wide), we expect alternating: pixel row, gap, pixel row, gap...
        # So num_dark_regions should be approximately num_bright_regions - 1 (or equal)
        # PAL has 272 visible lines, so we expect ~272 bright regions and ~271-272 dark regions
        expected_pixel_rows = 272  # PAL height
        expected_blank_scanlines = expected_pixel_rows - 1  # One gap between each row

        # Allow 10% tolerance on counts (due to edge effects and scaling)
        pixel_row_tolerance = expected_pixel_rows * 0.15
        blank_tolerance = expected_blank_scanlines * 0.15

        pixel_rows_ok = abs(num_bright_regions - expected_pixel_rows) <= pixel_row_tolerance
        blanks_ok = abs(num_dark_regions - expected_blank_scanlines) <= blank_tolerance

        self.log(f"Expected ~{expected_pixel_rows} pixel rows, got {num_bright_regions} (tolerance ±{pixel_row_tolerance:.0f})")
        self.log(f"Expected ~{expected_blank_scanlines} blank scanlines, got {num_dark_regions} (tolerance ±{blank_tolerance:.0f})")

        # === CHECK 3: Pixel aspect ratio (informational) ===
        # C64 PAL: 384x272 pixels
        # When stretched to fill 1920x1080, pixels are NOT square (ratio ~1.26)
        # This check is informational - the primary validation is scanline evenness
        c64_width = 384
        c64_height = 272

        # Calculate actual pixel aspect ratio
        # Each C64 pixel row occupies (content_height / num_bright_regions) screen pixels vertically
        # Each C64 pixel column occupies (content_width / 384) screen pixels horizontally
        if num_bright_regions > 0:
            screen_pixels_per_c64_row = content_height / num_bright_regions
            screen_pixels_per_c64_col = content_width / c64_width
            pixel_aspect_ratio = screen_pixels_per_c64_col / screen_pixels_per_c64_row
        else:
            pixel_aspect_ratio = 0
            screen_pixels_per_c64_row = 0
            screen_pixels_per_c64_col = 0

        # Expected aspect ratio when filling screen (1920/384) / (1080/272) = 1.259
        expected_aspect_fill = (content_width / c64_width) / (content_height / c64_height)
        aspect_tolerance = 0.05  # 5% tolerance
        aspect_ok = abs(pixel_aspect_ratio - expected_aspect_fill) <= aspect_tolerance if num_bright_regions > 0 else False

        self.log(f"Pixel aspect ratio: {pixel_aspect_ratio:.3f} (expected {expected_aspect_fill:.3f} for screen fill)")
        if num_bright_regions > 0:
            self.log(f"  - Screen pixels per C64 column: {screen_pixels_per_c64_col:.2f}")
            self.log(f"  - Screen pixels per C64 row: {screen_pixels_per_c64_row:.2f}")

        # Save analysis visualization
        vis_path = self.output_dir / 'scanline_analysis.png'
        self._save_analysis_visualization(img, data, center_x, y_start, y_end,
                                           bright_to_dark, dark_to_bright, vis_path)

        # === FINAL RESULT ===
        # With source-space scanlines using output_height, all spacings should be identical.
        # Require ≤0.5% variance for pixel-perfect uniformity.
        variance_ok = variance_pct < 0.5

        all_pass = variance_ok and pixel_rows_ok and blanks_ok and aspect_ok

        result = {
            'pass': all_pass,
            'variance_pct': variance_pct,
            'variance_ok': variance_ok,
            'mean_spacing': mean_spacing,
            'std_spacing': std_spacing,
            'num_pixel_rows': num_bright_regions,
            'num_blank_scanlines': num_dark_regions,
            'expected_pixel_rows': expected_pixel_rows,
            'expected_blanks': expected_blank_scanlines,
            'pixel_rows_ok': pixel_rows_ok,
            'blanks_ok': blanks_ok,
            'pixel_aspect_ratio': pixel_aspect_ratio,
            'expected_aspect_fill': expected_aspect_fill,
            'aspect_ok': aspect_ok,
            'content_width': content_width,
            'content_height': content_height,
            'visualization': str(vis_path)
        }

        # Report results
        self.log_always(f"\n{'='*50}")
        self.log_always("SCANLINE ANALYSIS RESULTS")
        self.log_always(f"{'='*50}")

        if variance_ok:
            self.log_always(f"✅ Scanline variance: {variance_pct:.1f}% < 0.5%")
        else:
            self.log_always(f"❌ Scanline variance: {variance_pct:.1f}% >= 0.5%")

        if pixel_rows_ok:
            self.log_always(f"✅ Pixel rows: {num_bright_regions} (expected ~{expected_pixel_rows})")
        else:
            self.log_always(f"❌ Pixel rows: {num_bright_regions} (expected ~{expected_pixel_rows})")

        if blanks_ok:
            self.log_always(f"✅ Blank scanlines: {num_dark_regions} (expected ~{expected_blank_scanlines})")
        else:
            self.log_always(f"❌ Blank scanlines: {num_dark_regions} (expected ~{expected_blank_scanlines})")

        if aspect_ok:
            self.log_always(f"✅ Pixel aspect ratio: {pixel_aspect_ratio:.3f} (expected ~{expected_aspect_fill:.2f} for screen fill)")
        else:
            self.log_always(f"❌ Pixel aspect ratio: {pixel_aspect_ratio:.3f} (expected ~{expected_aspect_fill:.2f} ± {aspect_tolerance*100:.0f}%)")

        self.log_always(f"{'='*50}")
        if all_pass:
            self.log_always("✅ OVERALL: PASS")
        else:
            self.log_always("❌ OVERALL: FAIL")
        self.log_always(f"{'='*50}\n")

        return result

    def _save_analysis_visualization(self, img, data, center_x, y_start, y_end,
                                      bright_to_dark, dark_to_bright, output_path):
        """Save visualization of scanline analysis."""
        from PIL import ImageDraw

        vis = img.convert('RGB')
        draw = ImageDraw.Draw(vis)

        # Draw analysis column
        draw.line([(center_x, y_start), (center_x, y_end)], fill=(255, 0, 0), width=1)

        # Mark scanline transitions
        for y in bright_to_dark:
            actual_y = y_start + y
            draw.ellipse([(center_x - 3, actual_y - 3), (center_x + 3, actual_y + 3)],
                         outline=(0, 255, 0), width=1)

        vis.save(output_path)
        self.log(f"Analysis visualization saved: {output_path}")

    def cleanup(self):
        """Clean up processes."""
        if self.obs_process and not self.keep_obs:
            self.log("Stopping OBS...")
            self.obs_process.terminate()
            try:
                self.obs_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.obs_process.kill()

        self.tcp_server_running = False
        if self.tcp_server_socket:
            try:
                self.tcp_server_socket.close()
            except Exception:
                pass

        if self.xvfb_process:
            self.log("Stopping Xvfb...")
            self.xvfb_process.terminate()
            try:
                self.xvfb_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.xvfb_process.kill()

    def run(self):
        """Run the complete scanline test."""
        self.log_always("=== Scanline Evenness E2E Test ===")

        if not WEBSOCKET_AVAILABLE:
            self.log_always("ERROR: websocket-client not available")
            return False

        if not PIL_AVAILABLE:
            self.log_always("ERROR: PIL/numpy not available for image analysis")
            return False

        try:
            # Step 1: Build and install
            if not self.build_and_install():
                return False

            # Step 2: Apply scanline scenario
            self.apply_scenario_overrides()

            # Step 3: Start Xvfb
            if not self.start_xvfb():
                return False

            # Step 4: Start TCP mock server
            self.start_tcp_server()

            # Step 5: Start OBS
            if not self.start_obs():
                return False

            # Step 6: Send test packets
            if not self.generate_and_send_packets():
                self.log_always("Failed to send test packets")
                return False

            # Step 7: Take screenshot
            screenshot = self.take_screenshot('scanline_test.png')
            if not screenshot:
                self.log_always("Failed to take screenshot")
                return False

            # Step 8: Analyze scanlines
            result = self.analyze_scanlines(screenshot)
            if result is None:
                return False

            return result.get('pass', False)

        finally:
            self.cleanup()


def main():
    parser = argparse.ArgumentParser(description='Scanline Evenness E2E Test')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    parser.add_argument('--skip-build', action='store_true', help='Skip build step')
    parser.add_argument('--keep-obs', action='store_true', help='Keep OBS running after test')
    args = parser.parse_args()

    test_dir = Path(__file__).parent
    test = ScanlineTest(test_dir, verbose=args.verbose, skip_build=args.skip_build,
                        keep_obs=args.keep_obs)

    success = test.run()
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
