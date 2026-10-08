"""Minimal read-only FTP server for the mock Ultimate: serves the palette
files in /Flash/data that the plugin's Follow device mode downloads.

Supports what libcurl sends for a single download: USER, PASS, PWD, CWD,
TYPE, EPSV, PASV, SIZE, RETR and QUIT. Like the device, any user name is
accepted and the password must match the network password (empty: none)."""

from __future__ import annotations

import logging
import socket
import threading
from typing import Callable, Optional

logger = logging.getLogger(__name__)


class MockFtpServer:
    def __init__(self, files: Callable[[], dict[str, bytes]], password: str = "", port: int = 21,
                 bind_ip: str = "0.0.0.0"):
        self.files = files
        self.password = password
        self.port = port
        self.bind_ip = bind_ip
        self.retrievals: list[str] = []
        self._lock = threading.Lock()
        self._socket: Optional[socket.socket] = None
        self._running = False

    def start(self) -> bool:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind((self.bind_ip, self.port))
            sock.listen(8)
            sock.settimeout(0.5)
            self.port = sock.getsockname()[1]  # the chosen port when 0 was given
        except OSError as error:
            logger.error(f"❌ Failed to start mock FTP server on port {self.port}: {error}")
            return False
        self._socket = sock
        self._running = True
        threading.Thread(target=self._accept_loop, name="mock-c64u-ftp", daemon=True).start()
        logger.info(f"✅ Mock FTP server started on port {self.port}")
        return True

    def stop(self) -> None:
        self._running = False
        if self._socket:
            try:
                self._socket.close()
            except OSError:
                pass

    def _accept_loop(self) -> None:
        while self._running:
            try:
                conn, _ = self._socket.accept()
            except (socket.timeout, OSError):
                continue
            threading.Thread(target=self._session, args=(conn,), daemon=True).start()

    def _session(self, conn: socket.socket) -> None:
        conn.settimeout(10)
        reader = conn.makefile("rb")
        passive: Optional[socket.socket] = None

        def reply(text: str) -> None:
            conn.sendall((text + "\r\n").encode("utf-8"))

        try:
            reply("220 Mock Ultimate FTP")
            logged_in = False
            for raw in reader:
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                verb, _, arg = line.partition(" ")
                verb = verb.upper()
                if verb == "USER":
                    reply("331 Password required")
                elif verb == "PASS":
                    logged_in = not self.password or arg == self.password
                    reply("230 Logged in" if logged_in else "530 Login incorrect")
                elif verb == "QUIT":
                    reply("221 Bye")
                    break
                elif not logged_in:
                    reply("530 Not logged in")
                elif verb == "PWD":
                    reply('257 "/"')
                elif verb in ("CWD", "TYPE"):
                    reply("200 OK")
                elif verb in ("EPSV", "PASV"):
                    if passive:
                        passive.close()
                    passive = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    passive.bind((conn.getsockname()[0], 0))
                    passive.listen(1)
                    passive.settimeout(5)
                    port = passive.getsockname()[1]
                    if verb == "EPSV":
                        reply(f"229 Entering Extended Passive Mode (|||{port}|)")
                    else:
                        host = conn.getsockname()[0].replace(".", ",")
                        reply(f"227 Entering Passive Mode ({host},{port >> 8},{port & 0xFF})")
                elif verb in ("SIZE", "RETR"):
                    name = arg.rsplit("/", 1)[-1]
                    data = self.files().get(name)
                    if data is None:
                        reply("550 File not found")
                    elif verb == "SIZE":
                        reply(f"213 {len(data)}")
                    elif not passive:
                        reply("425 Use PASV first")
                    else:
                        reply("150 Opening data connection")
                        try:
                            data_conn, _ = passive.accept()
                            with data_conn:
                                data_conn.sendall(data)
                        except OSError:
                            reply("426 Transfer aborted")
                            continue
                        with self._lock:
                            self.retrievals.append(name)
                        reply("226 Transfer complete")
                else:
                    reply("502 Not implemented")
        except OSError:
            pass
        finally:
            if passive:
                passive.close()
            reader.close()
            conn.close()
