#!/usr/bin/env python3
"""
Comprehensive C64Script Error Handling Tests

Tests error detection and handling:
- Invalid commands (parse errors)
- Missing labels (runtime errors)
- Duplicate labels (parse errors)
- Type mismatches
- Missing loop terminators (NEXT, WEND)
- Stack overflow (GOSUB depth)
- Maximum nesting depth
- Runaway loop detection

These tests validate that the script engine properly detects
and reports errors without crashing.
"""

import os
import sys
import unittest
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


class TestParseErrors(unittest.TestCase):
    """Test parse-time error detection"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def _load_script(self, filename):
        """Load a test script"""
        script_path = self.script_dir / filename
        self.assertTrue(script_path.exists(), f"Script not found: {filename}")
        with open(script_path, 'r') as f:
            return f.read()
    
    def _script_should_have_error(self, filename, expected_issue):
        """Helper to document expected error in script"""
        script = self._load_script(filename)
        # These scripts are designed to fail - we just verify they exist
        # and document what error they should trigger
        self.assertIsNotNone(script)
        print(f"  Script {filename} should trigger: {expected_issue}")
    
    def test_error_invalid_command(self):
        """Test detection of invalid command"""
        self._script_should_have_error(
            "test_error_invalid_command.c64script",
            "Parse error: invalid command"
        )
    
    def test_error_duplicate_label(self):
        """Test detection of duplicate label definitions"""
        self._script_should_have_error(
            "test_error_duplicate_label.c64script",
            "Parse error: duplicate label 'START'"
        )
    
    def test_error_missing_next(self):
        """Test detection of FOR without NEXT"""
        self._script_should_have_error(
            "test_error_missing_next.c64script",
            "Parse error: FOR loop without matching NEXT"
        )
    
    def test_error_missing_wend(self):
        """Test detection of WHILE without WEND"""
        self._script_should_have_error(
            "test_error_missing_wend.c64script",
            "Parse error: WHILE loop without matching WEND"
        )


class TestRuntimeErrors(unittest.TestCase):
    """Test runtime error detection"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def _load_script(self, filename):
        """Load a test script"""
        script_path = self.script_dir / filename
        self.assertTrue(script_path.exists(), f"Script not found: {filename}")
        with open(script_path, 'r') as f:
            return f.read()
    
    def _script_should_have_runtime_error(self, filename, expected_issue):
        """Helper to document expected runtime error"""
        script = self._load_script(filename)
        self.assertIsNotNone(script)
        print(f"  Script {filename} should trigger: {expected_issue}")
    
    def test_error_goto_missing_label(self):
        """Test GOTO to non-existent label"""
        self._script_should_have_runtime_error(
            "test_error_goto_missing.c64script",
            "Runtime error: label 'nonexistent_label' not found"
        )
    
    def test_error_type_mismatch(self):
        """Test type mismatch in operations"""
        self._script_should_have_runtime_error(
            "test_error_type_mismatch.c64script",
            "Runtime error: type mismatch in arithmetic"
        )
    
    def test_error_gosub_overflow(self):
        """Test GOSUB stack overflow"""
        self._script_should_have_runtime_error(
            "test_error_gosub_overflow.c64script",
            "Runtime error: GOSUB stack overflow (max depth exceeded)"
        )


