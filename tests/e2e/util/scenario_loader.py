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
from math import floor
from pathlib import Path
from typing import Any, Optional

import yaml


def validate_scenario_name(name: str) -> None:
    parts = name.split("_")
    if not name or name.lower() != name:
        raise ValueError(f"Scenario '{name}' must be lowercase")
    if len(parts) > 4:
        raise ValueError(f"Scenario '{name}' has {len(parts)} segments; max is 4")
    for part in parts:
        if not part or len(part) > 9:
            raise ValueError(
                f"Scenario '{name}' has invalid segment '{part}' (segments must be 1-9 chars)"
            )


def validate_scenario_prefix(name: str, display_name: str, overrides: dict[str, Any]) -> None:
    scripted_prefixes = ("ntsc_script", "pal_script")
    has_script_prefix = name.startswith(scripted_prefixes)
    display_has_script = "script" in display_name.lower().split()
    has_script_file = bool(overrides.get("script_file"))
    auto_start = bool(overrides.get("script_auto_start"))

    if has_script_prefix:
        if not display_has_script:
            raise ValueError(f"Scenario '{name}' must include 'Script' in its display name")
        if not has_script_file or not auto_start:
            raise ValueError(
                f"Scenario '{name}' must define script_file and script_auto_start when using a script prefix"
            )

    if display_has_script and not has_script_prefix:
        raise ValueError(
            f"Scenario '{name}' uses a script-centric display name and must use an ntsc_script_/pal_script_ prefix"
        )


def _e2e_dir() -> Path:
    # tests/e2e/util/scenario_loader.py -> parents[1] == tests/e2e
    return Path(__file__).resolve().parents[1]


def _repo_root() -> Path:
    # tests/e2e/util/scenario_loader.py -> parents[3] == repo root
    return Path(__file__).resolve().parents[3]


