"""
Script Log Assertion

Verifies that the hello_world.c64script ran by checking script log output
and OBS logs for auto-start/completion signals.
"""

import os
import re
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class ScriptLogAssertion(EffectAssertion):
    """Validate hello_world script output via logs."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "require_auto_start": 1.0,
        }
        super().__init__("Script Log", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        output_dir = mp4_path.parent
        obs_log_path = output_dir / "obs_log.txt"

        required_log_lines = [
            "=== Hello World Script Started ===",
            "Final counter value: 5",
        ]

        log_sources = []

        if obs_log_path.exists():
            log_sources.append(obs_log_path)

        # Default LOG output file used by C64Script (relative to OBS working dir).
        repo_root = Path(__file__).resolve().parents[4]
        script_log_paths = [repo_root / "c64script.log"]

        xdg_config_home = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
        script_log_paths.append(xdg_config_home / "obs-studio" / "plugins" / "c64stream" / "c64script.log")

        mp4_mtime = mp4_path.stat().st_mtime
        for script_log_path in script_log_paths:
            if script_log_path.exists() and script_log_path.stat().st_mtime >= mp4_mtime - 5:
                log_sources.append(script_log_path)

        if not log_sources:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="No log files found (obs_log.txt or c64script.log)",
            )

        content = ""
        for path in log_sources:
            try:
                content += path.read_text(errors="ignore") + "\n"
            except Exception:
                continue

        missing = [line for line in required_log_lines if line not in content]
        if missing:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Missing script log lines: {', '.join(missing)}",
                details={"checked_files": [str(p) for p in log_sources]},
            )

        auto_start_ok = True

        if obs_log_path.exists():
            obs_text = obs_log_path.read_text(errors="ignore")
            if self.thresholds.get("require_auto_start", 1.0) > 0:
                auto_start_ok = re.search(r"Auto-starting script:.*hello_world", obs_text, re.IGNORECASE) is not None

        if not auto_start_ok:
            missing_obs = []
            if not auto_start_ok:
                missing_obs.append("Auto-starting script")
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"OBS log missing expected entries: {', '.join(missing_obs)}",
                details={"obs_log": str(obs_log_path)},
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message="Hello World script logs detected",
            details={"checked_files": [str(p) for p in log_sources]},
        )
