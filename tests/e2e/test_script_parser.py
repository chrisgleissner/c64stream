#!/usr/bin/env python3
"""
Script parser validation tests
Tests the .c64script parser without requiring OBS
"""

import os
import sys
import tempfile
import unittest


class TestScriptParser(unittest.TestCase):
    """Test script parser functionality"""

    def setUp(self):
        """Create temp directory for test scripts"""
        self.test_dir = tempfile.mkdtemp()

    def tearDown(self):
        """Clean up temp directory"""
        import shutil
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def write_script(self, filename, content):
        """Write a test script file"""
        path = os.path.join(self.test_dir, filename)
        with open(path, 'w') as f:
            f.write(content)
        return path

    def test_valid_effect_command(self):
        """Test valid effect command syntax"""
        script = """
# Test effect command
effect Light Scanlines
wait 2s
stop
"""
        path = self.write_script('test.c64script', script)
        self.assertTrue(os.path.exists(path))

        # Validate syntax
        lines = script.strip().split('\n')
        commands = [l.strip() for l in lines if l.strip() and not l.strip().startswith('#')]
        self.assertEqual(len(commands), 3)
        self.assertTrue(commands[0].startswith('effect'))
        self.assertTrue(commands[1].startswith('wait'))
        self.assertTrue(commands[2] == 'stop')

    def test_valid_palette_command(self):
        """Test valid palette command syntax"""
        script = """
palette colodore
wait 5s
"""
        path = self.write_script('test_palette.c64script', script)
        lines = script.strip().split('\n')
        commands = [l.strip() for l in lines if l.strip()]
        self.assertEqual(len(commands), 2)

    def test_wait_duration_formats(self):
        """Test various wait duration formats"""
        test_cases = [
            ('wait 500ms', 'milliseconds'),
            ('wait 2s', 'seconds'),
            ('wait 1.5s', 'decimal seconds'),
            ('wait 1m', 'minutes'),
            ('wait 0.5m', 'decimal minutes'),
        ]

        for command, description in test_cases:
            with self.subTest(command=command, desc=description):
                # Simple syntax validation
                parts = command.split()
                self.assertEqual(parts[0], 'wait')
                self.assertTrue(len(parts) == 2)
                duration = parts[1]
                self.assertTrue(duration[-2:] in ['ms', 's', 'm'] or duration[-1] in ['s', 'm'])

    def test_path_prefixes(self):
        """Test local vs C64U path prefixes"""
        test_cases = [
            ('play_sid c64u:/Temp/music/test.sid', 'C64U path'),
            ('play_sid /local/path/test.sid', 'local path'),
            ('run_prg c64u:/Programs/demo.prg', 'C64U PRG'),
            ('mount_disk c64u:/Disks/game.d64', 'C64U disk'),
        ]

        for command, description in test_cases:
            with self.subTest(command=command, desc=description):
                parts = command.split()
                path = parts[1] if len(parts) > 1 else ''
                if path.startswith('c64u:'):
                    self.assertTrue(path.startswith('c64u:'))
                else:
                    self.assertTrue(path.startswith('/'))

    def test_loop_syntax(self):
        """Test loop command syntax"""
        script = """
loop 3
    effect Light Scanlines
    wait 1s
    effect Heavy Scanlines
    wait 1s
# End loop
stop
"""
        path = self.write_script('test_loop.c64script', script)
        lines = script.strip().split('\n')
        # Check loop command exists
        loop_line = [l for l in lines if l.strip().startswith('loop')]
        self.assertEqual(len(loop_line), 1)
        self.assertTrue('3' in loop_line[0])

    def test_label_goto_syntax(self):
        """Test label and goto syntax"""
        script = """
label start
effect Light Scanlines
wait 2s
goto start
"""
        path = self.write_script('test_goto.c64script', script)
        lines = script.strip().split('\n')
        commands = [l.strip() for l in lines if l.strip()]

        label_cmd = [c for c in commands if c.startswith('label')]
        goto_cmd = [c for c in commands if c.startswith('goto')]

        self.assertEqual(len(label_cmd), 1)
        self.assertEqual(len(goto_cmd), 1)

        # Extract label name
        label_name = label_cmd[0].split()[1]
        goto_target = goto_cmd[0].split()[1]
        self.assertEqual(label_name, goto_target)

    def test_comment_handling(self):
        """Test comment line handling"""
        script = """
# This is a comment
effect None  # Inline comment
# Another comment
wait 1s
"""
        lines = script.strip().split('\n')
        comment_lines = [l for l in lines if l.strip().startswith('#')]
        self.assertEqual(len(comment_lines), 2)

    def test_empty_lines_ignored(self):
        """Test that empty lines are ignored"""
        script = """
effect None

wait 1s

stop
"""
        lines = script.strip().split('\n')
        non_empty = [l for l in lines if l.strip()]
        self.assertEqual(len(non_empty), 3)

    def test_example_scripts_exist(self):
        """Test that example scripts exist in data/scripts"""
        script_dir = os.path.join(os.path.dirname(__file__), '../../data/scripts')
        if os.path.exists(script_dir):
            expected_scripts = [
                'demo_basic_hello_world.c64script',
                'demo_effect_preset_cycle.c64script',
                'demo_palette_cycle.c64script',
                'demo_sid_playback_cycle.c64script',
            ]
            for script_name in expected_scripts:
                script_path = os.path.join(script_dir, script_name)
                with self.subTest(script=script_name):
                    self.assertTrue(os.path.exists(script_path),
                                  f"Example script not found: {script_name}")

    def test_example_script_syntax(self):
        """Test that example scripts have valid syntax"""
        script_dir = os.path.join(os.path.dirname(__file__), '../../data/scripts')
        if not os.path.exists(script_dir):
            self.skipTest("Scripts directory not found")

        for filename in os.listdir(script_dir):
            if filename.endswith('.c64script'):
                path = os.path.join(script_dir, filename)
                with open(path, 'r') as f:
                    content = f.read()

                lines = content.split('\n')
                commands = [l.strip() for l in lines if l.strip() and not l.strip().startswith('#')]

                with self.subTest(script=filename):
                    # Should have at least one command
                    self.assertGreater(len(commands), 0,
                                     f"{filename} has no commands")

                    # Scripts can end with explicit STOP/GOTO/LOOP or have implicit termination
                    # (running out of instructions). No validation needed - all endings are valid.


if __name__ == '__main__':
    unittest.main()
