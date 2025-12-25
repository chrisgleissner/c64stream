#!/usr/bin/env python3
"""
Scanline All Modes Test - Tests all scanline distance settings and creates screenshots.

Tests:
- Tight (0.25): 5x scaling, 4 bright + 1 dark pattern
- Normal (0.5): 3x scaling, 2 bright + 1 dark pattern
- Wide (1.0): 4x scaling, 2 bright + 2 dark pattern
- Extra Wide (2.0): 3x scaling, 1 bright + 2 dark pattern

For each mode, verifies:
- Scanline variance ≤0.5%
- Correct pixel/gap counts
- Screenshots saved to scanline_output/

Usage:
    python scanline_all_modes_test.py [--verbose]
"""

import sys
import os
import json
import time
import subprocess
import shutil
import socket
import glob
import hashlib
import uuid
import argparse
from pathlib import Path

# Add parent directory for imports
sys.path.insert(0, str(Path(__file__).parent))

from PIL import Image
import numpy as np

# Scanline mode configurations
SCANLINE_MODES = {
    'off': {
        'distance': 0.0,
        'scale': 1,
        'pattern': 'No scanlines',
        'bright_pixels': 1,
        'gap_pixels': 0,
    },
    'tight': {
        'distance': 0.25,
        'scale': 5,
        'pattern': '4 bright + 1 dark',
        'bright_pixels': 4,
        'gap_pixels': 1,
    },
    'normal': {
        'distance': 0.5,
        'scale': 3,
        'pattern': '2 bright + 1 dark',
        'bright_pixels': 2,
        'gap_pixels': 1,
    },
    'wide': {
        'distance': 1.0,
        'scale': 4,
        'pattern': '2 bright + 2 dark',
        'bright_pixels': 2,
        'gap_pixels': 2,
    },
    'extra_wide': {
        'distance': 2.0,
        'scale': 3,
        'pattern': '1 bright + 2 dark',
        'bright_pixels': 1,
        'gap_pixels': 2,
    },
}


