#!/usr/bin/env python3
"""
C64 Stream - Scenario Loader Module
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

This module loads E2E test scenarios from YAML files and generates
the required OBS scene JSON with the appropriate source settings.

Scenarios specify:
- Video format (PAL/NTSC)
- Effect preset name (from effect_presets.ini)
- Optional effect overrides
- Assertions to run against recorded output
"""

import json
import os
from configparser import ConfigParser
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

import yaml


@dataclass
class ScenarioConfig:
    """Parsed scenario configuration."""

    name: str
    format: str  # PAL or NTSC
    preset: str
    overrides: dict[str, Any] = field(default_factory=dict)
    assertions: list[str] = field(default_factory=list)
    scenario_dir: Path = field(default_factory=Path)


def load_preset_settings(preset_name: str, presets_path: Optional[Path] = None) -> dict[str, Any]:
    """
    Load effect settings for a preset from effect_presets.ini.

    Args:
        preset_name: Name of the preset (e.g., "Amber Monitor")
        presets_path: Path to effect_presets.ini (auto-detected if None)

    Returns:
        Dictionary of effect settings for the preset
    """
    if presets_path is None:
        # Find effect_presets.ini relative to this script or project root
        script_dir = Path(__file__).parent
        candidates = [
            script_dir.parent.parent / "data" / "effect_presets.ini",
            script_dir / "effect_presets.ini",
            Path.home() / ".config" / "obs-studio" / "plugins" / "c64stream" / "data" / "effect_presets.ini",
        ]
        for candidate in candidates:
            if candidate.exists():
                presets_path = candidate
                break
        if presets_path is None:
            raise FileNotFoundError("Could not find effect_presets.ini")

    config = ConfigParser()
    config.read(presets_path)

    if preset_name not in config:
        raise ValueError(f"Preset '{preset_name}' not found in {presets_path}")

    # Convert settings to appropriate types
    settings = {}
    for key, value in config[preset_name].items():
        # Parse numeric values
        if "." in value:
            settings[key] = float(value)
        elif value.isdigit() or (value.startswith("-") and value[1:].isdigit()):
            settings[key] = int(value)
        else:
            settings[key] = value

    return settings


def load_scenario(scenario_path: Path) -> ScenarioConfig:
    """
    Load a scenario from a YAML file.

    Args:
        scenario_path: Path to scenario.yaml file

    Returns:
        ScenarioConfig with parsed settings
    """
    if not scenario_path.exists():
        raise FileNotFoundError(f"Scenario file not found: {scenario_path}")

    with open(scenario_path, "r") as f:
        data = yaml.safe_load(f)

    return ScenarioConfig(
        name=data.get("name", ""),
        format=data.get("format", "NTSC"),
        preset=data.get("preset", "Default"),
        overrides=data.get("overrides", {}),
        assertions=data.get("assertions", ["video_quality", "audio"]),
        scenario_dir=scenario_path.parent,
    )


def load_base_template(template_path: Optional[Path] = None) -> dict:
    """
    Load the base OBS scene template.

    Args:
        template_path: Path to base_template.json (auto-detected if None)

    Returns:
        Dictionary representing the base OBS scene JSON
    """
    if template_path is None:
        script_dir = Path(__file__).parent
        template_path = script_dir / "scenarios" / "base_template.json"

    if not template_path.exists():
        raise FileNotFoundError(f"Base template not found: {template_path}")

    with open(template_path, "r") as f:
        return json.load(f)


def build_source_settings(
    scenario: ScenarioConfig, presets_path: Optional[Path] = None, is_ci: bool = False
) -> dict[str, Any]:
    """
    Build the c64_source settings from preset + overrides + E2E defaults.

    Args:
        scenario: Parsed scenario configuration
        presets_path: Path to effect_presets.ini
        is_ci: Whether running in CI environment

    Returns:
        Dictionary of c64_source settings for OBS
    """
    # Start with preset settings
    settings = load_preset_settings(scenario.preset, presets_path)

    # Add the crt_preset name
    settings["crt_preset"] = scenario.preset

    # Apply overrides
    settings.update(scenario.overrides)

    # Add E2E testing defaults
    e2e_defaults = {
        "record_video": False,
        "record_csv": True,
        "save_frames": False,
        "debug_logging": True,
        "dns_server_ip": "127.0.0.1",
        "c64_host": "localhost",
        "auto_detect_ip": False,
        "obs_ip_address": "127.0.0.1",
        "video_port": 21000,
        "audio_port": 21001,
        "control_port": 6400,
        "buffer_delay_ms": 10,
    }
    for key, value in e2e_defaults.items():
        if key not in settings:
            settings[key] = value

    return settings


