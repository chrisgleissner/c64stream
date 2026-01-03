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
    
    def log_message(self, format, *args):
        """Override to provide cleaner logging"""
        sys.stderr.write(f"[MockC64] {format % args}\n")
    
    def do_GET(self):
        """Handle GET requests"""
        path = urlparse(self.path)
        
        if path.path == "/v1/machine:readmem":
            self.handle_read_memory(path.query)
        else:
            self.send_error(404, f"Not found: {path.path}")
    
    def do_PUT(self):
        """Handle PUT requests"""
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
        path = urlparse(self.path)
        
        if path.path == "/v1/runners:sidplay":
            self.handle_sidplay(path.query)
        elif path.path == "/v1/runners:run_prg":
            self.handle_run_prg()
        elif path.path.startswith("/v1/drives/") and path.path.endswith(":mount"):
            self.handle_mount_disk(path.query)
        else:
            self.send_error(404, f"Not found: {path.path}")
    
    def handle_reset(self):
        """Handle machine reset"""
        self.log_message("RESET machine")
        # Initialize keyboard buffer to empty
        self.memory[0x00C6] = 0
        for i in range(10):
            self.memory[0x0277 + i] = 0
        self.send_response(200)
        self.end_headers()
    
    def handle_reboot(self):
        """Handle machine reboot"""
        self.log_message("REBOOT machine")
        # Clear all memory
        self.memory = bytearray(65536)
        self.send_response(200)
        self.end_headers()
    
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
        
        # Write to memory
        for i, byte in enumerate(data):
            self.memory[address + i] = byte
        
        self.log_message(f"WRITE memory ${address:04X} data={hex_data}")
        
        # Simulate keyboard buffer consumption
        if address == 0x0277:
            # After writing to keyboard buffer, simulate gradual consumption
            # In real C64, KERNAL would process these keys
            pass
        
        self.send_response(200)
        self.end_headers()
    
    def handle_sidplay(self, query):
        """Handle SID playback"""
        params = parse_qs(query)
        song_number = params.get('songnr', ['0'])[0]
        self.log_message(f"SIDPLAY song={song_number}")
        self.send_response(200)
        self.end_headers()
    
    def handle_run_prg(self):
        """Handle PRG execution"""
        content_length = int(self.headers.get('Content-Length', 0))
        prg_data = self.rfile.read(content_length)
        self.log_message(f"RUN_PRG size={len(prg_data)}")
        self.send_response(200)
        self.end_headers()
    
    def handle_mount_disk(self, query):
        """Handle disk mount"""
        params = parse_qs(query)
        disk_type = params.get('type', ['d64'])[0]
        mode = params.get('mode', ['readonly'])[0]
        content_length = int(self.headers.get('Content-Length', 0))
        disk_data = self.rfile.read(content_length)
        self.log_message(f"MOUNT_DISK type={disk_type} mode={mode} size={len(disk_data)}")
        self.send_response(200)
        self.end_headers()

def run_server(port=8064):
    """Run the mock server"""
    server_address = ('', port)
    httpd = HTTPServer(server_address, MockC64Server)
    print(f"Mock C64U server running on port {port}")
    print(f"Base URL: http://localhost:{port}")
    print("Press Ctrl+C to stop")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        httpd.shutdown()

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8064
    run_server(port)
