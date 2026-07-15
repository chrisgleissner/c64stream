"""
Device Switch Log Assertion

Verifies that tests/e2e/scripts/device_switch.c64script ran to completion
and that the plugin actually performed the expected number of device
transitions (checked in obs_log.txt, since the mock servers' in-memory
state doesn't survive past the end of the run).
"""

import os
import re
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class DeviceSwitchLogAssertion(EffectAssertion):
    """Validate device_switch.c64script ran and switched devices as expected."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "expected_switches": 2.0,
        }
        super().__init__("Device Switch Log", {**defaults, **(thresholds or {})})

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

        try:
            content = obs_log_path.read_text(errors="ignore")
        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Failed to read OBS log: {e}",
            )

        expected_switches = int(self.thresholds.get("expected_switches", 2))
        required_log_lines = [
            "=== Device Switch Started ===",
            f"=== Device Switch Complete: {expected_switches} switches ===",
        ]

        missing = [line for line in required_log_lines if line not in content]
        if missing:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Missing script log lines: {', '.join(missing)}",
                details={"obs_log": str(obs_log_path)},
            )

        transition_count = len(re.findall(r"Completing asynchronous device transition:", content))
        if transition_count < expected_switches:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=(
                    f"Expected at least {expected_switches} device transitions, "
                    f"found {transition_count} in OBS log"
                ),
                details={"obs_log": str(obs_log_path), "transition_count": transition_count},
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=f"Device switch script completed with {transition_count} device transitions",
            details={"obs_log": str(obs_log_path), "transition_count": transition_count},
        )
