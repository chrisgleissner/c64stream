#!/usr/bin/env python3
"""
C64 Stream - Render Log Clean Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Fails when OBS logs contain graphics/shader errors while rendering a source with
the C64 Stream Effects filter.
"""

from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class RenderLogCleanAssertion(EffectAssertion):
    """Validate that OBS render logs do not contain known shader/render failures."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        super().__init__("Render Log Clean", thresholds or {})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        output_dir = mp4_path.parent
        obs_log_path = output_dir / "obs_log.txt"

        if not obs_log_path.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"OBS log file not found: {obs_log_path}",
            )

        log_text = obs_log_path.read_text(encoding="utf-8", errors="ignore")
        error_patterns = [
            "effect_setval_inline: invalid param",
            "No vertex shader specified",
            "device_draw (GL) failed",
        ]

        matches: dict[str, int] = {}
        snippets: list[str] = []
        lines = log_text.splitlines()
        for pattern in error_patterns:
            count = 0
            for line in lines:
                if pattern in line:
                    count += 1
                    if len(snippets) < 6:
                        snippets.append(line.strip())
            if count:
                matches[pattern] = count

        if matches:
            summary = ", ".join(f"{pattern} x{count}" for pattern, count in matches.items())
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"OBS render errors detected: {summary}",
                details={
                    "obs_log": str(obs_log_path),
                    "matches": matches,
                    "sample_lines": snippets,
                },
                metrics={
                    "error_count": float(sum(matches.values())),
                },
            )

        if verbose:
            self.log("No render/shader errors found in OBS log", verbose)

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message="OBS log is free of known render/shader errors",
            details={"obs_log": str(obs_log_path)},
        )
