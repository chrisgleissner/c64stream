#!/usr/bin/env python3
"""start-disk.py

Upload a disk image to the Ultimate REST API, mount it, and
inject autostart keystrokes via the KERNAL keyboard buffer.

Supported disk types: .d64 .g64 .d71 .g71 .d81
"""

from __future__ import annotations

import argparse
import os
import sys
import time
import urllib.parse
import urllib.request
import uuid
from dataclasses import dataclass
from typing import Dict, Optional


DEFAULT_BASE_URL = "http://c64u"
DEFAULT_PASSWORD = os.environ.get("C64U_PASSWORD", "")

KERNAL_KBD_COUNT_ADDR = 0x00C6
KERNAL_KBD_BUFFER_ADDR = 0x0277
KERNAL_KBD_BUFFER_SIZE = 10

DEFAULT_POLL_INTERVAL_S = 0.05
DEFAULT_CONSUME_TIMEOUT_S = 10.0
DEFAULT_RESET_DELAY_S = 0.5

DEFAULT_AUTOSTART_TEMPLATE = "LOAD\"*\",8,1\rRUN\r"

VOLUME_EXTENSIONS = {
    ".d64": "d64",
    ".g64": "g64",
    ".d71": "d71",
    ".g71": "g71",
    ".d81": "d81",
}


@dataclass(frozen=True)
class HttpResponse:
    status: int
    data: bytes


class RestError(RuntimeError):
    pass


def _http_request(
    *,
    base_url: str,
    password: str,
    method: str,
    path: str,
    query: Optional[Dict[str, str]] = None,
    body: Optional[bytes] = None,
    headers: Optional[Dict[str, str]] = None,
    timeout_s: float = 5.0,
) -> HttpResponse:
    url = base_url.rstrip("/") + path
    if query:
        url = url + "?" + urllib.parse.urlencode(query)

    req = urllib.request.Request(url, method=method, data=body)
    if password:
        req.add_header("X-Password", password)
    if headers:
        for key, value in headers.items():
            req.add_header(key, value)

    with urllib.request.urlopen(req, timeout=timeout_s) as resp:
        return HttpResponse(status=resp.status, data=resp.read())


def read_mem(*, base_url: str, password: str, address: int, length: int, timeout_s: float) -> bytes:
    resp = _http_request(
        base_url=base_url,
        password=password,
        method="GET",
        path="/v1/machine:readmem",
        query={"address": f"{address:04X}", "length": str(int(length))},
        timeout_s=timeout_s,
    )
    if resp.status != 200:
        raise RestError(f"readmem HTTP {resp.status}")
    return resp.data


def write_mem(*, base_url: str, password: str, address: int, data_bytes: bytes, timeout_s: float) -> None:
    resp = _http_request(
        base_url=base_url,
        password=password,
        method="PUT",
        path="/v1/machine:writemem",
        query={"address": f"{address:04X}", "data": data_bytes.hex().upper()},
        timeout_s=timeout_s,
    )
    if resp.status != 200:
        raise RestError(f"writemem HTTP {resp.status}")


def _wait_for_kbd_count(
    *,
    base_url: str,
    password: str,
    expected: int,
    timeout_s: float,
    poll_interval_s: float,
) -> None:
    deadline = time.monotonic() + timeout_s
    while True:
        data = read_mem(
            base_url=base_url,
            password=password,
            address=KERNAL_KBD_COUNT_ADDR,
            length=1,
            timeout_s=timeout_s,
        )
        if data and data[0] == (expected & 0xFF):
            return
        if time.monotonic() >= deadline:
            last = data[0] if data else None
            raise TimeoutError(f"Timeout waiting for $00C6 == {expected} (last={last})")
        time.sleep(poll_interval_s)


def inject_text(
    *,
    base_url: str,
    password: str,
    text: str,
    poll_interval_s: float,
    consume_timeout_s: float,
) -> None:
    data = text.encode("latin1")
    offset = 0
    while offset < len(data):
        chunk = data[offset : offset + KERNAL_KBD_BUFFER_SIZE]
        _wait_for_kbd_count(
            base_url=base_url,
            password=password,
            expected=0,
            timeout_s=consume_timeout_s,
            poll_interval_s=poll_interval_s,
        )
        write_mem(
            base_url=base_url,
            password=password,
            address=KERNAL_KBD_BUFFER_ADDR,
            data_bytes=chunk,
            timeout_s=consume_timeout_s,
        )
        write_mem(
            base_url=base_url,
            password=password,
            address=KERNAL_KBD_COUNT_ADDR,
            data_bytes=bytes([len(chunk)]),
            timeout_s=consume_timeout_s,
        )
        _wait_for_kbd_count(
            base_url=base_url,
            password=password,
            expected=0,
            timeout_s=consume_timeout_s,
            poll_interval_s=poll_interval_s,
        )
        offset += len(chunk)


