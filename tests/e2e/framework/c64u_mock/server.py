from __future__ import annotations
import json
import socket
import threading
import time
import logging
import queue
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs
from typing import Any, Tuple, Optional

from ..environment import Environment

logger = logging.getLogger(__name__)

class MockC64UServer:
    """Mock C64 Ultimate device. Always serves the legacy TCP control protocol
    on control_port (port 64 on real hardware); optionally also serves the
    REST API (stream start/stop, memory read/write, machine:input) on
    rest_port (port 80 on real hardware) when rest_port is given, so tests
    can exercise either transport against the same simulated device."""

    def __init__(self, env: Environment, control_port: int = 6400, rest_port: Optional[int] = None,
                 product: str = "Ultimate 64", hostname: str = "MockC64U", unique_id: str = "000000",
                 devices: Optional[list[dict[str, Any]]] = None):
        self.env = env
        self.control_port = control_port
        self.control_bind_ip = "0.0.0.0"
        self.rest_port = rest_port
        self.product = product
        self.hostname = hostname
        self.unique_id = unique_id
        # A topology maps every advertised address to one physical device.
        # A C64U may expose more than one address; stream_hosts identifies the
        # addresses that can actually deliver UDP A/V (normally Ethernet).
        self.devices_by_host: dict[str, dict[str, Any]] = {}
        self.device_states: dict[str, dict[str, Any]] = {}
        for index, device in enumerate(devices or []):
            device_id = str(device.get("id") or device.get("unique_id") or f"mock-{index}")
            copied = dict(device)
            copied["id"] = device_id
            copied.setdefault("product", product)
            copied.setdefault("hostname", device_id)
            copied.setdefault("unique_id", device_id)
            copied["stream_hosts"] = set(copied.get("stream_hosts", copied.get("hosts", [])))
            for host in copied.get("hosts", []):
                self.devices_by_host[str(host)] = copied
            self.device_states[device_id] = {"mask": 0, "video_dest": None, "audio_dest": None}
        self.stream_requests: queue.Queue[tuple[dict[str, Any], Tuple[str, int], Tuple[str, int], str]] = queue.Queue()

        self.running = False
        self.server_socket: Optional[socket.socket] = None
        self.server_thread: Optional[threading.Thread] = None
        self.rest_server: Optional[ThreadingHTTPServer] = None
        self.rest_thread: Optional[threading.Thread] = None

        self.video_dest: Optional[Tuple[str, int]] = None
        self.audio_dest: Optional[Tuple[str, int]] = None

        self._trigger_event = threading.Event()
        self._stream_start_mask = 0
        self._stream_start_lock = threading.Lock()

        # Ordered ("start"|"stop", stream_id) log, used by tests that need to
        # verify exactly which control commands this device instance received
        # (e.g. a device-switch test running two independent mock instances),
        # shared between the TCP and REST transports.
        self.events: list[Tuple[str, int]] = []
        self._events_lock = threading.Lock()

        # REST-only simulated state: a per-instance memory image (so multiple
        # mock instances never share state) backing machine:readmem/writemem,
        # and a log of raw machine:input request bodies for keyboard tests.
        self.memory = bytearray(65536)
        self.keyboard_events: list[Any] = []

    def start(self):
        """Start the TCP control server, and the REST server if configured."""
        logger.info(f"Starting mock C64 Ultimate TCP server on port {self.control_port}")

        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((self.control_bind_ip, self.control_port))
            self.server_socket.listen(5)

            self.running = True
            self.server_thread = threading.Thread(target=self._server_worker, name="mock-c64u-server")
            self.server_thread.daemon = True
            self.server_thread.start()

            # CI fallback for port 64 if needed (logic from e2e.py)
            if self.env.is_ci and self.control_port != 64:
                self._start_fallback_listener()

            logger.info("✅ Mock C64 Ultimate TCP server started")
        except Exception as e:
            logger.error(f"❌ Failed to start mock C64 Ultimate TCP server: {e}")
            return False

        if self.rest_port is not None:
            if not self._start_rest_server():
                return False

        return True

    def _start_rest_server(self) -> bool:
        logger.info(f"Starting mock C64 Ultimate REST server on port {self.rest_port}")
        try:
            handler_cls = self._build_rest_handler()
            self.rest_server = ThreadingHTTPServer(("0.0.0.0", self.rest_port), handler_cls)
        except Exception as e:
            logger.error(f"❌ Failed to start mock C64 Ultimate REST server: {e}")
            return False

        self.rest_thread = threading.Thread(target=self.rest_server.serve_forever, name="mock-c64u-rest")
        self.rest_thread.daemon = True
        self.rest_thread.start()
        logger.info("✅ Mock C64 Ultimate REST server started")
        return True

    def _start_fallback_listener(self):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind((self.control_bind_ip, 64))
            sock.listen(3)

            t = threading.Thread(target=self._socket_worker, args=(sock, "alt-64"), name="mock-c64u-fallback")
            t.daemon = True
            t.start()
            logger.info("ℹ️ CI fallback control listener active on port 64")
        except Exception:
            pass

    def stop(self):
        """Stop the TCP and REST servers."""
        self.running = False
        if self.server_socket:
            try:
                self.server_socket.close()
            except Exception:
                pass
        if self.rest_server:
            try:
                self.rest_server.shutdown()
                self.rest_server.server_close()
            except Exception:
                pass
        # Threads are daemon, will exit naturally

    def wait_for_trigger(self, timeout: float = 30) -> bool:
        """Wait for streaming request from plugin."""
        return self._trigger_event.wait(timeout=timeout)

    def _server_worker(self):
        self._socket_worker(self.server_socket, "primary")

    def _socket_worker(self, sock: socket.socket, label: str):
        logger.info(f"TCP server worker ({label}) started")
        while self.running:
            try:
                sock.settimeout(1.0)
                conn, addr = sock.accept()
                t = threading.Thread(target=self._handle_connection, args=(conn, addr))
                t.daemon = True
                t.start()
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    logger.error(f"TCP server ({label}) error: {e}")
                break

    def _handle_connection(self, conn: socket.socket, addr):
        logger.info(f"🔍 TCP connection received from {addr}")
        try:
            conn.settimeout(1.0)
            deadline = time.monotonic() + 5.0

            # Simple buffering handling
            buffer = bytearray()

            while time.monotonic() < deadline:
                try:
                    chunk = conn.recv(1024)
                    if not chunk:
                        break
                    buffer.extend(chunk)

                    # Process buffer
                    # A command is at least 4 bytes: [cmd][FF][len][00]...
                    while len(buffer) >= 4:
                        if buffer[1] != 0xFF:
                            # Not a valid start, finding next FF? Or just discard byte 0
                            buffer.pop(0)
                            continue

                        param_len = buffer[2]
                        if len(buffer) < 4 + param_len:
                            break # Wait for more data

                        # Extract command
                        cmd_data = buffer[:4+param_len]
                        del buffer[:4+param_len]

                        self._process_command(cmd_data, conn.getsockname()[0])

                except socket.timeout:
                    continue
                except Exception:
                    break
        except Exception as e:
            logger.error(f"Connection handler error: {e}")
        finally:
            conn.close()

    def _device_for_host(self, host: str) -> dict[str, Any]:
        return self.devices_by_host.get(host, {
            "id": self.unique_id,
            "product": self.product,
            "hostname": self.hostname,
            "unique_id": self.unique_id,
            "stream_hosts": {host},
        })

    def _process_command(self, cmd: bytearray, local_host: str):
        # cmd[0] is command byte
        cmd_byte = cmd[0]
        stream_id = cmd_byte & 0x0F
        is_start = (cmd_byte & 0xF0) == 0x20
        is_stop = (cmd_byte & 0xF0) == 0x30

        if is_stop:
            logger.info(f"🛑 Received legacy STOP command for stream {stream_id}")
            self._record_stop(stream_id)
            return

        if is_start:
            logger.info(f"✅ Received legacy START command for stream {stream_id}")
            # payload starts at index 4 (header is 4 bytes: CMD, FF, LEN, 00)
            # Actually e2e.py said:
            # Format: [command_byte][0xFF][param_len][0x00][param_bytes...]
            # param_bytes has duration (2 bytes) + dest string.
            # So dest starts at 6?
            # e2e.py: `if len(cmd) >= 6 + (param_len - 2): dest_str = cmd[6 : ...]`
            # This implies header is 4 bytes, payload at 4. Payload has 2 bytes duration then string.
            # So dest string starts at index 6 of the WHOLE cmd. Yes.

            dest_str = None
            param_len = cmd[2]
            if param_len > 2:
                try:
                    dest_str = cmd[6:].decode("ascii", errors="ignore")
                except Exception:
                    dest_str = None

            self._record_start(stream_id, dest_str, self._device_for_host(local_host), local_host)

    def _record_start(self, stream_id: int, dest_str: Optional[str], device: Optional[dict[str, Any]] = None,
                      local_host: Optional[str] = None):
        """Shared by both transports: record a start event, capture the
        destination, and fire the trigger once both streams have started."""
        state = self.device_states.setdefault((device or {}).get("id", self.unique_id),
                                              {"mask": 0, "video_dest": None, "audio_dest": None})
        if dest_str:
            logger.info(f"Stream destination: {dest_str}")
            if ":" in dest_str:
                try:
                    ip, port_s = dest_str.split(":", 1)
                    port = int(port_s)
                    if stream_id == 0:
                        self.video_dest = (ip, port)
                        state["video_dest"] = (ip, port)
                    elif stream_id == 1:
                        self.audio_dest = (ip, port)
                        state["audio_dest"] = (ip, port)
                except Exception:
                    pass

        with self._events_lock:
            self.events.append(("start", stream_id))

        with self._stream_start_lock:
            if stream_id == 0:
                self._stream_start_mask |= 0x1
            elif stream_id == 1:
                self._stream_start_mask |= 0x2

            # Trigger only when BOTH video (0x1) and audio (0x2) have started.
            # Starting replay early can create artificial A/V offset in the recording.
            if self._stream_start_mask == 0x3 and (not self.devices_by_host or
                                                  local_host in (device or {}).get("stream_hosts", set())):
                self._trigger_event.set()
            if stream_id == 0:
                state["mask"] |= 0x1
            elif stream_id == 1:
                state["mask"] |= 0x2
            if state["mask"] == 0x3 and local_host in (device or {}).get("stream_hosts", set()):
                video_dest = state.get("video_dest")
                audio_dest = state.get("audio_dest")
                if video_dest and audio_dest:
                    self.stream_requests.put((device, video_dest, audio_dest, local_host))
                state["mask"] = 0

    def _record_stop(self, stream_id: int):
        with self._events_lock:
            self.events.append(("stop", stream_id))

    # ------------------------------------------------------------------
    # REST API (port 80 on real hardware)
    # ------------------------------------------------------------------

    def _build_rest_handler(self):
        """Returns a BaseHTTPRequestHandler class bound to this instance via
        closure, so each MockC64UServer has its own isolated memory/state
        instead of sharing it through handler class attributes."""
        mock = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, fmt, *args):
                logger.debug("mock-c64u-rest: " + (fmt % args))

            def _send_json(self, data: dict, status: int = 200):
                body = json.dumps({"errors": [], **data}).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                path = urlparse(self.path)
                if path.path == "/v1/info":
                    device = mock._device_for_host(self.connection.getsockname()[0])
                    self._send_json({
                        "product": device["product"],
                        "hostname": device["hostname"],
                        "unique_id": device["unique_id"],
                        "firmware_version": "9.9",
                    })
                elif path.path == "/v1/machine:readmem":
                    params = parse_qs(path.query)
                    address = int(params.get("address", ["0"])[0], 16)
                    length = int(params.get("length", ["1"])[0])
                    # Real hardware's KERNAL consumes the keyboard buffer as
                    # soon as it's polled; simulate instant consumption so
                    # sequential batches don't stall waiting for it to clear.
                    data = bytes(mock.memory[address:address + length])
                    if address == 0x00C6 and length == 1:
                        mock.memory[address] = 0
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                else:
                    self.send_error(404, f"Not found: {path.path}")

            def do_PUT(self):
                path = urlparse(self.path)
                if path.path in ("/v1/streams/video:start", "/v1/streams/audio:start"):
                    stream_id = 1 if path.path.startswith("/v1/streams/audio") else 0
                    params = parse_qs(path.query)
                    dest = params.get("ip", [None])[0]
                    local_host = self.connection.getsockname()[0]
                    mock._record_start(stream_id, dest, mock._device_for_host(local_host), local_host)
                    self._send_json({})
                elif path.path in ("/v1/streams/video:stop", "/v1/streams/audio:stop"):
                    stream_id = 1 if path.path.startswith("/v1/streams/audio") else 0
                    mock._record_stop(stream_id)
                    self._send_json({})
                elif path.path == "/v1/machine:writemem":
                    params = parse_qs(path.query)
                    address = int(params.get("address", ["0"])[0], 16)
                    hex_data = params.get("data", [""])[0]
                    data = bytes.fromhex(hex_data) if hex_data else b""
                    if len(data) > 128:
                        self._send_json({"errors": ["Data exceeds 128 byte limit"]}, 400)
                        return
                    for i, byte in enumerate(data):
                        mock.memory[address + i] = byte
                    self._send_json({})
                else:
                    self.send_error(404, f"Not found: {path.path}")

            def do_POST(self):
                path = urlparse(self.path)
                if path.path == "/v1/machine:input":
                    length = int(self.headers.get("Content-Length", 0) or 0)
                    body = self.rfile.read(length) if length else b""
                    try:
                        payload = json.loads(body.decode("utf-8")) if body else {}
                    except Exception:
                        payload = {"raw": body.decode("utf-8", errors="replace")}
                    with mock._events_lock:
                        mock.keyboard_events.append(payload)
                    self._send_json({})
                else:
                    self.send_error(404, f"Not found: {path.path}")

        return Handler