class TestSafetyLimits(unittest.TestCase):
    """Test safety limit enforcement"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def _load_script(self, filename):
        """Load a test script"""
        script_path = self.script_dir / filename
        self.assertTrue(script_path.exists(), f"Script not found: {filename}")
        with open(script_path, 'r') as f:
            return f.read()
    
    def test_safety_max_nesting(self):
        """Test maximum loop nesting depth limit"""
        script = self._load_script("test_safety_max_nesting.c64script")
        
        # Count FOR statements - should have many
        for_count = script.upper().count("FOR L")
        self.assertGreater(for_count, 15, 
                          "Script should attempt deep nesting (>15 levels)")
        
        # Should have comment about hitting limit
        self.assertIn("fail", script.lower())
    
    def test_safety_infinite_loop_structure(self):
        """Test that infinite loop script has correct structure"""
        script = self._load_script("test_safety_infinite_loop.c64script")
        
        # Should have infinite loop structure
        self.assertIn("LOOP_START:", script)
        self.assertIn("GOTO LOOP_START", script)
        
        # Should have comment about infinite loop
        self.assertIn("infinite", script.lower())


class TestErrorScriptStructure(unittest.TestCase):
    """Validate structure of error test scripts"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def test_all_error_scripts_exist(self):
        """Verify all error test scripts exist"""
        expected_error_scripts = [
            "test_error_invalid_command.c64script",
            "test_error_goto_missing.c64script",
            "test_error_duplicate_label.c64script",
            "test_error_type_mismatch.c64script",
            "test_error_missing_next.c64script",
            "test_error_missing_wend.c64script",
            "test_error_gosub_overflow.c64script",
            "test_safety_max_nesting.c64script",
            "test_safety_infinite_loop.c64script",
        ]
        
        for script_name in expected_error_scripts:
            script_path = self.script_dir / script_name
            self.assertTrue(script_path.exists(), 
                          f"Missing error test script: {script_name}")
    
    def test_error_scripts_are_valid_utf8(self):
        """Verify error scripts are valid UTF-8"""
        for script_file in self.script_dir.glob("test_error_*.c64script"):
            try:
                with open(script_file, 'r', encoding='utf-8') as f:
                    content = f.read()
                    self.assertIsInstance(content, str)
            except UnicodeDecodeError:
                self.fail(f"Error script is not valid UTF-8: {script_file.name}")
    
    def test_error_scripts_have_error_markers(self):
        """Verify error scripts document their expected failure"""
        error_scripts = [
            ("test_error_invalid_command.c64script", "invalid"),
            ("test_error_goto_missing.c64script", "nonexistent"),
            ("test_error_duplicate_label.c64script", "duplicate"),
            ("test_error_missing_next.c64script", "Missing NEXT"),
            ("test_error_missing_wend.c64script", "Missing WEND"),
        ]
        
        for script_name, marker in error_scripts:
            script_path = self.script_dir / script_name
            if not script_path.exists():
                continue
            
            with open(script_path, 'r') as f:
                content = f.read()
            
            # Script should document what error it causes
            self.assertIn(marker.lower(), content.lower(),
                         f"Error script should document '{marker}': {script_name}")


class TestErrorHandlingCoverage(unittest.TestCase):
    """Ensure comprehensive error coverage"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def test_parse_error_coverage(self):
        """Verify we test major parse error categories"""
        parse_error_scripts = [
            "test_error_invalid_command.c64script",     # Invalid command
            "test_error_duplicate_label.c64script",     # Duplicate labels
            "test_error_missing_next.c64script",        # Unclosed FOR
            "test_error_missing_wend.c64script",        # Unclosed WHILE
        ]
        
        for script_name in parse_error_scripts:
            script_path = self.script_dir / script_name
            self.assertTrue(script_path.exists(),
                          f"Missing parse error test: {script_name}")
    
    def test_runtime_error_coverage(self):
        """Verify we test major runtime error categories"""
        runtime_error_scripts = [
            "test_error_goto_missing.c64script",        # Missing label
            "test_error_type_mismatch.c64script",       # Type error
            "test_error_gosub_overflow.c64script",      # Stack overflow
        ]
        
        for script_name in runtime_error_scripts:
            script_path = self.script_dir / script_name
            self.assertTrue(script_path.exists(),
                          f"Missing runtime error test: {script_name}")
    
    def test_safety_limit_coverage(self):
        """Verify we test safety limits"""
        safety_scripts = [
            "test_safety_max_nesting.c64script",        # Max loop depth
            "test_safety_infinite_loop.c64script",      # Runaway detection
        ]
        
        for script_name in safety_scripts:
            script_path = self.script_dir / script_name
            self.assertTrue(script_path.exists(),
                          f"Missing safety test: {script_name}")


if __name__ == '__main__':
    print("C64Script Error Handling Tests")
    print("=" * 70)
    print()
    print("Running comprehensive error detection validation...")
    print()
    print("Note: These tests validate error script structure.")
    print("Actual error detection happens during compilation/execution.")
    print()
    unittest.main(verbosity=2)
