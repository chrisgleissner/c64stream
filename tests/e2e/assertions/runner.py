#!/usr/bin/env python3
"""
C64 Stream - Assertion Runner
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import contextlib
import io
import os
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from typing import Any, Optional

from .afterglow import AfterglowAssertion
from .afterglow_decay import AfterglowDecayAssertion
from .afterglow_width import AfterglowWidthAssertion
from .audio import AudioAssertion
from .av_sync_csv_validation import AvSyncCsvValidationAssertion
from .av_sync_offset import AvSyncOffsetAssertion
from .av_sync_log_validation import AvSyncLogValidationAssertion
from .base import AssertionResult, AssertionStatus, EffectAssertion, is_ci
from .bounds import BoundsStabilityAssertion, BoundsVariationAssertion
from .debug_log_presence import DebugLogPresenceAssertion
from .device_switch_log import DeviceSwitchLogAssertion
from .transport_log import LegacyTransportLogAssertion, RestTransportLogAssertion
from .config import PresetConfig
from .effect_change import EffectChangeAssertion
from .effect_cycle_log import EffectCycleLogAssertion
from .frame_progression import FrameProgressionAssertion
from .palette_mapping import PaletteMappingAssertion
from .palette_stability import PaletteStabilityAssertion
from .record_audio import RecordAudioAssertion
from .record_frames import RecordFramesAssertion
from .record_network import RecordNetworkAssertion
from .record_obs import RecordObsAssertion
from .record_video import RecordVideoAssertion
from .render_log_clean import RenderLogCleanAssertion
from .scanlines import ScanlineAssertion
from .sharp_pixels import SharpPixelsAssertion
from .tint import TintAssertion
from .effect_transition import EffectTransitionAssertion
from .tint_transition import TintTransitionAssertion
from .video_quality import VideoQualityAssertion
from .preserve_size_canvas_match import PreserveSizeCanvasMatchAssertion
from .script_log import ScriptLogAssertion
from .script_record import ScriptRecordAssertion
from .script_status import ScriptStatusAssertion


def _get_max_workers_for_assertions() -> int:
    """Get appropriate worker count for parallel assertion execution."""
    if is_ci():
        return 1
    return os.cpu_count() or 1


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
        if not self.assertions:
            return []

        results: list[Optional[AssertionResult]] = [None] * len(self.assertions)
        logs: list[str] = [""] * len(self.assertions)

        # In CI, run sequentially to minimize memory usage
        if is_ci():
            for index, assertion in enumerate(self.assertions):
                result, output = _run_assertion_worker(assertion, mp4_path, properties, preset, self.verbose)
                results[index] = result
                logs[index] = output
        else:
            # Local: use parallel execution
            max_workers = min(len(self.assertions), _get_max_workers_for_assertions())
            with ProcessPoolExecutor(max_workers=max_workers) as executor:
                futures = {
                    executor.submit(
                        _run_assertion_worker, assertion, mp4_path, properties, preset, self.verbose
                    ): index
                    for index, assertion in enumerate(self.assertions)
                }

                for future, index in futures.items():
                    result, output = future.result()
                    results[index] = result
                    logs[index] = output

        if self.verbose:
            for assertion, result, output in zip(self.assertions, results, logs):
                print(f"\n{'='*60}")
                print(f"Running: {assertion.name}")
                print(f"{'='*60}")
                if output:
                    print(output, end="")
                status_icon = {
                    AssertionStatus.PASS: "✅",
                    AssertionStatus.FAIL: "❌",
                    AssertionStatus.SKIP: "⏭️",
                    AssertionStatus.WARNING: "⚠️",
                }[result.status]
                print(f"{status_icon} {result.message}")

        return [result for result in results if result is not None]

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


def _apply_tolerance_to_thresholds(thresholds: dict[str, float], tolerance: float) -> None:
    if tolerance <= 0:
        return
    for key, value in list(thresholds.items()):
        if not isinstance(value, (int, float)):
            continue
        key_lower = key.lower()
        if "expected" in key_lower:
            continue
        if key_lower.startswith("max") or "_max" in key_lower:
            thresholds[key] = value / tolerance
        elif key_lower.startswith("min") or "_min" in key_lower:
            thresholds[key] = value * tolerance
        elif "tolerance" in key_lower:
            thresholds[key] = value / tolerance
        elif "threshold" in key_lower or "ratio" in key_lower:
            thresholds[key] = value * tolerance


def create_assertions_from_list(
    assertion_names: list[str],
    thresholds: Optional[dict[str, dict[str, float]]] = None,
    tolerances: Optional[dict[str, float]] = None,
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
        "av_sync_offset": AvSyncOffsetAssertion,
        "av_sync_csv_validation": AvSyncCsvValidationAssertion,
        "av_sync_log_validation": AvSyncLogValidationAssertion,
        "debug_log_presence": DebugLogPresenceAssertion,
        "device_switch_log": DeviceSwitchLogAssertion,
        "legacy_transport_log": LegacyTransportLogAssertion,
        "rest_transport_log": RestTransportLogAssertion,
        "bounds_stability": BoundsStabilityAssertion,
        "bounds_variation": BoundsVariationAssertion,
        "effect_change": EffectChangeAssertion,
        "effect_cycle_log": EffectCycleLogAssertion,
        "tint": TintAssertion,
        "tint_transition": TintTransitionAssertion,
        "effect_transition": EffectTransitionAssertion,
        "palette_stability": PaletteStabilityAssertion,
        "palette_mapping": PaletteMappingAssertion,
        "afterglow": AfterglowAssertion,
        "afterglow_decay": AfterglowDecayAssertion,
        "afterglow_width": AfterglowWidthAssertion,
        "scanlines": ScanlineAssertion,
        "sharp_pixels": SharpPixelsAssertion,
        "frame_progression": FrameProgressionAssertion,
        "record_audio": RecordAudioAssertion,
        "record_video": RecordVideoAssertion,
        "record_obs": RecordObsAssertion,
        "record_network": RecordNetworkAssertion,
        "record_frames": RecordFramesAssertion,
        "render_log_clean": RenderLogCleanAssertion,
        "preserve_size_canvas_match": PreserveSizeCanvasMatchAssertion,
        "script_log": ScriptLogAssertion,
        "script_record": ScriptRecordAssertion,
        "script_status": ScriptStatusAssertion,
    }

    thresholds = thresholds or {}
    tolerances = tolerances or {}
    assertions: list[EffectAssertion] = []

    for name in assertion_names:
        assertion_cls = assertion_map.get(name.lower())
        if assertion_cls:
            assertion_thresholds = thresholds.get(name.lower())
            assertion = assertion_cls(assertion_thresholds)
            tolerance = tolerances.get(name.lower(), 1.0)
            if tolerance != 1.0:
                _apply_tolerance_to_thresholds(assertion.thresholds, tolerance)
            assertions.append(assertion)

    return assertions


def _run_assertion_worker(
    assertion: EffectAssertion,
    mp4_path: Path,
    properties: dict[str, Any],
    preset: PresetConfig,
    verbose: bool,
) -> tuple[AssertionResult, str]:
    output = ""
    if verbose:
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            result = assertion.verify(mp4_path, properties, preset, verbose)
        output = buffer.getvalue()
    else:
        result = assertion.verify(mp4_path, properties, preset, verbose)
    return result, output
