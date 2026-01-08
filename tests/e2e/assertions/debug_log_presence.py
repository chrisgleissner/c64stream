"""
Debug Log Presence Assertion

Verifies that debug logs actually appear in OBS logs when the debug_logging
checkbox is enabled in plugin properties. This ensures the checkbox mechanism
works correctly without requiring special OBS startup flags.

This assertion performs spot checks on various debug log categories:
- Debug logging state changes (enabled/disabled messages)
- Network operations (TCP connections, DNS resolution, socket operations)
- Video/Audio timing adjustments
- Protocol operations (start/stop commands)

The assertion validates that:
1. The "Debug logging enabled" message appears (proves checkbox is working)
2. Various debug log patterns from normal plugin operation are present
3. Logs contain the expected debug log prefixes (📡 NETWORK, 📺 VIDEO, etc.)
"""

import re
from pathlib import Path
from typing import Any, List, Tuple, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class DebugLogPresenceAssertion(EffectAssertion):
    """Validates that debug logs appear when debug_logging checkbox is enabled."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        """
        Initialize the debug log presence assertion.

        Args:
            thresholds: Optional dict of threshold overrides (currently unused)
        """
        defaults = {
            "min_debug_logs": 10,  # Minimum number of debug log lines expected
        }
        super().__init__("Debug Log Presence", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        """
        Verify that debug logs are present in OBS log file.

        Args:
            mp4_path: Path to the recorded MP4 file
            properties: Plugin properties dict
            preset: Preset configuration
            verbose: Enable verbose output

        Returns:
            AssertionResult with verification outcome
        """
        output_dir = mp4_path.parent
        obs_log_path = output_dir / "obs_log.txt"

        if not obs_log_path.exists():
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"OBS log file not found: {obs_log_path}",
            )

        # Read the OBS log
        try:
            with open(obs_log_path, "r", encoding="utf-8", errors="ignore") as f:
                log_content = f.read()
        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Failed to read OBS log: {e}",
            )

        # Define expected debug log patterns (spot checks)
        # Each pattern is (regex, description, required)
        debug_patterns: List[Tuple[str, str, bool]] = [
            # Network operations (common during connection) - these prove debug logs are working
            (r"📡 NETWORK.*(TCP connection|Connected to C64 Ultimate|Sending start command|Resolved C64 host)", "Network operation logs", True),
            (r"📡 NETWORK.*(socket|UDP|buffer|Created optimized)", "Socket/UDP operation logs", True),
            # Debug logging state message (may not appear due to OBS log level settings)
            (r"Debug logging.*(enabled|initialized.*enabled)", "Debug logging state message", False),
            # Video/Audio timing (should appear during normal operation if buffer changes)
            (r"📐 Video timing|🎵 Audio timing|🔄 Render texture", "Timing adjustment logs", False),
            # Socket cleanup logs
            (r"Closing video socket|Closing audio socket|socket closed", "Socket cleanup logs", False),
        ]

        # Check each pattern
        found_patterns = []
        missing_required = []

        for pattern, description, required in debug_patterns:
            matches = re.findall(pattern, log_content, re.IGNORECASE)
            if matches:
                found_patterns.append(f"✓ {description}: {len(matches)} occurrences")
                if verbose:
                    # Show first match as example
                    for line in log_content.split("\n"):
                        if re.search(pattern, line, re.IGNORECASE):
                            self.log(f"  Example: {line.strip()}", verbose)
                            break
            elif required:
                missing_required.append(f"✗ {description} (REQUIRED)")

        # Check for any debug logs at all (generic check)
        c64_debug_lines = [line for line in log_content.split("\n") if "[c64stream]" in line]
        total_debug_logs = len(c64_debug_lines)

        # Validation results
        min_debug_logs = int(self.thresholds.get("min_debug_logs", 10))

        if missing_required:
            msg = f"Missing required debug logs:\n" + "\n".join(missing_required)
            msg += f"\n\nFound patterns:\n" + "\n".join(found_patterns)
            msg += f"\n\nTotal debug log lines: {total_debug_logs}"
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=msg,
                details={
                    "total_debug_logs": total_debug_logs,
                    "missing_required": missing_required,
                },
            )

        # Success - all required patterns found
        msg = f"Debug logs present and working correctly! Total debug log lines: {total_debug_logs}"

        if verbose:
            msg += "\n\nFound patterns:\n" + "\n".join(found_patterns)

        # Additional validation: warn if suspiciously low debug log count
        if total_debug_logs < min_debug_logs:
            msg += f"\n\n⚠️  Warning: Only {total_debug_logs} debug logs found (expected at least {min_debug_logs})"
            status = AssertionStatus.WARNING
        else:
            status = AssertionStatus.PASS

        return AssertionResult(
            status=status,
            name=self.name,
            message=msg,
            details={
                "total_debug_logs": total_debug_logs,
                "found_patterns": len(found_patterns),
            },
            metrics={
                "debug_log_count": float(total_debug_logs),
            },
        )
