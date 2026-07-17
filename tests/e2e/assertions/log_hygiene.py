#!/usr/bin/env python3
"""
C64 Stream - Log Hygiene Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Validates the C64CLK-003 logging policy from the OBS log of a run:

- expect_error_summary: a lossy run must surface a user-visible
  "Network errors (last ..s)" WARNING summary whose `audio lost=` count is
  within tolerance of the injected loss derived from audio_manifest.csv.
- expect_quiet: a clean run at default (non-debug) logging must not contain
  network-error summaries, periodic health/spot-check lines, or any plugin
  status line recurring more than `max_line_repeats` times during streaming.

OBS log files do not record the blog() severity, so detection is
pattern-based on the plugin's message text.
"""

import re
from collections import Counter
from pathlib import Path
from typing import Any, Optional

from .audio_pcm import manifest_audio_stats
from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig

# The C64CLK-003 rate-limited error summary line.
ERROR_SUMMARY_RE = re.compile(r"Network errors \(last \d+s\): .*audio lost=(\d+)")

# Periodic health chatter that must be debug-only (absent at default logging).
PERIODIC_HEALTH_PATTERNS = (
    "SPOT CHECK",
    "A/V SYNC: synthetic delta",
)

TIMESTAMP_RE = re.compile(r"^\d{2}:\d{2}:\d{2}\.\d{3}: ")
NUMBER_RE = re.compile(r"\d+")


class LogHygieneAssertion(EffectAssertion):
    """Verify error visibility on lossy runs and log quietness on clean runs."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "expect_error_summary": 0,
            "expect_quiet": 0,
            "loss_count_tolerance_pct": 20.0,
            "max_line_repeats": 3,
        }
        super().__init__("Log Hygiene", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        output_dir = mp4_path.parent
        log_path = output_dir / "obs_log.txt"
        if not log_path.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"OBS log not found: {log_path}",
            )

        lines = log_path.read_text(errors="ignore").splitlines()
        plugin_lines = [line for line in lines if "[c64stream]" in line]

        failures: list[str] = []
        details: dict[str, Any] = {"log": str(log_path), "plugin_lines": len(plugin_lines)}
        metrics: dict[str, float] = {}

        summary_counts = [int(m.group(1)) for m in map(ERROR_SUMMARY_RE.search, plugin_lines) if m]
        details["error_summary_lines"] = len(summary_counts)

        if int(self.thresholds["expect_error_summary"]):
            failures += self._check_error_summary(output_dir, summary_counts, details, metrics)

        if int(self.thresholds["expect_quiet"]):
            failures += self._check_quiet(plugin_lines, summary_counts, details)

        if failures:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="; ".join(failures),
                details=details,
                metrics=metrics,
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message="Log hygiene OK",
            details=details,
            metrics=metrics,
        )

    def _check_error_summary(
        self,
        output_dir: Path,
        summary_counts: list[int],
        details: dict[str, Any],
        metrics: dict[str, float],
    ) -> list[str]:
        """A lossy run must report its loss, with counts near the injected loss."""
        if not summary_counts:
            return [
                "No 'Network errors (last ..s)' summary found in log despite injected packet loss"
            ]

        reported_lost = max(summary_counts)  # cumulative counters: last/highest summary
        details["reported_audio_lost"] = reported_lost
        metrics["reported_audio_lost"] = float(reported_lost)

        manifest = output_dir / "audio_manifest.csv"
        if not manifest.exists():
            return ["audio_manifest.csv missing; cannot verify reported loss counts"]

        stats = manifest_audio_stats(manifest)
        injected = stats["injected_loss"]
        details["injected_audio_loss"] = injected
        metrics["injected_audio_loss"] = float(injected)

        tolerance_pct = float(self.thresholds["loss_count_tolerance_pct"])
        low = injected * (1.0 - tolerance_pct / 100.0)
        high = injected * (1.0 + tolerance_pct / 100.0)
        if not (low <= reported_lost <= high):
            return [
                f"Reported audio loss {reported_lost} outside ±{tolerance_pct:.0f}% of "
                f"injected loss {injected}"
            ]
        return []

    def _check_quiet(
        self,
        plugin_lines: list[str],
        summary_counts: list[int],
        details: dict[str, Any],
    ) -> list[str]:
        """A clean run must be quiet: no error summaries, no recurring status lines."""
        failures: list[str] = []

        if summary_counts:
            failures.append(
                f"{len(summary_counts)} network-error summary line(s) in a clean run"
            )

        health_hits = [
            line for line in plugin_lines
            if any(pattern in line for pattern in PERIODIC_HEALTH_PATTERNS)
        ]
        if health_hits:
            details["periodic_health_lines"] = health_hits[:5]
            failures.append(
                f"{len(health_hits)} periodic health line(s) present at default logging "
                f"(e.g. {health_hits[0].strip()!r})"
            )

        # Recurrence check: the same (number-masked) status line firing repeatedly
        # during the streaming window means the log is not quiet. The window
        # excludes startup (config load, source creation) and shutdown.
        start_idx = 0
        end_idx = len(plugin_lines)
        for i, line in enumerate(plugin_lines):
            if "STREAM START" in line:
                start_idx = i + 1
                break
        for i in range(len(plugin_lines) - 1, -1, -1):
            if "Destroying C64 Stream source" in plugin_lines[i]:
                end_idx = i
                break
        # A replay has a deliberate end-of-stream period before OBS shuts down.
        # Retry chatter after the first no-video timeout is outside the streaming
        # window and must not make a clean capture fail the quiet-log contract.
        for i in range(start_idx, end_idx):
            if "No video packets for" in plugin_lines[i]:
                end_idx = i
                break

        counter: Counter[str] = Counter()
        for line in plugin_lines[start_idx:end_idx]:
            normalized = NUMBER_RE.sub("#", TIMESTAMP_RE.sub("", line)).strip()
            counter[normalized] += 1

        max_repeats = int(self.thresholds["max_line_repeats"])
        recurring = {line: n for line, n in counter.items() if n > max_repeats}
        if recurring:
            top = sorted(recurring.items(), key=lambda kv: -kv[1])[:5]
            details["recurring_lines"] = [f"{n}x {line}" for line, n in top]
            failures.append(
                f"{len(recurring)} status line(s) recur >{max_repeats}x during streaming "
                f"(worst: {top[0][1]}x {top[0][0]!r})"
            )

        return failures
