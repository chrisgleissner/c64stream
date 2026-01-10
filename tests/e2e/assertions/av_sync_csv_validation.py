#!/usr/bin/env python3
"""
C64 Stream - A/V Sync CSV Validation Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Validates that av-sync.csv exists and is consistent with AV SYNC log entries in obs_log.txt.

This is intended to be a high-signal, deterministic check that our new CSV recording matches
what the plugin emits at the "AV SYNC (OBS)" emission point.
"""

import csv
import re
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class AvSyncCsvValidationAssertion(EffectAssertion):
    """Validate that av-sync.csv matches AV SYNC log entries."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            # CSV/log formatting may round; keep this reasonably tight but not fragile.
            "tolerance_ms": 0.5,
        }
        super().__init__("A/V Sync CSV Validation", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        output_dir = mp4_path.parent
        av_sync_csv = output_dir / "av-sync.csv"
        obs_log = output_dir / "obs_log.txt"

        if not av_sync_csv.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"av-sync.csv not found in {output_dir}",
                details={"output_dir": str(output_dir)},
            )

        if not obs_log.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"obs_log.txt not found in {output_dir}",
                details={"output_dir": str(output_dir)},
            )

        try:
            csv_obs_offsets, csv_net_offsets = self._parse_av_sync_csv_offsets(av_sync_csv)
            log_obs_offsets, log_net_offsets = self._parse_obs_log_offsets_dedup(obs_log)

            if not csv_obs_offsets:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="av-sync.csv contains no usable rows",
                    details={"path": str(av_sync_csv)},
                )

            tol_ms = float(self.thresholds["tolerance_ms"])

            if len(csv_obs_offsets) != len(log_obs_offsets):
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=(
                        f"av-sync.csv/obs_log.txt mismatch: {len(csv_obs_offsets)} CSV rows vs "
                        f"{len(log_obs_offsets)} OBS log offsets"
                    ),
                    details={
                        "csv_rows": len(csv_obs_offsets),
                        "log_offsets": len(log_obs_offsets),
                        "tolerance_ms": tol_ms,
                    },
                )

            for i, (a, b) in enumerate(zip(csv_obs_offsets, log_obs_offsets)):
                if abs(a - b) > tol_ms:
                    return AssertionResult(
                        status=AssertionStatus.FAIL,
                        name=self.name,
                        message=(
                            f"av-sync.csv/obs_log.txt mismatch at index {i}: csv={a:.2f}ms log={b:.2f}ms "
                            f"(tol {tol_ms:.2f}ms)"
                        ),
                        details={
                            "index": i,
                            "csv_ms": a,
                            "log_ms": b,
                            "tolerance_ms": tol_ms,
                        },
                    )

            # Best-effort: validate network-match counts when log includes Network offsets.
            if log_net_offsets:
                if len(csv_net_offsets) != len(log_net_offsets):
                    return AssertionResult(
                        status=AssertionStatus.FAIL,
                        name=self.name,
                        message=(
                            f"av-sync.csv/obs_log.txt mismatch: {len(csv_net_offsets)} network matches vs "
                            f"{len(log_net_offsets)} Network log offsets"
                        ),
                        details={
                            "csv_network_matches": len(csv_net_offsets),
                            "log_network_offsets": len(log_net_offsets),
                        },
                    )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"av-sync.csv validated against obs_log.txt ({len(csv_obs_offsets)} rows)",
                details={
                    "rows": len(csv_obs_offsets),
                    "network_matches": len(csv_net_offsets),
                    "tolerance_ms": tol_ms,
                },
            )

        except Exception as exc:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"A/V sync CSV validation error: {exc}",
                details={"error": str(exc)},
            )

    def _parse_av_sync_csv_offsets(self, csv_path: Path) -> tuple[list[float], list[float]]:
        obs_offsets: list[float] = []
        net_offsets: list[float] = []

        with open(csv_path, "r", newline="") as f:
            reader = csv.DictReader(f)
            if not reader.fieldnames:
                return [], []

            for row in reader:
                obs_off = (row.get("obs_offset_ms") or "").strip()
                if obs_off:
                    try:
                        obs_offsets.append(abs(float(obs_off)))
                    except ValueError:
                        pass

                if (row.get("has_network_match") or "").strip() == "1":
                    net_off = (row.get("net_offset_ms") or "").strip()
                    if net_off:
                        try:
                            net_offsets.append(abs(float(net_off)))
                        except ValueError:
                            pass

        return obs_offsets, net_offsets

    def _parse_obs_log_offsets_dedup(self, log_path: Path) -> tuple[list[float], list[float]]:
        # obs_log.txt in some harnesses contains duplicate lines. De-dup identical consecutive lines
        # to align with per-event CSV row emission.
        av_sync_re = re.compile(r"AV SYNC \((OBS|Network)\): offset=([+-]?[0-9]+(?:\.[0-9]+)?)ms")
        obs_offsets: list[float] = []
        net_offsets: list[float] = []

        prev = None
        for raw in log_path.read_text(errors="replace").splitlines():
            line = raw.strip()
            if not line:
                continue
            if prev is not None and line == prev:
                continue
            prev = line

            for match in av_sync_re.finditer(line):
                origin = match.group(1)
                try:
                    val = abs(float(match.group(2)))
                except ValueError:
                    continue
                if origin == "OBS":
                    obs_offsets.append(val)
                else:
                    net_offsets.append(val)

        return obs_offsets, net_offsets
