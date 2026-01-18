#!/usr/bin/env python3
"""
Mock Ultimate 64 REST API server for testing
Implements minimal endpoints for E2E testing
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import sys
import os
from urllib.parse import parse_qs, urlparse, unquote

class MockC64Server(BaseHTTPRequestHandler):
    # Simulated C64 memory
    memory = bytearray(65536)
    # Optional network password (set to None to disable authentication)
    password = None
    # Simulated filesystem structure
    filesystem = {
        '/': {
            'type': 'directory',
            'children': {
                'ROMs': {
                    'type': 'directory',
                    'children': {
                        '1541.rom': {'type': 'file', 'size': 32768}
                    }
                },
                'Commodore': {
                    'type': 'directory',
                    'children': {
                        'SID': {
                            'type': 'directory',
                            'children': {
                                'tune1.sid': {'type': 'file', 'size': 4096},
                                'tune2.sid': {'type': 'file', 'size': 3584},
                                'subfolder': {
                                    'type': 'directory',
                                    'children': {
                                        'tune3.sid': {'type': 'file', 'size': 5120}
                                    }
                                }
                            }
                        },
                        'PRG': {
                            'type': 'directory',
                            'children': {
                                'game1.prg': {'type': 'file', 'size': 16384},
                                'demo.prg': {'type': 'file', 'size': 8192}
                            }
                        },
                        'D64': {
                            'type': 'directory',
                            'children': {
                                'disk1.d64': {'type': 'file', 'size': 174848},
                                'disk2.d64': {'type': 'file', 'size': 174848}
                            }
                        }
                    }
                }
            }
        }
    }

    request_log = []
    request_log_path = os.environ.get('C64U_MOCK_LOG', '')

    config_data = {
        "Audio Mixer": {
            "Vol Sid Socket 1": {"current": "80", "values": ["0", "50", "100"]},
            "Vol Sid Socket 2": {"current": "70", "values": ["0", "50", "100"]},
            "Vol UltiSid 1": {"current": "60", "values": ["0", "50", "100"]},
            "Vol UltiSid 2": {"current": "60", "values": ["0", "50", "100"]}
        },
        "U64 Specific Settings": {
            "System Mode": {"current": "PAL", "values": ["PAL", "NTSC"]},
            "CPU Speed": {"current": " 1", "values": [" 1", " 2", " 3", " 4"]}
        },
        "SID Sockets Configuration": {
            "SID Socket 1": {"current": "Enabled", "values": ["Enabled", "Disabled"]},
            "SID Socket 2": {"current": "Enabled", "values": ["Enabled", "Disabled"]},
            "UltiSID 1 Model": {"current": "8580", "values": ["6581", "8580"]},
            "UltiSID 2 Model": {"current": "8580", "values": ["6581", "8580"]}
        },
        "UltiSID Configuration": {
            "UltiSID 1 Filter Curve": {"current": "Flat", "values": ["Flat", "8580", "6581"]},
            "UltiSID 2 Filter Curve": {"current": "Flat", "values": ["Flat", "8580", "6581"]},
            "UltiSID 1 Resonance": {"current": "Medium", "values": ["Low", "Medium", "High"]},
            "UltiSID 2 Resonance": {"current": "Medium", "values": ["Low", "Medium", "High"]},
            "UltiSID 1 Combined Waveforms": {"current": "Enabled", "values": ["Enabled", "Disabled"]},
            "UltiSID 2 Combined Waveforms": {"current": "Enabled", "values": ["Enabled", "Disabled"]},
            "UltiSID 1 Digis": {"current": "Off", "values": ["Off", "On"]},
            "UltiSID 2 Digis": {"current": "Off", "values": ["Off", "On"]}
        },
        "Drive A Settings": {
            "Drive Bus ID": {"current": "8", "values": ["8", "9", "10", "11"]}
        },
        "Drive B Settings": {
            "Drive Bus ID": {"current": "9", "values": ["8", "9", "10", "11"]}
        }
    }

    drives = {
        "a": {
            "enabled": True,
            "bus_id": 8,
            "type": "1541",
            "rom": "1541.rom",
            "image_file": "",
            "image_path": ""
        },
        "b": {
            "enabled": True,
            "bus_id": 9,
            "type": "1541",
            "rom": "1541.rom",
            "image_file": "",
            "image_path": ""
        },
        "softiec": {
            "enabled": False,
            "bus_id": 0,
            "type": "softiec",
            "rom": "",
            "image_file": "",
            "image_path": ""
        }
    }

    def log_message(self, format, *args):
        """Override to provide cleaner logging"""
        sys.stderr.write(f"[MockC64] {format % args}\n")

    def record_request(self, method, path):
        line = f"{method} {path}"
        self.request_log.append(line)
        if self.request_log_path:
            try:
                with open(self.request_log_path, 'a', encoding='utf-8') as f:
                    f.write(line + "\n")
            except Exception:
                pass

    def check_password(self):
        """Verify X-Password header if password is set"""
        if self.password is None:
            return True

        provided_password = self.headers.get('X-Password', '')
        if provided_password != self.password:
            self.send_json_response({"errors": ["Invalid or missing password"]}, 403)
            return False
        return True

    def do_GET(self):
        """Handle GET requests"""
        if not self.check_password():
            return

        if self.path == "/__requests":
            self.send_json_response({"requests": list(self.request_log)})
            return

        path = urlparse(self.path)

        if path.path == "/v1/version":
            self.handle_version()
        elif path.path == "/v1/info":
            self.handle_info()
        elif path.path == "/v1/machine:readmem":
            self.handle_read_memory(path.query)
        elif path.path == "/v1/drives":
            self.handle_drives_list()
        elif path.path == "/v1/configs":
            self.handle_config_list("", path.query)
        elif path.path.startswith("/v1/configs/"):
            if path.path.count("/") >= 4:
                self.handle_config_get(path.path, path.query)
            else:
                self.handle_config_list(path.path, path.query)
        elif path.path == "/v1/files:list":
            self.handle_list_files(path.query)
        else:
            self.send_error(404, f"Not found: {path.path}")

    def do_PUT(self):
        """Handle PUT requests"""
        if not self.check_password():
            return

        path = urlparse(self.path)

        if path.path == "/v1/machine:reset":
            self.handle_reset()
        elif path.path == "/v1/machine:reboot":
            self.handle_reboot()
        elif path.path == "/v1/machine:pause":
            self.handle_pause()
        elif path.path == "/v1/machine:resume":
            self.handle_resume()
        elif path.path == "/v1/machine:poweroff":
            self.handle_poweroff()
        elif path.path == "/v1/machine:writemem":
            self.handle_write_memory(path.query)
        elif path.path == "/v1/runners:sidplay":
            self.handle_sidplay(path.query)
        elif path.path.startswith("/v1/drives/"):
            self.handle_drive_command(path.path, path.query)
        elif path.path.startswith("/v1/configs:"):
            self.handle_config_action(path.path)
        elif path.path.startswith("/v1/configs/"):
            self.handle_config_set(path.path, path.query)
        else:
            self.send_error(404, f"Not found: {path.path}")

    def do_POST(self):
        """Handle POST requests"""
        if not self.check_password():
            return

        path = urlparse(self.path)

        if path.path == "/v1/runners:sidplay":
            self.handle_sidplay(path.query)
        elif path.path == "/v1/runners:run_prg":
            self.handle_run_prg(path.query)
        elif path.path.startswith("/v1/drives/") and path.path.endswith(":mount"):
            self.handle_mount_disk(path.path, path.query, "POST")
        elif path.path.startswith("/v1/drives/") and path.path.endswith(":load_rom"):
            self.handle_load_rom(path.path, path.query, "POST")
        else:
            self.send_error(404, f"Not found: {path.path}")

    def do_HEAD(self):
        """Handle HEAD requests"""
        if not self.check_password():
            return

        path = urlparse(self.path)

        if path.path == "/v1/files:stat":
            self.handle_stat_file(path.query)
        else:
            self.send_error(404, f"Not found: {path.path}")

    def send_json_response(self, data, status=200):
        """Send a JSON response with errors array"""
        response = {"errors": [], **data}
        body = json.dumps(response).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _resolve_path(self, path_str):
        """Resolve a filesystem path to its entry

        Returns: (entry_dict, error_message) tuple
        """
        if not path_str or path_str == '':
            return self.filesystem['/'], None

        # Normalize path
        path_str = path_str.strip()
        if not path_str.startswith('/'):
            path_str = '/' + path_str

        # Split path into components
        components = [c for c in path_str.split('/') if c]

        # Navigate the filesystem
        current = self.filesystem['/']
        for component in components:
            if current.get('type') != 'directory':
                return None, f"Not a directory: /{'/'.join(components[:components.index(component)])}"

            children = current.get('children', {})
            if component not in children:
                return None, f"Path not found: {path_str}"

            current = children[component]

        return current, None

    def handle_list_files(self, query):
        """Handle file listing (GET /v1/files:list?path=<path>&recursive=<bool>)"""
        params = parse_qs(query)
        path = unquote(params.get('path', ['/'])[0])
        recursive = params.get('recursive', ['false'])[0].lower() == 'true'

        entry, error = self._resolve_path(path)
        if error:
            self.send_json_response({"errors": [error]}, 404)
            return

        if entry.get('type') != 'directory':
            self.send_json_response({"errors": [f"Not a directory: {path}"]}, 400)
            return

        # Build entry list
        entries = []

        def add_entries(current_entry, current_path, recurse):
            children = current_entry.get('children', {})
            for name, child in children.items():
                child_path = f"{current_path}/{name}".replace('//', '/')
                entry_info = {
                    "name": name,
                    "type": child['type'],
                    "path": child_path
                }
                if child['type'] == 'file':
                    entry_info['size'] = child.get('size', 0)

                entries.append(entry_info)

                # Recurse into subdirectories if requested
                if recurse and child['type'] == 'directory':
                    add_entries(child, child_path, recurse)

        add_entries(entry, path, recursive)

        self.log_message(f"LIST_FILES path={path} recursive={recursive} → {len(entries)} entries")
        self.send_json_response({"path": path, "entries": entries})

    def handle_stat_file(self, query):
        """Handle file stat (HEAD /v1/files:stat?path=<path>)"""
        params = parse_qs(query)
        path = unquote(params.get('path', [''])[0])

        entry, error = self._resolve_path(path)
        if error:
            self.send_response(404)
            self.end_headers()
            return

        self.log_message(f"STAT_FILE path={path} → {entry['type']}")
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        if entry.get('type') == 'file':
            self.send_header('Content-Length', str(entry.get('size', 0)))
        self.end_headers()

    def handle_version(self):
        """Handle version request"""
        self.send_json_response({"version": "0.1"})

    def handle_info(self):
        """Handle info request (firmware 3.12+)"""
        self.send_json_response({
            "product": "Ultimate 64",
            "firmware_version": "3.12",
            "fpga_version": "11F",
            "core_version": "143",
            "hostname": "MockC64",
            "unique_id": "000000"
        })

    def handle_reset(self):
        """Handle machine reset"""
        self.record_request("PUT", "/v1/machine:reset")
        self.log_message("RESET machine")
        # Initialize keyboard buffer to empty
        self.memory[0x00C6] = 0
        for i in range(10):
            self.memory[0x0277 + i] = 0
        self.send_json_response({"status": "ok"})

    def handle_reboot(self):
        """Handle machine reboot"""
        self.record_request("PUT", "/v1/machine:reboot")
        self.log_message("REBOOT machine")
        # Clear all memory
        self.memory = bytearray(65536)
        self.send_json_response({"status": "ok"})

    def handle_pause(self):
        self.record_request("PUT", "/v1/machine:pause")
        self.log_message("PAUSE machine")
        self.send_json_response({"status": "ok"})

    def handle_resume(self):
        self.record_request("PUT", "/v1/machine:resume")
        self.log_message("RESUME machine")
        self.send_json_response({"status": "ok"})

    def handle_poweroff(self):
        self.record_request("PUT", "/v1/machine:poweroff")
        self.log_message("POWEROFF machine")
        self.send_json_response({"status": "ok"})

    def handle_read_memory(self, query):
        """Handle memory read"""
        self.record_request("GET", f"/v1/machine:readmem?{query}")
        params = parse_qs(query)
        address = int(params.get('address', ['0'])[0], 16)
        length = int(params.get('length', ['1'])[0])

        data = bytes(self.memory[address:address+length])
        self.log_message(f"READ memory ${address:04X} length={length} → {data.hex()}")

        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def handle_write_memory(self, query):
        """Handle memory write"""
        self.record_request("PUT", f"/v1/machine:writemem?{query}")
        params = parse_qs(query)
        address = int(params.get('address', ['0'])[0], 16)
        hex_data = params.get('data', [''])[0]

        # Convert hex string to bytes
        data = bytes.fromhex(hex_data)

        # Check 128 byte limit (as per spec)
        if len(data) > 128:
            self.send_json_response({"errors": ["Data exceeds 128 byte limit"]}, 400)
            return

        # Write to memory
        for i, byte in enumerate(data):
            self.memory[address + i] = byte

        self.log_message(f"WRITE memory ${address:04X} data={hex_data}")

        # Simulate keyboard buffer consumption
        if address == 0x0277:
            # After writing to keyboard buffer, simulate gradual consumption
            # In real C64, KERNAL would process these keys
            pass

        self.send_json_response({"status": "ok"})

    def handle_sidplay(self, query):
        """Handle SID playback"""
        self.record_request(self.command, f"/v1/runners:sidplay?{query}" if query else "/v1/runners:sidplay")
        params = parse_qs(query)
        song_number = params.get('songnr', ['0'])[0]
        path = params.get('path', [None])[0]
        if not path:
            path = params.get('file', [None])[0]

        if path:
            # Play from C64U filesystem path
            path = unquote(path)
            entry, error = self._resolve_path(path)
            if error:
                self.send_json_response({"errors": [error]}, 404)
                return
            if entry.get('type') != 'file' or not path.lower().endswith('.sid'):
                self.send_json_response({"errors": [f"Not a SID file: {path}"]}, 400)
                return
            self.log_message(f"SIDPLAY path={path} song={song_number}")
        else:
            # Upload mode (body contains SID data)
            content_length = int(self.headers.get('Content-Length', 0))
            if content_length > 0:
                sid_data = self.rfile.read(content_length)
                self.log_message(f"SIDPLAY upload size={len(sid_data)} song={song_number}")
            else:
                self.log_message(f"SIDPLAY song={song_number}")

        self.send_json_response({"status": "ok"})

    def handle_run_prg(self, query):
        """Handle PRG execution"""
        self.record_request("POST", f"/v1/runners:run_prg?{query}" if query else "/v1/runners:run_prg")
        params = parse_qs(query)
        path = params.get('path', [None])[0]

        if path:
            # Run from C64U filesystem path
            path = unquote(path)
            entry, error = self._resolve_path(path)
            if error:
                self.send_json_response({"errors": [error]}, 404)
                return
            if entry.get('type') != 'file' or not path.lower().endswith('.prg'):
                self.send_json_response({"errors": [f"Not a PRG file: {path}"]}, 400)
                return
            self.log_message(f"RUN_PRG path={path}")
        else:
            # Upload mode (body contains PRG data)
            content_length = int(self.headers.get('Content-Length', 0))
            prg_data = self.rfile.read(content_length)
            self.log_message(f"RUN_PRG upload size={len(prg_data)}")

        self.send_json_response({"status": "ok"})

    def handle_mount_disk(self, path, query, method):
        """Handle disk mount"""
        self.record_request(method, f"{path}?{query}" if query else path)
        params = parse_qs(query)
        disk_type = params.get('type', ['d64'])[0]
        mode = params.get('mode', ['readonly'])[0]
        image_path = params.get('path', [None])[0]
        image = params.get('image', [None])[0]

        drive_id = path.split('/v1/drives/')[-1].split(':')[0]
        if drive_id in self.drives:
            self.drives[drive_id]["image_file"] = ""
            self.drives[drive_id]["image_path"] = ""

        if image_path or image:
            # Mount from C64U filesystem path
            mount_path = unquote(image_path or image)
            entry, error = self._resolve_path(mount_path)
            if error:
                self.send_json_response({"errors": [error]}, 404)
                return
            if entry.get('type') != 'file' or not mount_path.lower().endswith(f'.{disk_type}'):
                self.send_json_response({"errors": [f"Not a {disk_type.upper()} file: {mount_path}"]}, 400)
                return
            if drive_id in self.drives:
                self.drives[drive_id]["image_path"] = mount_path
            self.log_message(f"MOUNT_DISK path={mount_path} type={disk_type} mode={mode}")
        else:
            # Upload mode (body contains disk image data)
            content_length = int(self.headers.get('Content-Length', 0))
            disk_data = self.rfile.read(content_length)
            if drive_id in self.drives:
                self.drives[drive_id]["image_file"] = f"upload.{disk_type}"
            self.log_message(f"MOUNT_DISK upload type={disk_type} mode={mode} size={len(disk_data)}")

        self.send_json_response({"status": "ok"})

    def handle_load_rom(self, path, query, method):
        self.record_request(method, f"{path}?{query}" if query else path)
        params = parse_qs(query)
        rom_file = params.get('file', [None])[0]
        drive_id = path.split('/v1/drives/')[-1].split(':')[0]

        if rom_file:
            rom_path = unquote(rom_file)
            entry, error = self._resolve_path(rom_path)
            if error:
                self.send_json_response({"errors": [error]}, 404)
                return
            if entry.get('type') != 'file':
                self.send_json_response({"errors": [f"Not a ROM file: {rom_path}"]}, 400)
                return
            if drive_id in self.drives:
                self.drives[drive_id]["rom"] = rom_path.split('/')[-1]
            self.log_message(f"LOAD_ROM path={rom_path}")
        else:
            content_length = int(self.headers.get('Content-Length', 0))
            rom_data = self.rfile.read(content_length)
            if drive_id in self.drives:
                self.drives[drive_id]["rom"] = "upload.rom"
            self.log_message(f"LOAD_ROM upload size={len(rom_data)}")

        self.send_json_response({"status": "ok"})

    def handle_drive_command(self, path, query):
        drive_id = path.split('/v1/drives/')[-1].split(':')[0]
        action = path.split(':')[-1]

        if drive_id not in self.drives:
            self.send_json_response({"errors": [f"Unknown drive: {drive_id}"]}, 404)
            return

        if action == "mount":
            self.handle_mount_disk(path, query, "PUT")
            return
        if action == "load_rom":
            self.handle_load_rom(path, query, "PUT")
            return

        self.record_request("PUT", f"{path}?{query}" if query else path)

        if action == "remove":
            self.drives[drive_id]["image_file"] = ""
            self.drives[drive_id]["image_path"] = ""
        elif action == "reset":
            pass
        elif action == "on":
            self.drives[drive_id]["enabled"] = True
        elif action == "off":
            self.drives[drive_id]["enabled"] = False
        elif action == "set_mode":
            params = parse_qs(query)
            mode = params.get('mode', [''])[0]
            self.drives[drive_id]["type"] = mode
        self.send_json_response({"status": "ok"})

    def handle_drives_list(self):
        self.record_request("GET", "/v1/drives")
        data = {
            "a": self.drives["a"],
            "b": self.drives["b"],
            "softiec": self.drives["softiec"]
        }
        self.send_json_response(data)

    def handle_config_list(self, path, query):
        if path:
            self.record_request("GET", f"{path}?{query}" if query else path)
        else:
            self.record_request("GET", "/v1/configs")

        if not path or path == "/v1/configs":
            categories = list(self.config_data.keys())
            self.send_json_response({"categories": categories})
            return

        category = unquote(path.split("/v1/configs/")[-1])
        items = self.config_data.get(category, {})
        response = {category: {key: val.get("current", "") for key, val in items.items()}}
        self.send_json_response(response)

    def handle_config_get(self, path, query):
        self.record_request("GET", f"{path}?{query}" if query else path)
        parts = path.split("/v1/configs/")[-1].split("/")
        category = unquote(parts[0])
        item = unquote(parts[1]) if len(parts) > 1 else ""
        items = self.config_data.get(category, {})
        if item not in items:
            self.send_json_response({"errors": [f"Unknown item: {category}/{item}"]}, 404)
            return
        response = {category: {item: items[item]}}
        self.send_json_response(response)

    def handle_config_set(self, path, query):
        self.record_request("PUT", f"{path}?{query}" if query else path)
        parts = path.split("/v1/configs/")[-1].split("/")
        category = unquote(parts[0])
        item = unquote(parts[1]) if len(parts) > 1 else ""
        params = parse_qs(query)
        value = unquote(params.get('value', [''])[0])
        if category not in self.config_data or item not in self.config_data[category]:
            self.send_json_response({"errors": [f"Unknown item: {category}/{item}"]}, 404)
            return
        self.config_data[category][item]["current"] = value
        self.send_json_response({"status": "ok"})

    def handle_config_action(self, path):
        self.record_request("PUT", path)
        if path in ("/v1/configs:save_to_flash", "/v1/configs:load_from_flash", "/v1/configs:reset_to_default"):
            self.send_json_response({"status": "ok"})
        else:
            self.send_json_response({"errors": [f"Unknown action: {path}"]}, 404)

def run_server(port=8064, password=None):
    """Run the mock server

    Args:
        port: Port to listen on
        password: Optional network password (enables authentication if set)
    """
    MockC64Server.password = password
    server_address = ('', port)
    httpd = HTTPServer(server_address, MockC64Server)
    print(f"Mock C64U server running on port {port}")
    print(f"Base URL: http://localhost:{port}")
    if password:
        print(f"Authentication: ENABLED (password: {password})")
    else:
        print("Authentication: DISABLED")
    print("Press Ctrl+C to stop")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        httpd.shutdown()

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Mock Ultimate 64 REST API server')
    parser.add_argument('--port', type=int, default=8064, help='Port to listen on (default: 8064)')
    parser.add_argument('--password', type=str, default=None, help='Network password (enables authentication)')
    args = parser.parse_args()
    run_server(args.port, args.password)
