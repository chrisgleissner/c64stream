#!/usr/bin/env python3
"""
C64 Stream - Scenario Loader Unit Tests
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


def _load_scenario_loader():
    here = Path(__file__).resolve().parent
    module_path = here / "scenario_loader.py"
    spec = importlib.util.spec_from_file_location("c64stream_scenario_loader", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError("failed to load scenario_loader.py module spec")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TestScenarioLoader(unittest.TestCase):
    def setUp(self):
        self.loader = _load_scenario_loader()

    def _write_scenario(self, scenario_dir: Path, *, name: str, overrides: str = "") -> Path:
        scenario_dir.mkdir(parents=True, exist_ok=True)
        scenario_yaml = scenario_dir / "scenario.yaml"
        body = f"name: {name}\nformat: NTSC\npreset: Default\n"
        if overrides:
            body += overrides
        scenario_yaml.write_text(body, encoding="utf-8")
        return scenario_yaml

    def test_repository_scenarios_load_cleanly(self):
        scenarios_dir = Path(__file__).resolve().parents[1] / "scenarios"
        for scenario_yaml in sorted(scenarios_dir.glob("*/scenario.yaml")):
            with self.subTest(scenario=scenario_yaml.parent.name):
                scenario = self.loader.load_scenario(scenario_yaml)
                self.assertTrue(scenario.name)
                self.assertEqual(scenario.scenario_dir, scenario_yaml.parent)

    def test_script_prefix_requires_script_metadata(self):
        with tempfile.TemporaryDirectory() as td:
            scenario_yaml = self._write_scenario(
                Path(td) / "ntsc_script_invalid",
                name="NTSC Script Invalid",
            )

            with self.assertRaisesRegex(ValueError, "script_file and script_auto_start"):
                self.loader.load_scenario(scenario_yaml)

    def test_script_display_name_requires_script_prefix(self):
        with tempfile.TemporaryDirectory() as td:
            scenario_yaml = self._write_scenario(
                Path(td) / "ntsc_invalid",
                name="NTSC Script Invalid",
                overrides=(
                    "overrides:\n"
                    "  script_file: /tmp/test.c64script\n"
                    "  script_auto_start: true\n"
                ),
            )

            with self.assertRaisesRegex(ValueError, "must use an ntsc_script_/pal_script_ prefix"):
                self.loader.load_scenario(scenario_yaml)

    def test_list_scenarios_rejects_invalid_directory_name(self):
        with tempfile.TemporaryDirectory() as td:
            scenarios_dir = Path(td)
            self._write_scenario(scenarios_dir / "bad_name_too_many_parts_here", name="Bad Name")

            with self.assertRaisesRegex(ValueError, "max is 4"):
                self.loader.list_scenarios(scenarios_dir)


if __name__ == "__main__":
    unittest.main()
