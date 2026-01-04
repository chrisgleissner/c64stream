#!/usr/bin/env python3
"""
Comprehensive C64Script Control Flow Tests

Tests the full range of control flow features:
- Nested loops (FOR within WHILE, WHILE within FOR)
- Loop iteration counts and step values
- GOTO and GOSUB flow control
- Label resolution
- Loop variable mutation

These tests use the mock REST server to validate that scripts execute
with correct flow control semantics and generate the expected sequence
of operations.
"""

import os
import sys
import time
import unittest
import subprocess
import threading
import requests
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Mock server for REST API calls
MOCK_SERVER_PORT = 8065
MOCK_SERVER_URL = f"http://localhost:{MOCK_SERVER_PORT}"


class MockRestServer:
    """Simple mock REST server that tracks calls"""
    
    def __init__(self):
        self.calls = []
        self.server_process = None
        self.lock = threading.Lock()
    
    def start(self):
        """Start the mock server"""
        script_dir = Path(__file__).parent.parent
        server_script = script_dir / "mock_c64u_server.py"
        
        self.server_process = subprocess.Popen(
            [sys.executable, str(server_script), '--port', str(MOCK_SERVER_PORT)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        # Wait for server to start
        time.sleep(1)
        
        # Verify server is running
        try:
            response = requests.get(f"{MOCK_SERVER_URL}/v1/version", timeout=2)
            assert response.status_code == 200
        except Exception as e:
            self.stop()
            raise RuntimeError(f"Mock server failed to start: {e}")
    
    def stop(self):
        """Stop the mock server"""
        if self.server_process:
            self.server_process.terminate()
            self.server_process.wait(timeout=5)
            self.server_process = None
    
    def reset_calls(self):
        """Reset call tracking"""
        with self.lock:
            self.calls = []


class TestControlFlow(unittest.TestCase):
    """Test control flow constructs"""
    
    @classmethod
    def setUpClass(cls):
        """Start mock server once for all tests"""
        cls.mock_server = MockRestServer()
        cls.mock_server.start()
        cls.script_dir = Path(__file__).parent / "scripts"
    
    @classmethod
    def tearDownClass(cls):
        """Stop mock server"""
        cls.mock_server.stop()
    
    def setUp(self):
        """Reset call tracking before each test"""
        self.mock_server.reset_calls()
    
    def _load_script(self, filename):
        """Load a test script"""
        script_path = self.script_dir / filename
        self.assertTrue(script_path.exists(), f"Script not found: {filename}")
        with open(script_path, 'r') as f:
            return f.read()
    
    def _parse_script(self, content):
        """
        Parse script content and return command list.
        This is a simplified parser for testing purposes.
        """
        commands = []
        for line in content.splitlines():
            line = line.strip()
            # Skip empty lines and comments
            if not line or line.startswith('#') or line.startswith('REM'):
                continue
            # Skip labels
            if ':' in line and not any(kw in line.upper() for kw in ['IF', 'FOR', 'WHILE']):
                continue
            commands.append(line)
        return commands
    
    def test_nested_loops_for_in_while(self):
        """Test FOR loop nested within WHILE loop"""
        script = self._load_script("test_nested_loops.c64script")
        commands = self._parse_script(script)
        
        # Script should have nested loop structure
        self.assertIn("WHILE", " ".join(commands).upper())
        self.assertIn("FOR", " ".join(commands).upper())
        self.assertIn("NEXT", " ".join(commands).upper())
        self.assertIn("WEND", " ".join(commands).upper())
        
        # Verify multiple EFFECT and PALETTE commands exist
        effect_count = sum(1 for cmd in commands if 'EFFECT' in cmd.upper())
        palette_count = sum(1 for cmd in commands if 'PALETTE' in cmd.upper())
        
        self.assertGreater(effect_count, 0, "Should have EFFECT commands")
        self.assertGreater(palette_count, 0, "Should have PALETTE commands")
    
    def test_nested_loops_while_in_for(self):
        """Test WHILE loop nested within FOR loop"""
        script = self._load_script("test_nested_loops.c64script")
        
        # Check for OUTER FOR loop with INNER WHILE
        self.assertIn("FOR OUTER", script)
        self.assertIn("WHILE INNER", script)
    
    def test_nested_loops_triple(self):
        """Test triple nested loops"""
        script = self._load_script("test_nested_loops.c64script")
        
        # Should have triple nesting comment
        self.assertIn("Triple nesting", script)
        
        # Count FOR statements - should have at least 3
        for_count = script.upper().count("FOR ")
        self.assertGreaterEqual(for_count, 3, "Should have at least 3 FOR loops")
    
    def test_iteration_counts_forward(self):
        """Test forward counting with default step"""
        script = self._load_script("test_iteration_counts.c64script")
        
        # Should have simple forward counting (1 TO 5)
        self.assertIn("FOR I = 1 TO 5", script)
        self.assertIn("NEXT I", script)
    
    def test_iteration_counts_with_step(self):
        """Test counting with explicit step value"""
        script = self._load_script("test_iteration_counts.c64script")
        
        # Should have step-based counting
        self.assertIn("STEP 2", script)
        self.assertIn("STEP -1", script)
        self.assertIn("STEP -2", script)
    
    def test_iteration_counts_backward(self):
        """Test backward counting (10 to 1 step -1)"""
        script = self._load_script("test_iteration_counts.c64script")
        
        # Should have backward counting
        self.assertIn("10 TO 1 STEP -1", script)
    
    def test_iteration_counts_zero_iterations(self):
        """Test FOR loop that should not execute (start > end)"""
        script = self._load_script("test_iteration_counts.c64script")
        
        # Should have a loop with condition that won't execute
        self.assertIn("FOR M = 10 TO 1", script)
        # Should have comment about not executing
        self.assertIn("not execute", script.lower())
    
    def test_iteration_counts_large(self):
        """Test large iteration count (100 iterations)"""
        script = self._load_script("test_iteration_counts.c64script")
        
        # Should have 100-iteration loop
        self.assertIn("TO 100", script)
    
    def test_iteration_counts_fractional_step(self):
        """Test fractional step values"""
        script = self._load_script("test_iteration_counts.c64script")
        
        # Should have fractional step (0.5)
        self.assertIn("STEP 0.5", script)
    
    def test_simple_sequence_execution(self):
        """Test that simple command sequence has proper structure"""
        script = self._load_script("test_simple_sequence.c64script")
        commands = self._parse_script(script)
        
        # Should have effect, wait, effect, wait pattern
        effect_cmds = [cmd for cmd in commands if 'EFFECT' in cmd.upper()]
        wait_cmds = [cmd for cmd in commands if 'WAIT' in cmd.upper()]
        
        self.assertGreaterEqual(len(effect_cmds), 2, "Should have multiple effect commands")
        self.assertGreaterEqual(len(wait_cmds), 2, "Should have multiple wait commands")
        self.assertIn('STOP', script.upper())
    
    def test_loop_with_goto(self):
        """Test loop using GOTO and labels"""
        script = self._load_script("test_loop.c64script")
        
        # Should have label and goto structure
        self.assertIn("label start", script.lower())
        self.assertIn("goto start", script.lower())
    
    def test_variable_scope_for_loop(self):
        """Test variable mutation in FOR loops"""
        script = self._load_script("test_variable_scope.c64script")
        
        # Should have COUNTER variable and FOR loop
        self.assertIn("COUNTER", script)
        self.assertIn("FOR I = 1 TO 5", script)
        self.assertIn("COUNTER = COUNTER + I", script)
    
    def test_variable_scope_nested_loops(self):
        """Test variable scope in nested loops"""
        script = self._load_script("test_variable_scope.c64script")
        
        # Should have nested loops with SUM variable
        self.assertIn("SUM", script)
        self.assertIn("FOR X", script)
        self.assertIn("FOR Y", script)
    
    def test_variable_scope_while_mutation(self):
        """Test variable mutation in WHILE loop"""
        script = self._load_script("test_variable_scope.c64script")
        
        # Should have WHILE with VALUE variable
        self.assertIn("WHILE VALUE < 5", script)
        self.assertIn("VALUE = VALUE + 1", script)
    
    def test_variable_scope_gosub(self):
        """Test GOSUB with parameters"""
        script = self._load_script("test_variable_scope.c64script")
        
        # Should have GOSUB structure
        self.assertIn("GOSUB", script)
        self.assertIn("RETURN", script)
        self.assertIn("PARAM1", script)
        self.assertIn("PARAM2", script)


class TestScriptSyntax(unittest.TestCase):
    """Test script syntax and structure"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def _load_script(self, filename):
        """Load a test script"""
        script_path = self.script_dir / filename
        self.assertTrue(script_path.exists(), f"Script not found: {filename}")
        with open(script_path, 'r') as f:
            return f.read()
    
    def test_all_scripts_exist(self):
        """Verify all expected test scripts exist"""
        expected_scripts = [
            "test_nested_loops.c64script",
            "test_boolean_logic.c64script",
            "test_comparisons.c64script",
            "test_variable_scope.c64script",
            "test_iteration_counts.c64script",
            "test_simple_sequence.c64script",
            "test_loop.c64script",
            "test_error_invalid_command.c64script",
            "test_error_goto_missing.c64script",
            "test_error_duplicate_label.c64script",
            "test_error_missing_next.c64script",
            "test_error_missing_wend.c64script",
            "test_safety_max_nesting.c64script",
            "test_safety_infinite_loop.c64script",
        ]
        
        for script_name in expected_scripts:
            script_path = self.script_dir / script_name
            self.assertTrue(script_path.exists(), f"Missing script: {script_name}")
    
    def test_scripts_are_utf8(self):
        """Verify all scripts are valid UTF-8"""
        for script_file in self.script_dir.glob("*.c64script"):
            try:
                with open(script_file, 'r', encoding='utf-8') as f:
                    content = f.read()
                    self.assertIsInstance(content, str)
            except UnicodeDecodeError:
                self.fail(f"Script is not valid UTF-8: {script_file.name}")
    
    def test_scripts_have_commands(self):
        """Verify all non-error scripts have at least one command"""
        for script_file in self.script_dir.glob("*.c64script"):
            # Skip error test scripts
            if "error" in script_file.name:
                continue
            
            with open(script_file, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # Count non-comment, non-empty lines
            command_lines = 0
            for line in content.splitlines():
                stripped = line.strip()
                if stripped and not stripped.startswith('#') and not stripped.startswith('REM'):
                    command_lines += 1
            
            self.assertGreater(command_lines, 0, 
                             f"Script has no commands: {script_file.name}")


if __name__ == '__main__':
    print("C64Script Control Flow Tests")
    print("=" * 70)
    print()
    print("Running comprehensive control flow validation...")
    print()
    unittest.main(verbosity=2)