def generate_scene_json(
    scenario: ScenarioConfig, presets_path: Optional[Path] = None, template_path: Optional[Path] = None, is_ci: bool = False
) -> dict:
    """
    Generate a complete OBS scene JSON for a scenario.

    Args:
        scenario: Parsed scenario configuration
        presets_path: Path to effect_presets.ini
        template_path: Path to base_template.json
        is_ci: Whether running in CI environment

    Returns:
        Complete OBS scene JSON dictionary
    """
    # Load base template
    scene = load_base_template(template_path)

    # Build source settings
    source_settings = build_source_settings(scenario, presets_path, is_ci)

    # Find c64_source in the template and set its settings
    for source in scene.get("sources", []):
        if source.get("id") == "c64_source":
            source["settings"] = source_settings
            break

    return scene


def write_scene_json(
    scenario: ScenarioConfig,
    output_path: Path,
    presets_path: Optional[Path] = None,
    template_path: Optional[Path] = None,
    is_ci: bool = False,
) -> Path:
    """
    Generate and write an OBS scene JSON file for a scenario.

    Args:
        scenario: Parsed scenario configuration
        output_path: Path to write the generated JSON
        presets_path: Path to effect_presets.ini
        template_path: Path to base_template.json
        is_ci: Whether running in CI environment

    Returns:
        Path to the written JSON file
    """
    scene = generate_scene_json(scenario, presets_path, template_path, is_ci)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w") as f:
        json.dump(scene, f, indent=2)

    return output_path


def list_scenarios(scenarios_dir: Optional[Path] = None) -> list[str]:
    """
    List all available scenarios.

    Args:
        scenarios_dir: Path to scenarios directory (auto-detected if None)

    Returns:
        List of scenario directory names
    """
    if scenarios_dir is None:
        script_dir = Path(__file__).parent
        scenarios_dir = script_dir / "scenarios"

    scenarios = []
    for entry in sorted(scenarios_dir.iterdir()):
        if entry.is_dir():
            scenario_yaml = entry / "scenario.yaml"
            if scenario_yaml.exists():
                scenarios.append(entry.name)

    return scenarios


def get_scenario_summary(scenario_name: str, scenarios_dir: Optional[Path] = None) -> dict[str, Any]:
    """
    Get a summary of a scenario for display.

    Args:
        scenario_name: Name of the scenario directory
        scenarios_dir: Path to scenarios directory

    Returns:
        Dictionary with scenario summary info
    """
    if scenarios_dir is None:
        script_dir = Path(__file__).parent
        scenarios_dir = script_dir / "scenarios"

    scenario_path = scenarios_dir / scenario_name / "scenario.yaml"
    scenario = load_scenario(scenario_path)

    return {
        "name": scenario.name,
        "format": scenario.format,
        "preset": scenario.preset,
        "overrides": scenario.overrides,
        "assertions": scenario.assertions,
    }


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="C64 Stream E2E Scenario Loader")
    parser.add_argument("--list", action="store_true", help="List available scenarios")
    parser.add_argument("--scenario", type=str, help="Scenario name to load")
    parser.add_argument("--output", type=str, help="Output JSON path (optional)")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")

    args = parser.parse_args()

    if args.list:
        scenarios = list_scenarios()
        print("Available scenarios:")
        for name in scenarios:
            try:
                summary = get_scenario_summary(name)
                print(f"  {name}: {summary['name']} ({summary['format']}, preset: {summary['preset']})")
            except Exception as e:
                print(f"  {name}: (error loading: {e})")
    elif args.scenario:
        script_dir = Path(__file__).parent
        scenario_path = script_dir / "scenarios" / args.scenario / "scenario.yaml"
        scenario = load_scenario(scenario_path)

        if args.verbose:
            print(f"Loaded scenario: {scenario.name}")
            print(f"  Format: {scenario.format}")
            print(f"  Preset: {scenario.preset}")
            print(f"  Overrides: {scenario.overrides}")
            print(f"  Assertions: {scenario.assertions}")

        if args.output:
            output_path = Path(args.output)
            write_scene_json(scenario, output_path)
            print(f"Generated scene JSON: {output_path}")
        else:
            scene = generate_scene_json(scenario)
            print(json.dumps(scene, indent=2))
    else:
        parser.print_help()
