#!/usr/bin/env python3
"""
C64 Stream - PCM Click-Analysis Helpers
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Shared PCM analysis for the audio-quality assertions (C64CLK-006). Works on
16-bit signed interleaved PCM using only the stdlib (wave/struct), so the E2E
harness gains no new dependencies.

The click detector assumes the deterministic `sine1k` audio fixture: a
phase-continuous 1 kHz sine (amplitude ~12000, DC offset +2000). The largest
legitimate sample-to-sample delta of that signal at ~48 kHz is ~1573, so any
|delta| above the click threshold marks a genuine discontinuity (packet-loss
splice, zero-fill step, stale packet, ...).
"""

from __future__ import annotations

import struct
import wave
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class ClickReport:
    """Result of a click scan over one PCM stream."""

    click_count: int = 0
    max_delta: int = 0
    sample_rate: float = 0.0
    duration_seconds: float = 0.0
    active_start_s: float = 0.0
    active_end_s: float = 0.0
    click_times_s: list[float] = field(default_factory=list)  # capped, for reporting


def read_wav_pcm(path: Path) -> tuple[int, int, bytes]:
    """Read a PCM WAV file; returns (sample_rate, channels, raw 16-bit frames)."""
    with wave.open(str(path), "rb") as wav:
        if wav.getsampwidth() != 2:
            raise ValueError(f"Expected 16-bit PCM, got {wav.getsampwidth() * 8}-bit")
        sample_rate = wav.getframerate()
        channels = wav.getnchannels()
        frames = wav.readframes(wav.getnframes())
    return sample_rate, channels, frames


def deinterleave(raw: bytes, channels: int) -> list[list[int]]:
    """Split raw 16-bit LE PCM into per-channel sample lists."""
    total = len(raw) // 2
    total -= total % max(channels, 1)
    samples = struct.unpack(f"<{total}h", raw[: total * 2])
    return [list(samples[ch::channels]) for ch in range(channels)]


def find_active_region(channel: list[int], activity_threshold: int = 500) -> tuple[int, int]:
    """Find the [start, end) sample range where the signal is active.

    The sine fixture rides on a +2000 DC offset, so silence (0) vs signal is
    unambiguous. Returns (0, 0) when no active samples exist.
    """
    start = None
    end = None
    for i, value in enumerate(channel):
        if abs(value) > activity_threshold:
            start = i
            break
    if start is None:
        return 0, 0
    for i in range(len(channel) - 1, start - 1, -1):
        if abs(channel[i]) > activity_threshold:
            end = i + 1
            break
    return start, end if end is not None else 0


def scan_clicks(
    channels: list[list[int]],
    sample_rate: float,
    click_threshold: int,
    trim_ms: float = 50.0,
    cluster_ms: float = 5.0,
) -> ClickReport:
    """Count click events (clustered threshold-exceeding sample deltas).

    Only the active signal region is scanned, trimmed by `trim_ms` on each
    side, so stream start/stop transitions (silence -> DC-offset sine) are
    not counted. Consecutive exceedances within `cluster_ms` count as ONE
    click event (a single splice can produce several large deltas).
    """
    report = ClickReport(sample_rate=sample_rate)
    if not channels or sample_rate <= 0:
        return report

    report.duration_seconds = len(channels[0]) / sample_rate
    trim = int(sample_rate * trim_ms / 1000.0)
    cluster_gap = max(1, int(sample_rate * cluster_ms / 1000.0))

    # Use the union of per-channel active regions so a channel that is pure
    # silence (e.g. mono content on one side) cannot zero out the scan window.
    starts_ends = [find_active_region(ch) for ch in channels]
    active = [(s, e) for s, e in starts_ends if e > s]
    if not active:
        return report
    region_start = min(s for s, _ in active)
    region_end = max(e for _, e in active)
    scan_start = region_start + trim
    scan_end = region_end - trim
    report.active_start_s = region_start / sample_rate
    report.active_end_s = region_end / sample_rate
    if scan_end - scan_start < 2:
        return report

    last_click_idx = -(10 * cluster_gap)
    for ch in channels:
        for i in range(max(scan_start, 1), min(scan_end, len(ch))):
            delta = abs(ch[i] - ch[i - 1])
            if delta > report.max_delta:
                report.max_delta = delta
            if delta > click_threshold:
                if i - last_click_idx > cluster_gap:
                    report.click_count += 1
                    if len(report.click_times_s) < 50:
                        report.click_times_s.append(round(i / sample_rate, 4))
                last_click_idx = i
        last_click_idx = -(10 * cluster_gap)

    return report


def manifest_audio_stats(manifest_path: Path) -> dict[str, int]:
    """Derive ground-truth send stats from audio_manifest.csv.

    The manifest is the exact record of what was sent: injected loss omits
    rows, duplicates repeat filenames. Packet indices are parsed from the
    fixed `audio_NNNN.bin` naming, so:
      - unique_sent: distinct packets actually sent
      - duplicates:  extra sends of already-sent packets
      - span:        (max index - min index + 1) -> the timeline the WAV must
                     cover once gap concealment fills interior losses
      - injected_loss: packets inside the span that were never sent
    """
    unique: set[int] = set()
    rows = 0
    with open(manifest_path, "r") as f:
        header = f.readline()
        if not header.startswith("filename"):
            raise ValueError(f"Unexpected manifest header: {header!r}")
        for line in f:
            line = line.strip()
            if not line:
                continue
            filename = line.split(",", 1)[0]
            stem = filename.rsplit(".", 1)[0]  # audio_0042
            index = int(stem.split("_")[-1])
            unique.add(index)
            rows += 1

    if not unique:
        return {"rows": 0, "unique_sent": 0, "duplicates": 0, "span": 0, "injected_loss": 0}

    span = max(unique) - min(unique) + 1
    return {
        "rows": rows,
        "unique_sent": len(unique),
        "duplicates": rows - len(unique),
        "span": span,
        "injected_loss": span - len(unique),
    }
