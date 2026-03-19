#!/usr/bin/env python3
"""
C64 Stream - Effect Cycle Script Log Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import os
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class EffectCycleLogAssertion(EffectAssertion):
    """Verify the demo effect-preset-cycle script ran to completion."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        super().__init__("Effect Cycle Log", thresholds or {})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        del properties
        del preset
        del verbose

        output_dir = mp4_path.parent
        obs_log_path = output_dir / "obs_log.txt"

        log_sources = []
        if obs_log_path.exists():
            log_sources.append(obs_log_path)

        repo_root = Path(__file__).resolve().parents[4]
        xdg_config_home = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
        for candidate in [
            repo_root / "c64script.log",
            xdg_config_home / "obs-studio" / "plugins" / "c64stream" / "c64script.log",
        ]:
            if candidate.exists():
                log_sources.append(candidate)

        if not log_sources:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="No OBS or C64Script log file found for effect-cycle verification",
            )

        content = ""
        for path in log_sources:
            try:
                content += path.read_text(errors="ignore") + "\n"
            except Exception:
                continue

        required_lines = [
            "Starting effect preset cycle demo",
            "Applying effect: Classic CRT",
            "Applying effect: Sharp Pixels",
            "Applying effect: Vintage TV",
            "Effect preset cycle demo completed",
        ]
        missing = [line for line in required_lines if line not in content]
        if missing:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Missing effect-cycle log lines: {', '.join(missing)}",
                details={"checked_files": [str(path) for path in log_sources]},
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message="Effect preset cycle logs detected",
            details={"checked_files": [str(path) for path in log_sources]},
        )
