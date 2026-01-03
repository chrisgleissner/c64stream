#!/usr/bin/env python3
"""
Mock Ultimate 64 REST API server for testing
Implements minimal endpoints for E2E testing
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import sys
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

    def log_message(self, format, *args):
        """Override to provide cleaner logging"""
        sys.stderr.write(f"[MockC64] {format % args}\n")

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

        path = urlparse(self.path)

        if path.path == "/v1/version":
            self.handle_version()
        elif path.path == "/v1/info":
            self.handle_info()
        elif path.path == "/v1/machine:readmem":
            self.handle_read_memory(path.query)
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
        elif path.path == "/v1/machine:writemem":
            self.handle_write_memory(path.query)
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
            self.handle_mount_disk(path.query)
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
        self.log_message("RESET machine")
        # Initialize keyboard buffer to empty
        self.memory[0x00C6] = 0
        for i in range(10):
            self.memory[0x0277 + i] = 0
        self.send_json_response({"status": "ok"})

    def handle_reboot(self):
        """Handle machine reboot"""
        self.log_message("REBOOT machine")
        # Clear all memory
        self.memory = bytearray(65536)
        self.send_json_response({"status": "ok"})

    def handle_read_memory(self, query):
        """Handle memory read"""
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
        params = parse_qs(query)
        song_number = params.get('songnr', ['0'])[0]
        path = params.get('path', [None])[0]

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

    def handle_mount_disk(self, query):
        """Handle disk mount"""
        params = parse_qs(query)
        disk_type = params.get('type', ['d64'])[0]
        mode = params.get('mode', ['readonly'])[0]
        path = params.get('path', [None])[0]

        if path:
            # Mount from C64U filesystem path
            path = unquote(path)
            entry, error = self._resolve_path(path)
            if error:
                self.send_json_response({"errors": [error]}, 404)
                return
            if entry.get('type') != 'file' or not path.lower().endswith(f'.{disk_type}'):
                self.send_json_response({"errors": [f"Not a {disk_type.upper()} file: {path}"]}, 400)
                return
            self.log_message(f"MOUNT_DISK path={path} type={disk_type} mode={mode}")
        else:
            # Upload mode (body contains disk image data)
            content_length = int(self.headers.get('Content-Length', 0))
            disk_data = self.rfile.read(content_length)
            self.log_message(f"MOUNT_DISK upload type={disk_type} mode={mode} size={len(disk_data)}")

        self.send_json_response({"status": "ok"})

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
