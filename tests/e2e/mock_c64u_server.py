#!/usr/bin/env python3
"""
Mock Ultimate 64 REST API server for testing
Implements minimal endpoints for E2E testing
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import sys
from urllib.parse import parse_qs, urlparse

class MockC64Server(BaseHTTPRequestHandler):
    # Simulated C64 memory
    memory = bytearray(65536)
    # Optional network password (set to None to disable authentication)
    password = None

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
            self.handle_run_prg()
        elif path.path.startswith("/v1/drives/") and path.path.endswith(":mount"):
            self.handle_mount_disk(path.query)
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
        self.log_message(f"SIDPLAY song={song_number}")
        self.send_json_response({"status": "ok"})

    def handle_run_prg(self):
        """Handle PRG execution"""
        content_length = int(self.headers.get('Content-Length', 0))
        prg_data = self.rfile.read(content_length)
        self.log_message(f"RUN_PRG size={len(prg_data)}")
        self.send_json_response({"status": "ok"})

    def handle_mount_disk(self, query):
        """Handle disk mount"""
        params = parse_qs(query)
        disk_type = params.get('type', ['d64'])[0]
        mode = params.get('mode', ['readonly'])[0]
        content_length = int(self.headers.get('Content-Length', 0))
        disk_data = self.rfile.read(content_length)
        self.log_message(f"MOUNT_DISK type={disk_type} mode={mode} size={len(disk_data)}")
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
