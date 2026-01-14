#!/usr/bin/env python3
"""send-petscii.py

Injects a PETSCII byte range using the same mechanism as the plugin:

- REST DMA write → KERNAL keyboard buffer ($0277..$0280)
- Backpressure via polling keyboard buffer length at $00C6

This avoids writing directly to screen RAM and ensures we only send data when
the C64 has consumed the previous chunk.

Usage (always hex):
    tools/send-petscii.py 60-80   # inject $60..$7F
    tools/send-petscii.py 6E      # inject single byte $6E

The range is half-open: $60..$7F.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Dict, Iterable, List


# Per user request: fixed host name.
DEFAULT_BASE_URL = "http://c64u"

DEFAULT_PASSWORD = os.environ.get("C64U_PASSWORD", "")

KERNAL_KBD_COUNT_ADDR = 0x00C6
KERNAL_KBD_BUFFER_ADDR = 0x0277
KERNAL_KBD_BUFFER_SIZE = 10

DEFAULT_POLL_INTERVAL_S = 0.05
DEFAULT_CONSUME_TIMEOUT_S = 10.0


@dataclass(frozen=True)
class RangeSpec:
    start: int
    end: int


def _parse_int_hex(value: str) -> int:
    value = value.strip().lower()
    if value.startswith("$"):
        value = value[1:]
        return int(value, 16)
    if value.startswith("0x"):
        return int(value, 16)
    # Always hex (per request)
    return int(value, 16)


def parse_range(spec: str) -> RangeSpec:
    spec = spec.strip()
    if "-" not in spec:
        raise ValueError(f"Invalid range '{spec}'. Expected form like 60-80")
    left, right = spec.split("-", 1)
    start = _parse_int_hex(left)
    end = _parse_int_hex(right)
    if not (0 <= start <= 0xFF and 0 <= end <= 0x100):
        raise ValueError(f"Range out of byte bounds: {start}-{end}")
    if end < start:
        raise ValueError(f"Range end must be >= start: {start}-{end}")
    return RangeSpec(start=start, end=end)


def parse_spec(spec: str) -> RangeSpec:
    """Parse a single-argument spec.

    Supported forms:
    - Range:   60-80 (hex, end-exclusive)
    - Single:  6E    (hex byte)
    """

    spec = spec.strip()
    if "-" in spec:
        return parse_range(spec)

    value = _parse_int_hex(spec)
    if not (0 <= value <= 0xFF):
        raise ValueError(f"Byte out of range: {value}")
    return RangeSpec(start=value, end=value + 1)


def iter_petscii_bytes(r: RangeSpec) -> Iterable[int]:
    for b in range(r.start, r.end):
        yield b & 0xFF


def _http_request(
    *,
    base_url: str,
    password: str,
    method: str,
    path: str,
    query: Dict[str, str],
    timeout_s: float,
) -> bytes:
    url = base_url.rstrip("/") + path
    if query:
        url = url + "?" + urllib.parse.urlencode(query)

    req = urllib.request.Request(url, method=method)
    if password:
        req.add_header("X-Password", password)

    with urllib.request.urlopen(req, timeout=timeout_s) as resp:
        return resp.read()


def write_mem(
    *,
    base_url: str,
    password: str,
    address: int,
    data_bytes: bytes,
    timeout_s: float,
) -> None:
    query = {
        "address": f"{address:04X}",
        "data": data_bytes.hex().upper(),
    }
    _http_request(
        base_url=base_url,
        password=password,
        method="PUT",
        path="/v1/machine:writemem",
        query=query,
        timeout_s=timeout_s,
    )


def read_mem(*, base_url: str, password: str, address: int, length: int, timeout_s: float) -> bytes:
    query = {
        "address": f"{address:04X}",
        "length": str(int(length)),
    }
    return _http_request(
        base_url=base_url,
        password=password,
        method="GET",
        path="/v1/machine:readmem",
        query=query,
        timeout_s=timeout_s,
    )


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


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="Inject a PETSCII byte range via KERNAL keyboard buffer ($0277/$00C6)")
    ap.add_argument("spec", help="Hex range (60-80) or hex byte (6E)")
    ap.add_argument("--password", default=DEFAULT_PASSWORD, help="REST password (or env C64U_PASSWORD)")

    args = ap.parse_args(argv)

    try:
        r = parse_spec(args.spec)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    petscii_list = list(iter_petscii_bytes(r))
    if len(petscii_list) == 0:
        print("Nothing to send (empty range).")
        return 0

    try:
        remaining = petscii_list
        while remaining:
            # Wait until empty (backpressure)
            _wait_for_kbd_count(
                base_url=DEFAULT_BASE_URL,
                password=args.password,
                expected=0,
                timeout_s=DEFAULT_CONSUME_TIMEOUT_S,
                poll_interval_s=DEFAULT_POLL_INTERVAL_S,
            )

            chunk_list = remaining[:KERNAL_KBD_BUFFER_SIZE]
            remaining = remaining[len(chunk_list) :]
            chunk_bytes = bytes(chunk_list)

            # Write bytes to $0277 and then set $00C6 = len
            write_mem(
                base_url=DEFAULT_BASE_URL,
                password=args.password,
                address=KERNAL_KBD_BUFFER_ADDR,
                data_bytes=chunk_bytes,
                timeout_s=DEFAULT_CONSUME_TIMEOUT_S,
            )
            write_mem(
                base_url=DEFAULT_BASE_URL,
                password=args.password,
                address=KERNAL_KBD_COUNT_ADDR,
                data_bytes=bytes([len(chunk_list) & 0xFF]),
                timeout_s=DEFAULT_CONSUME_TIMEOUT_S,
            )

            # Wait until consumed before sending next chunk
            _wait_for_kbd_count(
                base_url=DEFAULT_BASE_URL,
                password=args.password,
                expected=0,
                timeout_s=DEFAULT_CONSUME_TIMEOUT_S,
                poll_interval_s=DEFAULT_POLL_INTERVAL_S,
            )

    except urllib.error.HTTPError as e:
        body = b""
        try:
            body = e.read()
        except Exception:
            pass
        print(f"HTTP error: {e.code} {e.reason}", file=sys.stderr)
        if body:
            print(body.decode("utf-8", errors="replace"), file=sys.stderr)
        return 1
    except urllib.error.URLError as e:
        print(f"Network error: {e}", file=sys.stderr)
        return 1
    except TimeoutError as e:
        print(f"Timeout: {e}", file=sys.stderr)
        return 1

    start_hex = f"${r.start:02X}"
    end_hex = f"${r.end:02X}"
    print(f"Injected PETSCII range {start_hex}-{end_hex} (exclusive) via KERNAL keyboard buffer")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