@dataclass
class ScenarioConfig:
    """Parsed scenario configuration."""

    name: str
    format: str  # PAL or NTSC
    preset: str
    overrides: dict[str, Any] = field(default_factory=dict)
    network_simulation: dict[str, Any] = field(default_factory=dict)
    assertions: list[str] = field(default_factory=list)
    thresholds: dict[str, dict[str, float]] = field(default_factory=dict)
    tolerances: dict[str, Any] = field(default_factory=dict)
    fixed_canvas_bounds: bool = False
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
        repo_root = _repo_root()
        candidates = [
            repo_root / "data" / "effect_presets.ini",
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

    validate_scenario_name(scenario_path.parent.name)

    with open(scenario_path, "r") as f:
        data = yaml.safe_load(f)

    validate_scenario_prefix(
        scenario_path.parent.name,
        data.get("name", ""),
        data.get("overrides", {}),
    )

    return ScenarioConfig(
        name=data.get("name", ""),
        format=data.get("format", "NTSC"),
        preset=data.get("preset", "Default"),
        overrides=data.get("overrides", {}),
        network_simulation=data.get("network_simulation", {}),
        assertions=data.get("assertions", ["video_quality", "audio"]),
        thresholds=data.get("thresholds", {}),
        tolerances=data.get("tolerances", {}),
        fixed_canvas_bounds=bool(data.get("fixed_canvas_bounds", False)),
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
        template_path = _e2e_dir() / "scenarios" / "base_template.json"

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

    repo_root = _repo_root()
    for key, value in list(settings.items()):
        if isinstance(value, str):
            value = value.replace("${repo_root}", str(repo_root))
            value = value.replace("${REPO_ROOT}", str(repo_root))
            settings[key] = value

    # Add E2E testing defaults
    e2e_defaults = {
        "record_video": False,
        "record_csv": True,
        "record_frames": False,
        "debug_logging": True,
        "dns_server_ip": "127.0.0.1",
        "c64_host": "localhost",
        "auto_detect_ip": False,
        "obs_ip_address": "127.0.0.1",
        "video_port": 21000,
        "audio_port": 21001,
        "control_port": 6400,
        "buffer_delay_ms": 10,
        # Force the legacy TCP transport (port 6400) like the static baseline
        # scene config: the mock C64U serves REST only when a scenario opts in
        # via mock_rest_enabled (binding port 80 needs privileges), and the
        # AUTO transport does not fall back to legacy on an unreachable REST
        # endpoint. Transport scenarios override this explicitly.
        "stream_control_transport": 2,
    }
    for key, value in e2e_defaults.items():
        if key not in settings:
            settings[key] = value

    return settings


def _get_canvas_size_from_scenario(scenario: ScenarioConfig) -> tuple[float, float]:
    """
    Get the OBS canvas size from scenario overrides.

    Reads BaseCX/BaseCY from overrides/basic/profiles/C64StreamTest/basic.ini if it exists.
    Falls back to 1920x1080 if not specified.

    Args:
        scenario: Parsed scenario configuration

    Returns:
        Tuple of (canvas_width, canvas_height)
    """
    # Default to 1080p
    canvas_w = 1920.0
    canvas_h = 1080.0

    # Check for basic.ini override
    basic_ini_path = scenario.scenario_dir / "overrides" / "basic" / "profiles" / "C64StreamTest" / "basic.ini"
    if basic_ini_path.exists():
        config = ConfigParser()
        config.read(basic_ini_path)
        if "Video" in config:
            video_section = config["Video"]
            if "BaseCX" in video_section and "BaseCY" in video_section:
                try:
                    canvas_w = float(video_section["BaseCX"])
                    canvas_h = float(video_section["BaseCY"])
                except (ValueError, TypeError):
                    pass  # Keep defaults

    return canvas_w, canvas_h


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

    # Get canvas size from scenario overrides (if specified in basic.ini)
    canvas_w, canvas_h = _get_canvas_size_from_scenario(scenario)

    def _scanline_scale_for_distance(scan_line_distance: float) -> int:
        """Map scan line distance to the integer scale described in README.md."""
        mapping = {
            0.0: 4,   # None
            0.25: 5,  # Tight
            0.5: 3,   # Normal
            1.0: 4,   # Wide
            2.0: 3,   # Extra Wide
        }
        # Snap to nearest known mode within tolerance.
        try:
            d = float(scan_line_distance)
        except Exception:
            return 4
        nearest = min(mapping.keys(), key=lambda k: abs(d - k))
        if abs(d - nearest) <= 0.02:
            return mapping[nearest]
        # Fallback: keep integer scale (avoid non-integer scaling in OBS).
        return 4

    def _apply_pixel_perfect_scene_item(
        scene_json: dict,
        *,
        fmt: str,
        settings: dict[str, Any],
        canvas_w: float,
        canvas_h: float,
        fixed_canvas_bounds: bool,
    ) -> None:
        """Update the scene item transform so OBS doesn't introduce interpolation blur."""
        if fixed_canvas_bounds:
            for src in scene_json.get('sources', []):
                if src.get('id') != 'scene':
                    continue
                items = (((src.get('settings') or {}).get('items')) or [])
                for item in items:
                    if item.get('name') != 'C64 Stream':
                        continue
                    item['align'] = 0
                    item['pos'] = {'x': float(canvas_w / 2.0), 'y': float(canvas_h / 2.0)}
                    item['scale'] = {'x': 1.0, 'y': 1.0}
                    item['bounds_type'] = 2
                    item['bounds_align'] = 0
                    item['bounds'] = {'x': float(canvas_w), 'y': float(canvas_h)}
                    item['scale_filter'] = 'point'
                    item['crop_left'] = 0
                    item['crop_right'] = 0
                    item['crop_top'] = 0
                    item['crop_bottom'] = 0
                    return

        # The plugin itself can change its reported base dimensions via effect settings:
        # - pixel_width / pixel_height
        # - scan_line_distance (adds integer upscaling to create scanline/gap structure)
        #
        # Model the source base size the same way as the plugin (`c64_get_width/height`) so we can
        # pick an integer OBS scale and crop without introducing interpolation.
        base_w = 384.0
        base_h = 272.0 if str(fmt).upper() == 'PAL' else 240.0

        pixel_w = float(settings.get('pixel_width', 1.0) or 1.0)
        pixel_h = float(settings.get('pixel_height', 1.0) or 1.0)
        scan_line_distance = float(settings.get('scan_line_distance', 0.0) or 0.0)
        preserve_size = bool(settings.get('preserve_size', True))

        scanline_unit = 1.0
        if scan_line_distance > 0.0:
            # Must match `get_scanline_scaling_info()` in src/c64-source.c
            if scan_line_distance <= 0.25:
                scanline_unit = 5.0
            elif scan_line_distance <= 0.5:
                scanline_unit = 3.0
            elif scan_line_distance <= 1.0:
                scanline_unit = 4.0
            else:
                scanline_unit = 3.0

        virtual_w = base_w * pixel_w * scanline_unit
        virtual_h = base_h * pixel_h * scanline_unit
        source_w = base_w if preserve_size else virtual_w
        source_h = base_h if preserve_size else virtual_h

        # Choose the largest integer scale that fits without requiring crop.
        # (Crop is still supported for small overflows caused by rounding.)
        fit_scale = min(canvas_w / source_w, canvas_h / source_h)
        scale = float(max(1, int(floor(fit_scale))))

        scaled_w = source_w * scale
        scaled_h = source_h * scale

        # Crop in *source pixels* so that scaled result fits the canvas without resampling.
        overflow_x = max(0.0, scaled_w - canvas_w)
        overflow_y = max(0.0, scaled_h - canvas_h)

        crop_left = int(floor((overflow_x / 2.0) / scale))
        crop_right = int(floor((overflow_x - (crop_left * scale)) / scale))
        crop_top = int(floor((overflow_y / 2.0) / scale))
        crop_bottom = int(floor((overflow_y - (crop_top * scale)) / scale))

        # Clamp crops to valid ranges.
        crop_left = max(0, min(crop_left, int(source_w)))
        crop_right = max(0, min(crop_right, int(source_w) - crop_left))
        crop_top = max(0, min(crop_top, int(source_h)))
        crop_bottom = max(0, min(crop_bottom, int(source_h) - crop_top))

        displayed_w = (source_w - crop_left - crop_right) * scale
        displayed_h = (source_h - crop_top - crop_bottom) * scale
        pos_x = (canvas_w - displayed_w) / 2.0
        pos_y = (canvas_h - displayed_h) / 2.0

        # Locate scene item named "C64 Stream".
        for src in scene_json.get('sources', []):
            if src.get('id') != 'scene':
                continue
            items = (((src.get('settings') or {}).get('items')) or [])
            for item in items:
                if item.get('name') != 'C64 Stream':
                    continue
                item['align'] = 5
                item['pos'] = {'x': float(pos_x), 'y': float(pos_y)}
                item['scale'] = {'x': float(scale), 'y': float(scale)}
                # Disable bounds scaling: we want exact integer scaling via `scale` only.
                # Bounds-based scaling can introduce non-integer resampling depending on the source's base size.
                item['bounds_type'] = 0
                item['bounds'] = {'x': 0.0, 'y': 0.0}
                item['scale_filter'] = 'point'
                item['crop_left'] = int(crop_left)
                item['crop_right'] = int(crop_right)
                item['crop_top'] = int(crop_top)
                item['crop_bottom'] = int(crop_bottom)
                return

    # Find c64_source in the template and set its settings
    for source in scene.get("sources", []):
        if source.get("id") == "c64_source":
            source["settings"] = source_settings
            break

    # Ensure the scene item transform uses integer scaling and point filtering
    _apply_pixel_perfect_scene_item(
        scene,
        fmt=scenario.format,
        settings=source_settings,
        canvas_w=canvas_w,
        canvas_h=canvas_h,
        fixed_canvas_bounds=scenario.fixed_canvas_bounds,
    )

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
        scenarios_dir = _e2e_dir() / "scenarios"

    scenarios = []
    for entry in sorted(scenarios_dir.iterdir()):
        if entry.is_dir():
            scenario_yaml = entry / "scenario.yaml"
            if scenario_yaml.exists():
                validate_scenario_name(entry.name)
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
        scenarios_dir = _e2e_dir() / "scenarios"

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
        scenario_path = _e2e_dir() / "scenarios" / args.scenario / "scenario.yaml"
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
