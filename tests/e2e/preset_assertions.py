#!/usr/bin/env python3
"""
C64 Stream - Preset-specific E2E Assertion Configurations
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

This module defines per-preset assertion configurations with
tuned thresholds for fast, resilient E2E testing.
"""

from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from assertions import (
    AfterglowAssertion,
    AssertionRunner,
    AudioAssertion,
    EffectAssertion,
    PresetConfig,
    ScanlineAssertion,
    TintAssertion,
    VideoQualityAssertion,
    load_preset_from_ini,
)


@dataclass
class PresetAssertionConfig:
    """Configuration for assertions specific to a preset."""

    preset_name: str
    video_thresholds: Optional[dict] = None
    audio_thresholds: Optional[dict] = None
    tint_thresholds: Optional[dict] = None
    afterglow_thresholds: Optional[dict] = None
    scanline_thresholds: Optional[dict] = None
    description: str = ""


# Preset-specific assertion configurations with tuned thresholds
PRESET_CONFIGS: dict[str, PresetAssertionConfig] = {
    # Default - No effects, just basic video/audio quality
    "default": PresetAssertionConfig(
        preset_name="Default",
        description="No CRT effects - validates basic video/audio capture",
        video_thresholds={
            "min_nonblack_ratio": 0.6,  # Allow more black frames (no visual effects)
        },
    ),
    # Classic CRT - Scanlines + subtle blur/bloom
    "classic_crt": PresetAssertionConfig(
        preset_name="Classic CRT",
        description="Moderate scanlines (50%) with subtle blur and bloom",
        scanline_thresholds={
            "max_variance_percent": 1.0,  # Slightly more tolerance due to blur
            "min_scanline_count": 40,
        },
        video_thresholds={
            "min_nonblack_ratio": 0.5,
            "black_threshold": 2.5,  # CRT effects darken output
        },
    ),
    # Amber Monitor - Amber tint
    "amber_monitor": PresetAssertionConfig(
        preset_name="Amber Monitor",
        description="Amber/orange color tint typical of older monitors",
        tint_thresholds={
            "min_tint_ratio": 1.15,  # Amber is subtler than green
            "min_nonblack_sum": 400_000,
        },
        video_thresholds={
            "black_threshold": 1.0,  # Amber tint makes output very dark with CRT effects
        },
    ),
    # Green Monitor - Green phosphor tint
    "green_monitor": PresetAssertionConfig(
        preset_name="Green Monitor",
        description="Green phosphor tint typical of P1 monitors",
        tint_thresholds={
            "min_tint_ratio": 1.20,  # Strong green
            "min_nonblack_sum": 400_000,
        },
        video_thresholds={
            "black_threshold": 2.0,  # CRT effects (blur+bloom+scanlines+tint) darken output significantly
        },
    ),
    # Sharp Pixels - No effects, similar to Default
    "sharp_pixels": PresetAssertionConfig(
        preset_name="Sharp Pixels",
        description="No CRT effects - clean pixel output",
        video_thresholds={
            "min_nonblack_ratio": 0.6,
        },
    ),
    # Phosphor Glow - Afterglow + blur + bloom
    "phosphor_glow": PresetAssertionConfig(
        preset_name="Phosphor Glow",
        description="Phosphor persistence (afterglow) with blur and bloom",
        afterglow_thresholds={
            "bright_thresh": 120.0,  # Lower threshold due to bloom softening
            "min_tail_luma": 2.0,
            "palette_drift_tol": 10.0,  # More tolerance due to bloom
        },
        video_thresholds={
            "min_nonblack_ratio": 0.5,
            "black_threshold": 2.0,  # CRT effects (blur+bloom+afterglow) darken output significantly
        },
    ),
    # Vintage TV - Scanlines + blur
    "vintage_tv": PresetAssertionConfig(
        preset_name="Vintage TV",
        description="Strong scanlines (75%) with blur for vintage TV look",
        scanline_thresholds={
            "max_variance_percent": 1.5,  # More tolerance due to blur
            "min_scanline_count": 35,
        },
        video_thresholds={
            "min_nonblack_ratio": 0.5,
            "black_threshold": 2.5,  # CRT effects darken output
        },
    ),
    # Arcade Cabinet - Strong scanlines
    "arcade_cabinet": PresetAssertionConfig(
        preset_name="Arcade Cabinet",
        description="Maximum scanlines (100%) with strong intensity for arcade look",
        scanline_thresholds={
            "max_variance_percent": 0.5,  # Strict - should be pixel-perfect
            "min_scanline_count": 50,
        },
        video_thresholds={
            "min_nonblack_ratio": 0.4,  # Scanlines reduce brightness significantly
            "black_threshold": 2.0,  # CRT effects darken output
        },
    ),
}


def get_preset_config(preset_name: str) -> Optional[PresetAssertionConfig]:
    """Get the assertion configuration for a preset by name."""
    # Normalize the name
    key = preset_name.lower().replace(" ", "_")
    return PRESET_CONFIGS.get(key)


def create_runner_for_preset(
    preset: PresetConfig, config: Optional[PresetAssertionConfig] = None, verbose: bool = False
) -> AssertionRunner:
    """
    Create an AssertionRunner configured for a specific preset.

    Args:
        preset: The preset configuration (loaded from effect_presets.ini)
        config: Optional preset-specific assertion config with tuned thresholds
        verbose: Enable verbose logging

    Returns:
        Configured AssertionRunner
    """
    runner = AssertionRunner(verbose=verbose)

    # Always add video quality check
    video_thresholds = config.video_thresholds if config else None
    runner.add_assertion(VideoQualityAssertion(video_thresholds))

    # Always add audio check
    audio_thresholds = config.audio_thresholds if config else None
    runner.add_assertion(AudioAssertion(audio_thresholds))

    # Add effect-specific assertions based on preset
    if preset.has_tint():
        tint_thresholds = config.tint_thresholds if config else None
        runner.add_assertion(TintAssertion(tint_thresholds))

    if preset.has_afterglow():
        afterglow_thresholds = config.afterglow_thresholds if config else None
        runner.add_assertion(AfterglowAssertion(afterglow_thresholds))

    if preset.has_scanlines():
        scanline_thresholds = config.scanline_thresholds if config else None
        runner.add_assertion(ScanlineAssertion(scanline_thresholds))

    return runner


def list_presets_with_assertions() -> None:
    """Print all presets with their expected assertions."""
    presets_ini = Path(__file__).parent.parent.parent / "data" / "effect_presets.ini"

    print("Preset Assertion Configurations:")
    print("=" * 70)

    for key, config in PRESET_CONFIGS.items():
        preset = load_preset_from_ini(key, presets_ini)
        if not preset:
            continue

        print(f"\n{config.preset_name} ({key})")
        print(f"  Description: {config.description}")
        print("  Assertions:")
        print("    - VideoQuality (always)")
        print("    - Audio (always)")

        if preset.has_tint():
            tint_type = preset.tint_type()
            print(f"    - Tint ({tint_type}, strength={preset.tint_strength})")

        if preset.has_afterglow():
            print(f"    - Afterglow (duration={preset.afterglow_duration_ms}ms, curve={preset.afterglow_curve})")

        if preset.has_scanlines():
            print(f"    - Scanlines (distance={preset.scan_line_distance}, strength={preset.scan_line_strength})")


if __name__ == "__main__":
    list_presets_with_assertions()
