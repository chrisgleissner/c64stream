#!/usr/bin/env python3
"""
C64 Stream - Script Status Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class ScriptStatusAssertion(EffectAssertion):
    """Verify that a script completed without runtime failures."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        super().__init__("Script Status", thresholds or {})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        del properties
        del preset
        del verbose

        obs_log_path = mp4_path.parent / "obs_log.txt"
        if not obs_log_path.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="obs_log.txt not found for script status verification",
            )

        content = obs_log_path.read_text(errors="ignore")
        if "Auto-start script failed:" in content or "Script failed:" in content:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="Script execution failed; see obs_log.txt for details",
                details={"obs_log": str(obs_log_path)},
            )

        if "Script completed successfully" not in content:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="Script did not report successful completion",
                details={"obs_log": str(obs_log_path)},
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message="Script completed successfully",
            details={"obs_log": str(obs_log_path)},
        )