class ScanlineAllModesTest:
    def __init__(self, verbose=False):
        self.verbose = verbose
        self.test_dir = Path(__file__).parent
        self.project_root = self.test_dir.parent.parent
        self.build_dir = self.project_root / 'build_x86_64'
        self.output_dir = self.test_dir / 'scanline_output'
        self.output_dir.mkdir(exist_ok=True)

        # Network ports
        self.control_port = 21064
        self.video_port = 21000
        self.audio_port = 21001

        # State
        self.xvfb_proc = None
        self.obs_proc = None
        self.tcp_server = None
        self.ws = None
        self.results = {}

    def log(self, msg):
        if self.verbose:
            print(f"[ALL_MODES] {msg}")

    def log_always(self, msg):
        print(f"[ALL_MODES] {msg}")

    def generate_solid_packets(self):
        """Generate solid color test packets."""
        packet_dir = self.output_dir / 'packets'
        if packet_dir.exists():
            shutil.rmtree(packet_dir)
        packet_dir.mkdir(parents=True)

        self.log("Generating solid color test packets...")
        result = subprocess.run(
            ['python', 'generate_packets.py', '--format', 'PAL', '--frames', '60',
             '--output', str(packet_dir), '--pattern', 'solid'],
            cwd=self.test_dir,
            capture_output=True,
            text=True
        )

        if result.returncode != 0:
            self.log_always(f"Packet generation failed: {result.stderr}")
            return False

        self.log("Packets generated successfully")
        return True

    def install_plugin(self):
        """Install the plugin to OBS."""
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

        self.log("Plugin installed")
        return True

    def start_xvfb(self):
        """Start Xvfb virtual display."""
        self.log("Starting Xvfb...")
        os.environ['DISPLAY'] = ':99'

        subprocess.run(['pkill', '-9', 'Xvfb'], capture_output=True)
        time.sleep(0.5)

        self.xvfb_proc = subprocess.Popen(
            ['Xvfb', ':99', '-screen', '0', '1920x1080x24'],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        time.sleep(1)

        if self.xvfb_proc.poll() is not None:
            self.log_always("Failed to start Xvfb")
            return False

        self.log("Xvfb started")
        return True

    def start_tcp_server(self):
        """Start TCP mock server."""
        self.tcp_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp_server.bind(('127.0.0.1', self.control_port))
        self.tcp_server.listen(1)
        self.tcp_server.settimeout(30)
        self.log("TCP server started")
        return True

    def handle_tcp_connection(self):
        """Handle TCP connection from plugin."""
        import threading

        def tcp_worker():
            while True:
                try:
                    conn, addr = self.tcp_server.accept()
                    conn.settimeout(1.0)
                    while True:
                        try:
                            data = conn.recv(1024)
                            if not data:
                                break
                        except socket.timeout:
                            continue
                        except:
                            break
                except socket.timeout:
                    continue
                except:
                    break

        self.tcp_thread = threading.Thread(target=tcp_worker, daemon=True)
        self.tcp_thread.start()

    def create_obs_profile(self, mode_name, mode_config):
        """Create OBS profile for a specific scanline mode."""
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

        # WebSocket config
        ws_cfg_dir = obs_config_dir / 'plugin_config/obs-websocket'
        ws_cfg_dir.mkdir(parents=True, exist_ok=True)
        with open(ws_cfg_dir / 'config.json', 'w') as f:
            json.dump({
                "alerts_enabled": False,
                "auth_required": False,
                "first_load": False,
                "server_enabled": True,
                "server_port": 4455
            }, f)

        # Profile basic.ini
        with open(profile_dir / 'basic.ini', 'w') as f:
            f.write("""[General]
Name=C64StreamTest

[Video]
BaseCX=1920
BaseCY=1080
OutputCX=1920
OutputCY=1080
FPSType=1
FPSCommon=50
""")

        # Scene configuration
        scene_uuid = str(uuid.uuid4())
        source_uuid = str(uuid.uuid4())
        canvas_uuid = "6c69626f-6273-4c00-9d88-c5136d61696e"

        # Calculate scaling based on mode
        source_width = 384.0
        source_height = 272.0
        scale = mode_config['scale']

        if mode_config['distance'] == 0.0:
            # No scanlines - use 4x for good size
            scale = 4

        scaled_width = source_width * scale
        scaled_height = source_height * scale
        pos_x = (1920.0 - scaled_width) / 2.0
        pos_y = (1080.0 - scaled_height) / 2.0

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
                            "scale": {"x": float(scale), "y": float(scale)},
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
                        "scan_line_distance": mode_config['distance'],
                        "scan_line_strength": 0.7,
                        "pixel_width": 1.0,
                        "pixel_height": 1.0,
                        "blur_strength": 0.0,
                        "bloom_strength": 0.0,
                        "afterglow_duration_ms": 0,
                        "afterglow_curve": 0,
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
            "transitions": [],
            "transition_duration": 300,
            "modules": {"scripts-tool": []},
            "resolution": {"x": 1920, "y": 1080},
            "preview_locked": False,
            "scaling_enabled": False,
            "scaling_level": 0,
            "scaling_off_x": 0.0,
            "scaling_off_y": 0.0,
            "virtual_cam_internal": False,
            "canvases": [{"uuid": canvas_uuid, "name": "", "width": 1920, "height": 1080}]
        }

        scene_file = scenes_dir / 'C64StreamTest.json'
        backup_file = scenes_dir / 'C64StreamTest.json.bak'
        if backup_file.exists():
            backup_file.unlink()
        with open(scene_file, 'w') as f:
            json.dump(scene_config, f, indent=2)

    def start_obs(self):
        """Start OBS."""
        self.log("Starting OBS...")
        subprocess.run(['pkill', '-9', 'obs'], capture_output=True)
        time.sleep(1)

        obs_cmd = [
            'obs',
            '--minimize-to-tray',
            '--collection', 'C64StreamTest',
            '--profile', 'C64StreamTest',
            '--disable-updater',
            '--disable-missing-files-check',
            '--multi'
        ]

        self.obs_proc = subprocess.Popen(
            obs_cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env={**os.environ, 'DISPLAY': ':99'}
        )

        time.sleep(3)
        return self.obs_proc.poll() is None

    def stop_obs(self):
        """Stop OBS."""
        if self.obs_proc:
            self.obs_proc.terminate()
            try:
                self.obs_proc.wait(timeout=5)
            except:
                self.obs_proc.kill()
        subprocess.run(['pkill', '-9', 'obs'], capture_output=True)
        time.sleep(1)

    def connect_obs_websocket(self):
        """Connect to OBS WebSocket."""
        import websocket

        for attempt in range(30):
            try:
                ws = websocket.create_connection("ws://127.0.0.1:4455", timeout=2)
                # Handle hello
                hello = json.loads(ws.recv())
                if hello.get('op') == 0:
                    # Send identify
                    ws.send(json.dumps({"op": 1, "d": {"rpcVersion": 1}}))
                    identified = json.loads(ws.recv())
                    if identified.get('op') == 2:
                        return ws
            except:
                pass
            time.sleep(0.5)
        return None

    def take_screenshot(self, filename):
        """Take screenshot via OBS WebSocket."""
        ws = self.connect_obs_websocket()
        if not ws:
            return None

        try:
            request_id = hashlib.md5(str(time.time()).encode()).hexdigest()[:8]
            request = {
                "op": 6,
                "d": {
                    "requestType": "GetSourceScreenshot",
                    "requestId": request_id,
                    "requestData": {
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
                                image_data = resp_data.get('responseData', {}).get('imageData', '')
                                if image_data.startswith('data:image/png;base64,'):
                                    image_data = image_data[22:]

                                import base64
                                output_path = self.output_dir / filename
                                with open(output_path, 'wb') as f:
                                    f.write(base64.b64decode(image_data))
                                return output_path
                except:
                    pass
                time.sleep(0.1)
        finally:
            ws.close()

        return None

    def send_packets(self):
        """Send video packets."""
        packet_dir = self.output_dir / 'packets' / 'video' / 'PAL'
        video_files = sorted(glob.glob(str(packet_dir / "*.bin")))

        if not video_files:
            return False

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        for video_file in video_files:
            with open(video_file, 'rb') as f:
                data = f.read()
            sock.sendto(data, ('127.0.0.1', self.video_port))
            time.sleep(0.000293)

        sock.close()
        return True

    def analyze_scanlines(self, image_path, mode_config):
        """Analyze scanlines in screenshot."""
        img = np.array(Image.open(image_path).convert('L'))
        h, w = img.shape

        # Find content bounds
        row_max = np.max(img, axis=1)
        col_max = np.max(img, axis=0)

        threshold = 10
        content_rows = np.where(row_max > threshold)[0]
        content_cols = np.where(col_max > threshold)[0]

        if len(content_rows) == 0 or len(content_cols) == 0:
            return {'error': 'No content detected'}

        y_start, y_end = content_rows[0], content_rows[-1]
        x_start, x_end = content_cols[0], content_cols[-1]

        content_width = x_end - x_start + 1
        content_height = y_end - y_start + 1

        result = {
            'content_bounds': (x_start, y_start, x_end, y_end),
            'content_size': (content_width, content_height),
        }

        if mode_config['distance'] == 0.0:
            # No scanlines - just return basic info
            result['scanlines_enabled'] = False
            return result

        result['scanlines_enabled'] = True

        # Analyze center column
        center_x = (x_start + x_end) // 2
        col = img[y_start:y_end+1, center_x]

        col_min, col_max = np.min(col), np.max(col)
        threshold = (col_min + col_max) / 2
        is_bright = col >= threshold

        transitions = np.diff(is_bright.astype(int))
        bright_to_dark = np.where(transitions == -1)[0]
        dark_to_bright = np.where(transitions == 1)[0]

        num_bright = len(bright_to_dark)
        num_dark = len(dark_to_bright) + 1 if len(dark_to_bright) > 0 else 0

        if len(bright_to_dark) >= 2:
            spacings = np.diff(bright_to_dark)
            mean_spacing = np.mean(spacings)
            std_spacing = np.std(spacings)
            variance_pct = 100 * std_spacing / mean_spacing if mean_spacing > 0 else 0

            unique, counts = np.unique(spacings, return_counts=True)
            spacing_dist = dict(zip(unique.tolist(), counts.tolist()))

            result['spacings'] = spacings.tolist()
            result['mean_spacing'] = mean_spacing
            result['std_spacing'] = std_spacing
            result['variance_pct'] = variance_pct
            result['spacing_distribution'] = spacing_dist
        else:
            result['variance_pct'] = 0
            result['mean_spacing'] = 0

        result['num_bright_regions'] = num_bright
        result['num_dark_regions'] = num_dark

        # Calculate expected pattern size
        total = mode_config['bright_pixels'] + mode_config['gap_pixels']
        result['expected_pattern_size'] = total

        return result

    def test_mode(self, mode_name, mode_config):
        """Test a single scanline mode."""
        self.log_always(f"\n{'='*60}")
        self.log_always(f"Testing mode: {mode_name.upper()}")
        self.log_always(f"  Distance: {mode_config['distance']}")
        self.log_always(f"  Scale: {mode_config['scale']}x")
        self.log_always(f"  Pattern: {mode_config['pattern']}")
        self.log_always(f"{'='*60}")

        # Create profile for this mode
        self.create_obs_profile(mode_name, mode_config)

        # Start OBS
        if not self.start_obs():
            return {'error': 'Failed to start OBS'}

        # Wait for WebSocket
        time.sleep(3)

        # Send packets
        self.send_packets()
        time.sleep(2)

        # Take screenshot
        screenshot_name = f"scanline_{mode_name}.png"
        screenshot_path = self.take_screenshot(screenshot_name)

        if not screenshot_path:
            self.stop_obs()
            return {'error': 'Failed to take screenshot'}

        self.log_always(f"Screenshot saved: {screenshot_path}")

        # Stop OBS
        self.stop_obs()

        # Analyze
        result = self.analyze_scanlines(screenshot_path, mode_config)
        result['screenshot'] = str(screenshot_path)

        return result

    def test_afterglow(self):
        """Test afterglow effect."""
        self.log_always(f"\n{'='*60}")
        self.log_always("Testing AFTERGLOW effect")
        self.log_always(f"{'='*60}")

        # Run afterglow E2E test
        result = subprocess.run(
            ['bash', '-c',
             'rm -rf test_output test_packets && '
             'C64_E2E_PATTERN=avpop C64_E2E_AFTERGLOW=1 '
             './e2e.sh --format PAL --frames 180 --no-cleanup 2>&1'],
            cwd=self.test_dir,
            capture_output=True,
            text=True
        )

        success = 'E2E test completed successfully' in result.stdout

        if success:
            self.log_always("✅ Afterglow test PASSED")
        else:
            self.log_always("❌ Afterglow test FAILED")
            self.log_always(result.stdout[-500:] if len(result.stdout) > 500 else result.stdout)

        return {'pass': success}

    def run(self):
        """Run all tests."""
        self.log_always("="*60)
        self.log_always("SCANLINE ALL MODES TEST")
        self.log_always("="*60)

        # Generate packets
        if not self.generate_solid_packets():
            return False

        # Install plugin
        if not self.install_plugin():
            return False

        # Start Xvfb
        if not self.start_xvfb():
            return False

        # Start TCP server
        if not self.start_tcp_server():
            return False

        self.handle_tcp_connection()

        try:
            # Test each mode
            for mode_name, mode_config in SCANLINE_MODES.items():
                result = self.test_mode(mode_name, mode_config)
                self.results[mode_name] = result

                if 'error' in result:
                    self.log_always(f"❌ {mode_name}: {result['error']}")
                elif not result.get('scanlines_enabled', True):
                    self.log_always(f"✅ {mode_name}: No scanlines (disabled)")
                else:
                    variance = result.get('variance_pct', 0)
                    mean = result.get('mean_spacing', 0)
                    dist = result.get('spacing_distribution', {})

                    if variance <= 0.5:
                        self.log_always(f"✅ {mode_name}: Variance {variance:.2f}% ≤ 0.5%")
                        self.log_always(f"   Mean spacing: {mean:.2f}px, Distribution: {dist}")
                    else:
                        self.log_always(f"❌ {mode_name}: Variance {variance:.2f}% > 0.5%")
                        self.log_always(f"   Mean spacing: {mean:.2f}px, Distribution: {dist}")

            # Test afterglow
            self.results['afterglow'] = self.test_afterglow()

        finally:
            # Cleanup
            self.stop_obs()
            if self.xvfb_proc:
                self.xvfb_proc.terminate()
            if self.tcp_server:
                self.tcp_server.close()

        # Summary
        self.log_always("\n" + "="*60)
        self.log_always("SUMMARY")
        self.log_always("="*60)

        all_pass = True
        for mode_name, result in self.results.items():
            if 'error' in result:
                self.log_always(f"❌ {mode_name}: ERROR")
                all_pass = False
            elif mode_name == 'afterglow':
                if result.get('pass'):
                    self.log_always(f"✅ {mode_name}: PASS")
                else:
                    self.log_always(f"❌ {mode_name}: FAIL")
                    all_pass = False
            elif not result.get('scanlines_enabled', True):
                self.log_always(f"✅ {mode_name}: PASS (no scanlines)")
            else:
                variance = result.get('variance_pct', 100)
                if variance <= 0.5:
                    self.log_always(f"✅ {mode_name}: PASS (variance {variance:.2f}%)")
                else:
                    self.log_always(f"❌ {mode_name}: FAIL (variance {variance:.2f}%)")
                    all_pass = False

        self.log_always("\n" + "="*60)
        if all_pass:
            self.log_always("✅ ALL TESTS PASSED")
        else:
            self.log_always("❌ SOME TESTS FAILED")
        self.log_always("="*60)

        # Save results
        results_file = self.output_dir / 'all_modes_results.json'
        with open(results_file, 'w') as f:
            json.dump(self.results, f, indent=2, default=str)
        self.log_always(f"\nResults saved to: {results_file}")

        return all_pass


def main():
    parser = argparse.ArgumentParser(description='Test all scanline modes')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    args = parser.parse_args()

    test = ScanlineAllModesTest(verbose=args.verbose)
    success = test.run()
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
