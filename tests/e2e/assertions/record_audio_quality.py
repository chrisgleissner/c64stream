#!/usr/bin/env python3
"""
C64 Stream - Record Audio Quality Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Click and duration analysis of the plugin's own audio.wav recording
(C64CLK-006). Requires the deterministic `sine1k` audio fixture: any
sample-to-sample step above the click threshold is a genuine discontinuity
(loss splice, zero-fill, stale packet), and the WAV must cover the replayed
timeline span (gap concealment keeps it duration-matched to the video).
"""

from pathlib import Path
from typing import Any, Optional

from .audio_pcm import deinterleave, manifest_audio_stats, read_wav_pcm, scan_clicks
from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig
from .record_audio import RecordAudioAssertion


class RecordAudioQualityAssertion(EffectAssertion):
    """Verify audio.wav is click-free and duration-matched to the replay span."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            # ~3x the largest legitimate 1 kHz-sine step (~1573) at ~48 kHz.
            "click_threshold": 6000,
            "max_clicks": 0,
            "duration_tolerance_ms": 25.0,
            "trim_ms": 50.0,
        }
        super().__init__("Record Audio Quality", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        output_dir = mp4_path.parent
        audio_wav = RecordAudioAssertion()._find_audio_wav(output_dir)
        if audio_wav is None:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="audio.wav not found in session folder",
                details={"searched_dir": str(output_dir)},
            )

        try:
            sample_rate, channels, raw = read_wav_pcm(audio_wav)
            pcm_channels = deinterleave(raw, channels)
        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Failed to decode audio.wav: {e}",
                details={"path": str(audio_wav)},
            )

        report = scan_clicks(
            pcm_channels,
            float(sample_rate),
            click_threshold=int(self.thresholds["click_threshold"]),
            trim_ms=float(self.thresholds["trim_ms"]),
        )

        details: dict[str, Any] = {
            "path": str(audio_wav),
            "sample_rate": sample_rate,
            "channels": channels,
            "duration_seconds": round(report.duration_seconds, 3),
            "click_count": report.click_count,
            "max_delta": report.max_delta,
            "click_times_s": report.click_times_s,
        }
        metrics = {
            "click_count": float(report.click_count),
            "max_delta": float(report.max_delta),
            "duration_seconds": report.duration_seconds,
        }

        failures: list[str] = []

        # 1. Click budget.
        max_clicks = int(self.thresholds["max_clicks"])
        if report.click_count > max_clicks:
            failures.append(
                f"{report.click_count} click(s) detected (budget {max_clicks}, "
                f"max |delta|={report.max_delta})"
            )

        # 2. Duration must match the replayed audio timeline span.
        manifest = output_dir / "audio_manifest.csv"
        if manifest.exists():
            try:
                stats = manifest_audio_stats(manifest)
                packet_ms = (192.0 / sample_rate) * 1000.0
                expected_ms = stats["span"] * packet_ms
                actual_ms = report.duration_seconds * 1000.0
                tolerance_ms = float(self.thresholds["duration_tolerance_ms"])
                deficit_ms = expected_ms - actual_ms
                details["expected_duration_ms"] = round(expected_ms, 1)
                details["actual_duration_ms"] = round(actual_ms, 1)
                details["duration_deficit_ms"] = round(deficit_ms, 1)
                details["manifest_stats"] = stats
                metrics["duration_deficit_ms"] = deficit_ms
                if abs(deficit_ms) > tolerance_ms:
                    failures.append(
                        f"WAV duration {actual_ms:.1f}ms vs replay span {expected_ms:.1f}ms "
                        f"(deficit {deficit_ms:+.1f}ms > tolerance {tolerance_ms:.0f}ms)"
                    )
            except Exception as e:
                failures.append(f"Failed to derive replay span from audio_manifest.csv: {e}")
        else:
            details["expected_duration_ms"] = None
            self.log("audio_manifest.csv not found; skipping duration check", verbose)

        self.log(
            f"clicks={report.click_count} max_delta={report.max_delta} "
            f"duration={report.duration_seconds:.2f}s",
            verbose,
        )

        if failures:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="; ".join(failures),
                details=details,
                metrics=metrics,
            )

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=(
                f"audio.wav clean: {report.click_count} clicks "
                f"(max |delta|={report.max_delta}), duration {report.duration_seconds:.2f}s"
            ),
            details=details,
            metrics=metrics,
        )
