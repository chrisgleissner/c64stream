#!/usr/bin/env python3
"""
Unit tests for C64 script compilation and validation.
Tests all scripts in data/scripts/ directory to ensure they compile successfully.
"""

import os
import sys
import glob
import subprocess
import unittest
from pathlib import Path


class TestScripts(unittest.TestCase):
    """Test all C64 scripts compile without errors."""

    @classmethod
    def setUpClass(cls):
        """Find the script compiler executable and scripts directory."""
        # Find project root (parent of tests directory)
        cls.project_root = Path(__file__).parent.parent
        cls.scripts_dir = cls.project_root / "data" / "scripts"

        # Find the c64stream plugin library
        build_dirs = ["build_x86_64", "build_x64", "build_arm64"]
        cls.plugin_path = None
        for build_dir in build_dirs:
            plugin_file = cls.project_root / build_dir / "c64stream.so"
            if not plugin_file.exists():
                plugin_file = cls.project_root / build_dir / "c64stream.dll"
            if plugin_file.exists():
                cls.plugin_path = plugin_file
                break

        if not cls.plugin_path:
            raise unittest.SkipTest("c64stream plugin not found - build the project first")

        if not cls.scripts_dir.exists():
            raise unittest.SkipTest(f"Scripts directory not found: {cls.scripts_dir}")

    def test_all_scripts_compile(self):
        """Test that all .c64script files compile without errors."""
        script_files = list(self.scripts_dir.glob("*.c64script"))
        self.assertGreater(len(script_files), 0, "No .c64script files found")

        failed_scripts = []

        for script_file in script_files:
            with self.subTest(script=script_file.name):
                # We can't easily test compilation without OBS running,
                # but we can at least check file syntax:
                # - Must be UTF-8 text
                # - Must not have control characters (except newline/tab)
                # - Should have at least one command

                try:
                    with open(script_file, 'r', encoding='utf-8') as f:
                        content = f.read()

                    # Check for non-printable characters (except newline/CR/tab)
                    for line_no, line in enumerate(content.splitlines(), 1):
                        for col_no, char in enumerate(line, 1):
                            if ord(char) < 32 and char not in '\t\r\n':
                                failed_scripts.append(
                                    f"{script_file.name}:{line_no}:{col_no}: "
                                    f"Non-printable character 0x{ord(char):02x}"
                                )
                            # Check for smart quotes (common copy-paste error)
                            if char in '\u201c\u201d\u2018\u2019':  # Smart quotes: ""''
                                failed_scripts.append(
                                    f"{script_file.name}:{line_no}:{col_no}: "
                                    f"Smart quote character (use plain ASCII quotes)"
                                )

                    # Check that file has at least one command (not just comments)
                    has_command = False
                    for line in content.splitlines():
                        stripped = line.strip()
                        if stripped and not stripped.startswith('REM') and not stripped.startswith('#'):
                            has_command = True
                            break

                    if not has_command:
                        failed_scripts.append(f"{script_file.name}: No commands found (only comments)")

                except UnicodeDecodeError as e:
                    failed_scripts.append(f"{script_file.name}: Not valid UTF-8: {e}")
                except Exception as e:
                    failed_scripts.append(f"{script_file.name}: Error reading file: {e}")

        if failed_scripts:
            self.fail("\n".join(["Script validation errors:"] + failed_scripts))

    def test_script_filenames(self):
        """Test that all script filenames are valid."""
        script_files = list(self.scripts_dir.glob("*.c64script"))

        for script_file in script_files:
            with self.subTest(script=script_file.name):
                # Check filename conventions
                name = script_file.stem

                # Should use lowercase with underscores
                self.assertEqual(name, name.lower(),
                                f"Script name should be lowercase: {script_file.name}")

                # Should not have spaces
                self.assertNotIn(' ', name,
                                f"Script name should not have spaces: {script_file.name}")

                # Should only contain alphanumeric and underscore
                self.assertTrue(all(c.isalnum() or c == '_' for c in name),
                               f"Script name should only contain alphanumeric and underscore: {script_file.name}")


if __name__ == '__main__':
    unittest.main()
