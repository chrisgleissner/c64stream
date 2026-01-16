#!/usr/bin/env python3
"""Send a SID with optional songlengths .ssl payload to the C64U REST API.

Usage:
  ./util/send_sid_songlengths.py path/to/file.sid --length-seconds 123
"""

from __future__ import annotations

import argparse
import base64
import http.client
import os
import sys
import uuid
from typing import Tuple
from urllib.parse import urlparse


def parse_sid_subsongs(sid_bytes: bytes) -> int:
    if len(sid_bytes) >= 0x10 and (sid_bytes[:4] in (b"PSID", b"RSID")):
        subsongs = (sid_bytes[0x0E] << 8) | sid_bytes[0x0F]
        return subsongs if subsongs > 0 else 1
    return 1


def build_songlength_payload(length_seconds: int, subsongs: int) -> bytes:
    minutes = max(0, min(99, length_seconds // 60))
    seconds = max(0, min(59, length_seconds % 60))
    bcd_minutes = ((minutes // 10) << 4) | (minutes % 10)
    bcd_seconds = ((seconds // 10) << 4) | (seconds % 10)
    return bytes([bcd_minutes, bcd_seconds]) * subsongs


def build_multipart(sid_path: str, sid_bytes: bytes, songlengths: bytes | None) -> Tuple[str, bytes]:
    boundary = f"----c64stream-{uuid.uuid4().hex}"
    parts: list[bytes] = []

    def add_part(name: str, filename: str, content_type: str, data: bytes) -> None:
        header = (
            f"--{boundary}\r\n"
            f"Content-Disposition: form-data; name=\"{name}\"; filename=\"{filename}\"\r\n"
            f"Content-Type: {content_type}\r\n\r\n"
        ).encode("ascii")
        parts.append(header + data + b"\r\n")

    add_part("file", os.path.basename(sid_path) or "music.sid", "application/octet-stream", sid_bytes)
    if songlengths:
        add_part("file", "songlengths.ssl", "application/octet-stream", songlengths)

    parts.append(f"--{boundary}--\r\n".encode("ascii"))
    body = b"".join(parts)
    return boundary, body


def log_request(method: str, path: str, headers: dict, body: bytes) -> None:
    print("=== REQUEST ===")
    print(f"{method} {path}")
    for key, value in headers.items():
        print(f"{key}: {value}")
    print("--- BODY (latin-1) ---")
    print(body.decode("latin-1", errors="replace"))
    print("--- BODY (hex preview) ---")
    preview = body[:256]
    print(preview.hex(" "))
    print("--- BODY (sid base64 preview) ---")
    print(base64.b64encode(preview).decode("ascii"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Send SID with optional songlengths .ssl payload.")
    parser.add_argument("sid", help="Path to SID file")
    parser.add_argument("--length-seconds", type=int, default=0, help="Song length in seconds (optional)")
    parser.add_argument("--songnr", type=int, default=0, help="Song number (0 for default)")
    parser.add_argument("--base-url", default="http://c64u", help="Base URL (default: http://c64u)")
    parser.add_argument("--password", default="", help="Optional X-Password header")
    args = parser.parse_args()

    if not os.path.isfile(args.sid):
        print(f"SID file not found: {args.sid}", file=sys.stderr)
        return 2

    with open(args.sid, "rb") as f:
        sid_bytes = f.read()

    subsongs = parse_sid_subsongs(sid_bytes)

    songlengths = None
    if args.length_seconds > 0:
        songlengths = build_songlength_payload(args.length_seconds, subsongs)

    boundary, body = build_multipart(args.sid, sid_bytes, songlengths)

    parsed = urlparse(args.base_url)
    if not parsed.scheme or not parsed.netloc:
        print(f"Invalid base URL: {args.base_url}", file=sys.stderr)
        return 2

    path = f"/v1/runners:sidplay?songnr={args.songnr}"
    headers = {
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(body)),
    }
    if args.password:
        headers["X-Password"] = args.password

    log_request("POST", path, headers, body)

    if parsed.scheme == "https":
        conn = http.client.HTTPSConnection(parsed.netloc)
    else:
        conn = http.client.HTTPConnection(parsed.netloc)

    conn.request("POST", path, body=body, headers=headers)
    resp = conn.getresponse()
    resp_body = resp.read()
    print("=== RESPONSE ===")
    print(f"Status: {resp.status} {resp.reason}")
    for key, value in resp.getheaders():
        print(f"{key}: {value}")
    print(resp_body.decode("utf-8", errors="replace"))
    conn.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
