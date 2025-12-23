#!/usr/bin/env python3
"""
C64 Stream - Assertion Runner
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

from pathlib import Path
from typing import Any, Optional

from .afterglow import AfterglowAssertion
from .audio import AudioAssertion
from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig
from .scanlines import ScanlineAssertion
from .tint import TintAssertion
from .video_quality import VideoQualityAssertion


class AssertionRunner:
    """Orchestrates running multiple assertions against a recording."""

    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.assertions: list[EffectAssertion] = []

    def add_assertion(self, assertion: EffectAssertion) -> "AssertionRunner":
        self.assertions.append(assertion)
        return self

    def run_all(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig
    ) -> list[AssertionResult]:
        """Run all configured assertions and return results."""
        results = []
        for assertion in self.assertions:
            if self.verbose:
                print(f"\n{'='*60}")
                print(f"Running: {assertion.name}")
                print(f"{'='*60}")

            result = assertion.verify(mp4_path, properties, preset, self.verbose)
            results.append(result)

            if self.verbose:
                status_icon = {
                    AssertionStatus.PASS: "✅",
                    AssertionStatus.FAIL: "❌",
                    AssertionStatus.SKIP: "⏭️",
                    AssertionStatus.WARNING: "⚠️",
                }[result.status]
                print(f"{status_icon} {result.message}")

        return results

    @staticmethod
    def summarize(results: list[AssertionResult]) -> tuple[bool, dict[str, Any]]:
        """Summarize assertion results."""
        passed = sum(1 for r in results if r.status == AssertionStatus.PASS)
        failed = sum(1 for r in results if r.status == AssertionStatus.FAIL)
        skipped = sum(1 for r in results if r.status == AssertionStatus.SKIP)
        warnings = sum(1 for r in results if r.status == AssertionStatus.WARNING)

        summary = {
            "total": len(results),
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
            "warnings": warnings,
            "results": [r.to_dict() for r in results],
        }

        all_ok = failed == 0
        return all_ok, summary


def create_preset_assertions(preset: PresetConfig) -> list[EffectAssertion]:
    """Create the appropriate assertions for a given preset."""
    assertions: list[EffectAssertion] = []

    # Always check video quality
    assertions.append(VideoQualityAssertion())

    # Always check audio
    assertions.append(AudioAssertion())

    # Add effect-specific assertions
    if preset.has_tint():
        assertions.append(TintAssertion())

    if preset.has_afterglow():
        assertions.append(AfterglowAssertion())

    if preset.has_scanlines():
        assertions.append(ScanlineAssertion())

    return assertions


def create_assertions_from_list(
    assertion_names: list[str], thresholds: Optional[dict[str, dict[str, float]]] = None
) -> list[EffectAssertion]:
    """Create assertions from a list of assertion names.

    Args:
        assertion_names: List of assertion names (e.g., ['video_quality', 'audio', 'tint'])
        thresholds: Optional dict mapping assertion names to threshold overrides

    Returns:
        List of EffectAssertion instances
    """
    assertion_map = {
        "video_quality": VideoQualityAssertion,
        "audio": AudioAssertion,
        "tint": TintAssertion,
        "afterglow": AfterglowAssertion,
        "scanlines": ScanlineAssertion,
    }

    thresholds = thresholds or {}
    assertions: list[EffectAssertion] = []

    for name in assertion_names:
        assertion_cls = assertion_map.get(name.lower())
        if assertion_cls:
            assertion_thresholds = thresholds.get(name.lower())
            assertions.append(assertion_cls(assertion_thresholds))

    return assertions
