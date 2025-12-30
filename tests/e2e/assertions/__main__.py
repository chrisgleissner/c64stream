#!/usr/bin/env python3
"""
C64 Stream - E2E Assertions CLI
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import argparse
import json
import sys
from configparser import ConfigParser
from pathlib import Path

from .base import AssertionStatus
from .config import (
    PresetConfig,
    load_preset_from_ini,
    load_properties,
    load_settings_from_obs_scene,
)
from .runner import (
    AssertionRunner,
    create_assertions_from_list,
    create_preset_assertions,
)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="C64 Stream E2E Assertions",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Verify recording against a scenario (preferred)
  python -m assertions --mp4 recording.mp4 --scenario ntsc_amber_monitor

  # Verify with OBS scene JSON
  python -m assertions --mp4 recording.mp4 --scene-json C64StreamTest.json

  # Verify with preset name
  python -m assertions --mp4 recording.mp4 --preset arcade_cabinet

  # List available presets
  python -m assertions --list-presets
""",
    )
    ap.add_argument("--mp4", type=Path, help="Path to OBS recording (MP4/MKV)")
    ap.add_argument("--scenario", help="Scenario name (e.g., ntsc_amber_monitor)")
    ap.add_argument("--properties", type=Path, help="Path to properties.ini used for recording")
    ap.add_argument(
        "--scene-json", type=Path, help="Path to OBS scene JSON file (e.g., C64StreamTest.json)"
    )
    ap.add_argument("--preset", help="Effect preset name (e.g., arcade_cabinet, green_monitor)")
    ap.add_argument(
        "--presets-ini",
        type=Path,
        default=Path(__file__).parent.parent.parent.parent / "data" / "effect_presets.ini",
        help="Path to effect_presets.ini",
    )
    ap.add_argument("--list-presets", action="store_true", help="List available presets and exit")
    ap.add_argument("--verbose", "-v", action="store_true", help="Enable verbose output")
    ap.add_argument("--json", action="store_true", help="Output results as JSON")
    ap.add_argument(
        "--settling-duration",
        "--settling-seconds",
        dest="settling_seconds",
        type=float,
        default=0.0,
        help="Ignore frame-progression anomalies during the initial settling period (seconds)",
    )

    args = ap.parse_args()

    # List presets mode
    if args.list_presets:
        if not args.presets_ini.exists():
            print(f"Presets file not found: {args.presets_ini}")
            return 1
        parser = ConfigParser()
        parser.read(args.presets_ini)
        print("Available presets:")
        for section in parser.sections():
            print(f"  - {section}")
        return 0

    # Verification mode
    if not args.mp4:
        ap.error("--mp4 is required for verification")
    if not args.scenario and not args.scene_json and not args.preset:
        ap.error("One of --scenario, --scene-json, or --preset is required for verification")

    if not args.mp4.exists():
        print(f"Recording not found: {args.mp4}")
        return 1

    # Load preset and assertions - either from scenario, scene JSON, or preset name
    preset = None
    properties = {}
    scenario_assertions = None

    if args.scenario:
        # Load from scenario (preferred)
        from scenario_loader import generate_scene_json, load_scenario, _get_canvas_size_from_scenario

        scenarios_dir = Path(__file__).parent.parent / "scenarios"
        scenario_yaml = scenarios_dir / args.scenario / "scenario.yaml"
        if not scenario_yaml.exists():
            print(f"Scenario not found: {args.scenario}")
            print(f"Expected: {scenario_yaml}")
            return 1

        scenario_cfg = load_scenario(scenario_yaml)
        scene = generate_scene_json(scenario_cfg, args.presets_ini)

        # Extract settings from generated scene
        settings = {}
        for source in scene.get("sources", []):
            if source.get("id") == "c64_source":
                settings = source.get("settings", {})
                break

        preset = PresetConfig.from_obs_settings(settings)
        properties = settings
        scenario_assertions = scenario_cfg.assertions

        # Add expected resolution from scenario
        canvas_w, canvas_h = _get_canvas_size_from_scenario(scenario_cfg)
        properties["expected_width"] = int(canvas_w)
        properties["expected_height"] = int(canvas_h)

        if args.verbose:
            print(f"Loaded scenario: {scenario_cfg.name}")
            print(f"  Preset: {scenario_cfg.preset}")
            print(f"  Overrides: {scenario_cfg.overrides}")
            print(f"  Assertions: {scenario_cfg.assertions}")
    elif args.scene_json:
        if not args.scene_json.exists():
            print(f"Scene JSON not found: {args.scene_json}")
            return 1
        # Load settings from OBS scene JSON
        settings = load_settings_from_obs_scene(args.scene_json)
        if not settings:
            print(f"No c64_source found in scene JSON: {args.scene_json}")
            return 1
        preset = PresetConfig.from_obs_settings(settings)
        properties = settings  # Use settings as properties dict
        if args.verbose:
            print(f"Loaded settings from scene JSON: {preset.name}")
            print(f"  Scanlines: {preset.scan_line_distance}/{preset.scan_line_strength}")
            print(f"  Afterglow: {preset.afterglow_duration_ms}ms")
            print(f"  Tint: mode={preset.tint_mode} strength={preset.tint_strength}")
    else:
        # Load preset from INI file
        preset = load_preset_from_ini(args.preset, args.presets_ini)
        if not preset:
            print(f"Preset not found: {args.preset}")
            print(f"Check available presets with: {sys.argv[0]} --list-presets")
            return 1

        # Load properties (optional)
        if args.properties and args.properties.exists():
            properties = load_properties(args.properties)

    if isinstance(properties, dict):
        properties["settling_seconds"] = float(args.settling_seconds)

    # Create assertions - from scenario list or auto-detect from preset
    if scenario_assertions:
        # Load preset-specific thresholds if available
        try:
            import sys
            sys.path.insert(0, str(Path(__file__).parent.parent))
            from preset_assertions import get_preset_config

            preset_config = get_preset_config(preset.name)
            thresholds = {}
            if preset_config:
                if preset_config.video_thresholds:
                    thresholds["video_quality"] = preset_config.video_thresholds
                if preset_config.audio_thresholds:
                    thresholds["audio"] = preset_config.audio_thresholds
                if preset_config.tint_thresholds:
                    thresholds["tint"] = preset_config.tint_thresholds
                if preset_config.afterglow_thresholds:
                    thresholds["afterglow"] = preset_config.afterglow_thresholds
                if preset_config.scanline_thresholds:
                    thresholds["scanlines"] = preset_config.scanline_thresholds
            assertions = create_assertions_from_list(scenario_assertions, thresholds)
        except ImportError:
            # Fall back to default thresholds if preset_assertions not available
            assertions = create_assertions_from_list(scenario_assertions)
    else:
        assertions = create_preset_assertions(preset)

    # Run assertions
    runner = AssertionRunner(verbose=args.verbose)
    for assertion in assertions:
        runner.add_assertion(assertion)

    results = runner.run_all(args.mp4, properties, preset)
    all_ok, summary = runner.summarize(results)

    # Output
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print(f"\n{'='*60}")
        print(f"Assertion Summary for preset: {preset.name}")
        print(f"{'='*60}")
        print(f"  Passed:   {summary['passed']}")
        print(f"  Failed:   {summary['failed']}")
        print(f"  Skipped:  {summary['skipped']}")
        print(f"  Warnings: {summary['warnings']}")
        print(f"{'='*60}")

        if all_ok:
            print("✅ All assertions passed!")
        else:
            print("❌ Some assertions failed")
            for r in results:
                if r.status == AssertionStatus.FAIL:
                    print(f"   - {r.name}: {r.message}")

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
