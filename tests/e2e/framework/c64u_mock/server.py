from __future__ import annotations
import socket
import threading
import time
import logging
from typing import Tuple, Optional

from ..environment import Environment

logger = logging.getLogger(__name__)

class MockC64UServer:
    """Mock C64 Ultimate TCP server that listens for plugin connections."""

    def __init__(self, env: Environment, control_port: int = 6400):
        self.env = env
        self.control_port = control_port
        self.control_bind_ip = "0.0.0.0"

        self.running = False
        self.server_socket: Optional[socket.socket] = None
        self.server_thread: Optional[threading.Thread] = None

        self.video_dest: Optional[Tuple[str, int]] = None
        self.audio_dest: Optional[Tuple[str, int]] = None

        self._trigger_event = threading.Event()
        self._stream_start_mask = 0
        self._stream_start_lock = threading.Lock()

    def start(self):
        """Start the TCP server."""
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
            return True
        except Exception as e:
            logger.error(f"❌ Failed to start mock C64 Ultimate TCP server: {e}")
            return False

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
        """Stop the TCP server."""
        self.running = False
        if self.server_socket:
            try:
                self.server_socket.close()
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

                        self._process_command(cmd_data)

                except socket.timeout:
                    continue
                except Exception:
                    break
        except Exception as e:
            logger.error(f"Connection handler error: {e}")
        finally:
            conn.close()

    def _process_command(self, cmd: bytearray):
        # cmd[0] is command byte
        cmd_byte = cmd[0]
        stream_id = cmd_byte & 0x0F
        is_start = (cmd_byte & 0xF0) == 0x20

        if is_start:
            logger.info(f"✅ Received START command for stream {stream_id}")
            # payload starts at index 4 (header is 4 bytes: CMD, FF, LEN, 00)
            # Actually e2e.py said:
            # Format: [command_byte][0xFF][param_len][0x00][param_bytes...]
            # param_bytes has duration (2 bytes) + dest string.
            # So dest starts at 6?
            # e2e.py: `if len(cmd) >= 6 + (param_len - 2): dest_str = cmd[6 : ...]`
            # This implies header is 4 bytes, payload at 4. Payload has 2 bytes duration then string.
            # So dest string starts at index 6 of the WHOLE cmd. Yes.

            param_len = cmd[2]
            if param_len > 2:
                try:
                    dest_str = cmd[6:].decode("ascii", errors="ignore")
                    logger.info(f"Stream destination: {dest_str}")

                    if ":" in dest_str:
                        ip, port_s = dest_str.split(":", 1)
                        port = int(port_s)
                        if stream_id == 0:
                            self.video_dest = (ip, port)
                        elif stream_id == 1:
                            self.audio_dest = (ip, port)
                except Exception:
                    pass

            with self._stream_start_lock:
                if stream_id == 0: self._stream_start_mask |= 0x1
                elif stream_id == 1: self._stream_start_mask |= 0x2

                if self._stream_start_mask == 0x3:
                    self._trigger_event.set()
