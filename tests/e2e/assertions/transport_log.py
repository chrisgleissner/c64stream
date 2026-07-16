"""
Transport Log Assertions

Verify, from obs_log.txt, which wire transport (legacy port-64 + KERNAL-buffer
polling, or REST port-80 stream control + machine:input) the plugin actually
used for a run of tests/e2e/scripts/keyboard_injection.c64script. Both stream
control and keyboard injection share the same stream_control_transport
setting, so one script exercises both paths per scenario
(ntsc_transport_legacy / ntsc_transport_rest).
"""

from pathlib import Path
from typing import Any, List, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class _TransportLogAssertionBase(EffectAssertion):
    def __init__(self, name: str, required_substrings: List[str], forbidden_substrings: List[str],
                 thresholds: Optional[dict[str, float]] = None):
        super().__init__(name, thresholds or {})
        self.required_substrings = required_substrings
        self.forbidden_substrings = forbidden_substrings

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

        missing = [s for s in self.required_substrings if s not in content]
        if missing:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Missing required transport evidence: {missing}",
                details={"obs_log": str(obs_log_path)},
            )

        present_forbidden = [s for s in self.forbidden_substrings if s in content]
        if present_forbidden:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Found evidence of the wrong transport: {present_forbidden}",
                details={"obs_log": str(obs_log_path)},
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message="Transport evidence matched expectations",
            details={"obs_log": str(obs_log_path)},
        )


class LegacyTransportLogAssertion(_TransportLogAssertionBase):
    """Confirms stream control and keyboard injection both used the legacy
    (port-64 / KERNAL-buffer-via-REST-readmem-writemem) path."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        super().__init__(
            "Legacy Transport Log",
            required_substrings=[
                "Sending start command for stream 0 to",
                "bytes with verification",
            ],
            forbidden_substrings=[
                "via machine:input",
                "started via REST",
            ],
            thresholds=thresholds,
        )


class RestTransportLogAssertion(_TransportLogAssertionBase):
    """Confirms stream control and keyboard injection both used the REST
    (port-80 streams:start/stop + machine:input) path."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        super().__init__(
            "REST Transport Log",
            required_substrings=[
                "started via REST",
                "via machine:input",
            ],
            forbidden_substrings=[
                "bytes with verification",
            ],
            thresholds=thresholds,
        )