def _build_multipart_file(*, field: str, filename: str, payload: bytes) -> tuple[bytes, str]:
    boundary = f"----c64stream-{uuid.uuid4().hex}"
    headers = (
        f"--{boundary}\r\n"
        f"Content-Disposition: form-data; name=\"{field}\"; filename=\"{filename}\"\r\n"
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode("utf-8")
    trailer = f"\r\n--{boundary}--\r\n".encode("utf-8")
    body = headers + payload + trailer
    return body, boundary


def mount_volume(
    *,
    base_url: str,
    password: str,
    drive: str,
    volume_type: str,
    mode: str,
    payload: bytes,
    timeout_s: float,
) -> None:
    body, boundary = _build_multipart_file(field="file", filename=f"disk.{volume_type}", payload=payload)
    resp = _http_request(
        base_url=base_url,
        password=password,
        method="POST",
        path=f"/v1/drives/{drive}:mount",
        query={"type": volume_type, "mode": mode},
        body=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
        timeout_s=timeout_s,
    )
    if resp.status != 200:
        raise RestError(f"mount HTTP {resp.status}")


def reset_machine(*, base_url: str, password: str, timeout_s: float) -> None:
    resp = _http_request(
        base_url=base_url,
        password=password,
        method="PUT",
        path="/v1/machine:reset",
        timeout_s=timeout_s,
    )
    if resp.status != 200:
        raise RestError(f"reset HTTP {resp.status}")


def _volume_type_from_path(path: str) -> str:
    ext = os.path.splitext(path)[1].lower()
    if ext not in VOLUME_EXTENSIONS:
        raise ValueError(f"Unsupported volume type '{ext}'. Supported: {', '.join(VOLUME_EXTENSIONS)}")
    return VOLUME_EXTENSIONS[ext]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Upload and autostart a disk image via Ultimate REST API",
    )
    ap.add_argument("path", help="Path to .d64/.g64/.d71/.g71/.d81 file")
    ap.add_argument("--base-url", default=DEFAULT_BASE_URL, help="Base REST URL (default: http://c64u)")
    ap.add_argument("--password", default=DEFAULT_PASSWORD, help="REST password (or env C64U_PASSWORD)")
    ap.add_argument("--drive", default="a", help="Drive letter (default: a)")
    ap.add_argument("--mode", default="readwrite", help="Mount mode: readonly|readwrite|unlinked")
    ap.add_argument("--autostart", default=DEFAULT_AUTOSTART_TEMPLATE, help="Autostart template string")
    ap.add_argument("--poll-interval", type=float, default=DEFAULT_POLL_INTERVAL_S, help="Poll interval (seconds)")
    ap.add_argument("--consume-timeout", type=float, default=DEFAULT_CONSUME_TIMEOUT_S, help="Consume timeout (seconds)")
    ap.add_argument("--reset", action="store_true", help="Reset machine before mounting")
    ap.add_argument("--reset-delay", type=float, default=DEFAULT_RESET_DELAY_S, help="Delay after reset (seconds)")
    ap.add_argument("--retries", type=int, default=3, help="Retry attempts on failure")
    ap.add_argument("--retry-delay", type=float, default=0.5, help="Delay between retries (seconds)")

    args = ap.parse_args(argv)

    if not os.path.isfile(args.path):
        print(f"error: file not found: {args.path}", file=sys.stderr)
        return 2

    try:
        volume_type = _volume_type_from_path(args.path)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    with open(args.path, "rb") as f:
        payload = f.read()

    for attempt in range(1, max(args.retries, 1) + 1):
        try:
            if args.reset:
                reset_machine(base_url=args.base_url, password=args.password, timeout_s=5.0)
                time.sleep(max(args.reset_delay, 0.0))

            mount_volume(
                base_url=args.base_url,
                password=args.password,
                drive=args.drive,
                volume_type=volume_type,
                mode=args.mode,
                payload=payload,
                timeout_s=10.0,
            )

            inject_text(
                base_url=args.base_url,
                password=args.password,
                text=args.autostart,
                poll_interval_s=max(args.poll_interval, 0.0),
                consume_timeout_s=max(args.consume_timeout, 0.1),
            )

            print("OK: mounted and autostart injected")
            return 0
        except Exception as exc:
            if attempt >= args.retries:
                print(f"error: {exc}", file=sys.stderr)
                return 1
            time.sleep(max(args.retry_delay, 0.0))

    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
