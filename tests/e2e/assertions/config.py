#!/usr/bin/env python3
"""
C64 Stream - E2E Assertion Configuration
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import json
from configparser import ConfigParser
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional


@dataclass
class PresetConfig:
    """Configuration for an effect preset loaded from effect_presets.ini."""

    name: str
    scan_line_distance: float = 0.0
    scan_line_strength: float = 0.0
    pixel_width: float = 1.0
    pixel_height: float = 1.0
    blur_strength: float = 0.0
    bloom_strength: float = 0.0
    afterglow_duration_ms: int = 0
    afterglow_curve: int = 0
    tint_mode: int = 0  # 0=None, 1=Amber, 2=Green
    tint_strength: float = 0.0
    palette: str = "Default"  # Palette name

    @classmethod
    def from_ini_section(cls, name: str, section: dict[str, str]) -> "PresetConfig":
        """Create a PresetConfig from an INI section."""
        return cls(
            name=name,
            scan_line_distance=float(section.get("scan_line_distance", "0.0")),
            scan_line_strength=float(section.get("scan_line_strength", "0.0")),
            pixel_width=float(section.get("pixel_width", "1.0")),
            pixel_height=float(section.get("pixel_height", "1.0")),
            blur_strength=float(section.get("blur_strength", "0.0")),
            bloom_strength=float(section.get("bloom_strength", "0.0")),
            afterglow_duration_ms=int(section.get("afterglow_duration_ms", "0")),
            afterglow_curve=int(section.get("afterglow_curve", "0")),
            tint_mode=int(section.get("tint_mode", "0")),
            tint_strength=float(section.get("tint_strength", "0.0")),
            palette=section.get("palette", "Default"),
        )

    @classmethod
    def from_obs_settings(cls, settings: dict[str, Any]) -> "PresetConfig":
        """Create a PresetConfig from OBS source settings dict."""
        return cls(
            name=settings.get("crt_preset", "Custom"),
            scan_line_distance=float(settings.get("scan_line_distance", 0.0)),
            scan_line_strength=float(settings.get("scan_line_strength", 0.0)),
            pixel_width=float(settings.get("pixel_width", 1.0)),
            pixel_height=float(settings.get("pixel_height", 1.0)),
            blur_strength=float(settings.get("blur_strength", 0.0)),
            bloom_strength=float(settings.get("bloom_strength", 0.0)),
            afterglow_duration_ms=int(settings.get("afterglow_duration_ms", 0)),
            afterglow_curve=int(settings.get("afterglow_curve", 0)),
            tint_mode=int(settings.get("tint_mode", 0)),
            tint_strength=float(settings.get("tint_strength", 0.0)),
            palette=settings.get("palette", "Default"),
        )

    def has_scanlines(self) -> bool:
        return self.scan_line_distance > 0.0 and self.scan_line_strength > 0.0

    def has_afterglow(self) -> bool:
        return self.afterglow_duration_ms > 0

    def has_tint(self) -> bool:
        return self.tint_mode > 0 and self.tint_strength > 0.0

    def tint_type(self) -> Optional[str]:
        if self.tint_mode == 1:
            return "amber"
        elif self.tint_mode == 2:
            return "green"
        return None


def load_settings_from_obs_scene(scene_json_path: Path) -> dict[str, Any]:
    """Load c64_source settings from OBS scene JSON file.

    Args:
        scene_json_path: Path to the C64StreamTest.json scene file

    Returns:
        Dict of source settings (e.g., crt_preset, scan_line_distance, etc.)
    """
    with open(scene_json_path) as f:
        scene = json.load(f)

    # Find the c64_source in sources
    for source in scene.get("sources", []):
        if source.get("id") == "c64_source":
            return source.get("settings", {})

    return {}


def load_preset_from_ini(preset_name: str, presets_ini_path: Path) -> Optional[PresetConfig]:
    """Load a preset configuration from effect_presets.ini."""
    if not presets_ini_path.exists():
        return None

    parser = ConfigParser()
    parser.read(presets_ini_path)

    # Normalize preset name for section lookup
    section_name = preset_name.replace(" ", "_").lower()

    for section in parser.sections():
        if section.lower().replace(" ", "_") == section_name:
            return PresetConfig.from_ini_section(preset_name, dict(parser.items(section)))

    return None


def load_properties(properties_path: Path) -> dict[str, Any]:
    """Load properties.ini as a dict."""
    parser = ConfigParser()
    parser.read(properties_path)
    return {section: dict(parser.items(section)) for section in parser.sections()}
