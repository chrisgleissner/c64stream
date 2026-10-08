"""
Device Switch Speed Assertion

Reads the per-switch measurements the plugin logs on every device switch,

    Device switch from HOST: first frame N ms after the request; longest pause without video M ms

and requires:

- at least ``min_live_switches`` measured switches,
- every pause between the previous device's last frame and the new device's
  first frame to stay at or below ``max_pause_ms`` for switches between live
  devices (a switch away from the unreachable device has no previous picture,
  so its pause is not a switch gap),
- every first frame of a live device to arrive within ``max_first_frame_ms`` of
  the request,
- the script to have run to completion, and no source to have stopped
  retrying the devices it switched away from.
"""

import re
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig

MEASUREMENT = re.compile(
    r"Device switch from (\S*): first frame (\d+) ms after the request; longest pause without video (\d+) ms")
UNREACHABLE_HOST = "192.0.2.1"


class DeviceSwitchSpeedAssertion(EffectAssertion):
    """Every switch between live devices must leave at most a short pause."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {"max_pause_ms": 1000.0, "max_first_frame_ms": 1000.0, "min_live_switches": 30.0}
        super().__init__("Device Switch Speed", {**defaults, **(thresholds or {})})

    def verify(self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig,
               verbose: bool = False) -> AssertionResult:
        obs_log_path = mp4_path.parent / "obs_log.txt"
        if not obs_log_path.exists():
            return AssertionResult(AssertionStatus.FAIL, self.name, f"OBS log file not found: {obs_log_path}")
        content = obs_log_path.read_text(errors="ignore")
        if "=== Device switch speed complete ===" not in content:
            return AssertionResult(AssertionStatus.FAIL, self.name, "Switch script did not run to completion")

        # Each measurement names the device the source switched away from.
        measurements = [(int(m.group(2)), int(m.group(3)), m.group(1)) for m in MEASUREMENT.finditer(content)]

        max_pause = float(self.thresholds["max_pause_ms"])
        max_first = float(self.thresholds["max_first_frame_ms"])
        live = [(first, pause) for first, pause, previous in measurements if previous != UNREACHABLE_HOST]
        from_unreachable = [first for first, _, previous in measurements if previous == UNREACHABLE_HOST]
        slow_pauses = [pause for _, pause in live if pause > max_pause]
        slow_first = [first for first, _, _ in measurements if first > max_first]
        details = {
            "measured_switches": len(measurements),
            "live_switches": len(live),
            "from_unreachable": len(from_unreachable),
            "worst_pause_ms": max((pause for _, pause in live), default=0),
            "worst_first_frame_ms": max((first for first, _, _ in measurements), default=0),
            "slow_pauses_ms": slow_pauses[:10],
            "slow_first_frames_ms": slow_first[:10],
        }
        problems = []
        if len(live) < int(self.thresholds["min_live_switches"]):
            problems.append(f"only {len(live)} measured live switches")
        if not from_unreachable:
            problems.append("no switch back from the unreachable device was measured")
        if slow_pauses:
            problems.append(f"{len(slow_pauses)} switches paused longer than {max_pause:.0f} ms")
        if slow_first:
            problems.append(f"{len(slow_first)} switches took longer than {max_first:.0f} ms to the first frame")
        if "Device switch: giving up stopping 127.0.0." in content:
            problems.append("a live mock device was never told to stop")
        if problems:
            return AssertionResult(AssertionStatus.FAIL, self.name, "; ".join(problems), details=details)
        return AssertionResult(
            AssertionStatus.PASS, self.name,
            f"{len(measurements)} switches: worst pause {details['worst_pause_ms']} ms, "
            f"worst first frame {details['worst_first_frame_ms']} ms", details=details)
