#!/usr/bin/env python3
"""
Unit tests for C64 script executor.

Tests the script execution engine's behavior without requiring a full OBS environment.
Uses mock REST client and source data to isolate executor logic.
"""

import unittest
from unittest.mock import Mock, MagicMock, patch, call
import sys
import os

# Add tests directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


class TestScriptExecutor(unittest.TestCase):
    """Test cases for script executor functionality"""

    def setUp(self):
        """Set up test fixtures"""
        self.mock_source = Mock()
        self.mock_rest_client = Mock()
        self.mock_settings = Mock()

        # Configure mock responses
        self.mock_source.get_settings.return_value = self.mock_settings

    def test_executor_lifecycle(self):
        """Test executor creation and destruction"""
        # This is a conceptual test - actual implementation would use ctypes
        # to call C functions from Python for testing

        # Test creation
        executor = self._create_executor()
        self.assertIsNotNone(executor)

        # Test initial state
        self.assertFalse(self._is_running(executor))
        self.assertEqual(self._get_status(executor), "IDLE")

        # Test destruction
        self._destroy_executor(executor)

    def test_command_dispatch_effect(self):
        """Test effect command execution"""
        executor = self._create_executor()
        script = self._parse_script("effect Sharp Pixels\\nstop")

        # Start execution
        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify effect was applied
        self.mock_settings.set_string.assert_called_with("effect_preset", "Sharp Pixels")
        self.mock_source.update.assert_called()

    def test_command_dispatch_effect_param(self):
        """Test effect_param command execution"""
        executor = self._create_executor()
        script = self._parse_script("effect_param bloom_strength 0.5\\nstop")

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify parameter was set
        self.mock_settings.set_double.assert_called_with("bloom_strength", 0.5)

    def test_command_dispatch_palette(self):
        """Test palette command execution"""
        executor = self._create_executor()
        script = self._parse_script("palette Pepto\\nstop")

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify palette was changed
        self.mock_settings.set_string.assert_called_with("palette", "Pepto")

    def test_command_dispatch_play_sid(self):
        """Test play_sid command execution"""
        executor = self._create_executor()
        script = self._parse_script("play_sid c64u:/Music/test.sid songnr=1\\nstop")

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify REST API was called
        self.mock_rest_client.post.assert_called_with(
            "/v1/runners:sidplay",
            params={"path": "/Music/test.sid", "song": 1}
        )

    def test_command_dispatch_run_prg(self):
        """Test run_prg command execution"""
        executor = self._create_executor()
        script = self._parse_script("run_prg c64u:/Programs/demo.prg\\nstop")

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify REST API was called
        self.mock_rest_client.post.assert_called_with(
            "/v1/runners:run_prg",
            params={"path": "/Programs/demo.prg"}
        )

    def test_command_dispatch_mount_disk(self):
        """Test mount_disk command execution"""
        executor = self._create_executor()
        script = self._parse_script("mount_disk c64u:/Games/game.d64\\nstop")

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify REST API was called
        self.mock_rest_client.post.assert_called_with(
            "/v1/drives/A:mount",
            params={"path": "/Games/game.d64"}
        )

    def test_command_dispatch_reset(self):
        """Test reset command execution"""
        executor = self._create_executor()
        script = self._parse_script("reset\\nstop")

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify REST API was called
        self.mock_rest_client.put.assert_called_with("/v1/machine:reset")

    def test_command_dispatch_reboot(self):
        """Test reboot command execution"""
        executor = self._create_executor()
        script = self._parse_script("reboot\\nstop")

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify REST API was called
        self.mock_rest_client.put.assert_called_with("/v1/machine:reboot")

    def test_wait_timing(self):
        """Test wait command timing accuracy"""
        import time

        executor = self._create_executor()
        script = self._parse_script("wait 200ms\\nstop")

        start_time = time.time()
        self._start_executor(executor, script)
        self._wait_for_completion(executor)
        elapsed = time.time() - start_time

        # Should be ~200ms, allow 100ms polling overhead
        self.assertGreaterEqual(elapsed, 0.2)
        self.assertLess(elapsed, 0.4)

    def test_wait_formats(self):
        """Test different wait duration formats"""
        test_cases = [
            ("wait 100ms\\nstop", 0.1),
            ("wait 1s\\nstop", 1.0),
            ("wait 0.5m\\nstop", 30.0),
        ]

        for script_text, expected_duration in test_cases:
            with self.subTest(script=script_text):
                executor = self._create_executor()
                script = self._parse_script(script_text)

                import time
                start_time = time.time()
                self._start_executor(executor, script)
                self._wait_for_completion(executor)
                elapsed = time.time() - start_time

                # Allow 100ms polling overhead
                self.assertGreaterEqual(elapsed, expected_duration)
                self.assertLess(elapsed, expected_duration + 0.2)

    def test_label_resolution(self):
        """Test label definition and goto"""
        executor = self._create_executor()
        script = self._parse_script("""
            goto skip
            effect Bad Preset
            label skip
            effect Good Preset
            stop
        """)

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify only "Good Preset" was applied (skipped "Bad Preset")
        self.mock_settings.set_string.assert_called_once_with("effect_preset", "Good Preset")

    def test_loop_fixed_count(self):
        """Test loop with fixed iteration count"""
        executor = self._create_executor()
        script = self._parse_script("""
            loop 3
            effect Test
            stop
        """)

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify effect was applied 3 times
        self.assertEqual(self.mock_settings.set_string.call_count, 3)

    def test_loop_goto_iteration(self):
        """Test manual loop with goto and iteration counting"""
        executor = self._create_executor()
        script = self._parse_script("""
            # Manual counter-based loop (v1 doesn't have variables, so test structure only)
            label start
            effect Test
            goto start
        """)

        # Start executor
        self._start_executor(executor, script)

        # Let it run for a bit
        import time
        time.sleep(0.5)

        # Stop execution
        self._stop_executor(executor)
        self._wait_for_completion(executor)

        # Verify effect was applied multiple times
        self.assertGreater(self.mock_settings.set_string.call_count, 1)

    def test_error_invalid_command(self):
        """Test error handling for invalid commands"""
        executor = self._create_executor()

        # Parser should reject invalid commands
        script = self._parse_script("invalid_command\\nstop")

        # Script should be None (parse error)
        self.assertIsNone(script)

    def test_error_missing_label(self):
        """Test error handling for missing goto target"""
        executor = self._create_executor()
        script = self._parse_script("goto nonexistent\\nstop")

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Should be in error state
        status = self._get_status(executor)
        self.assertEqual(status, "ERROR")

        error_msg = self._get_error(executor)
        self.assertIn("nonexistent", error_msg.lower())

    def test_error_duplicate_label(self):
        """Test error handling for duplicate labels"""
        executor = self._create_executor()
        script = self._parse_script("""
            label test
            effect A
            label test
            effect B
            stop
        """)

        # Should fail to start due to duplicate label
        result = self._start_executor(executor, script)
        self.assertFalse(result)

    def test_cancellation(self):
        """Test immediate cancellation during execution"""
        executor = self._create_executor()
        script = self._parse_script("wait 10s\\nstop")

        # Start execution
        self._start_executor(executor, script)

        # Verify it's running
        self.assertTrue(self._is_running(executor))

        # Stop immediately
        import time
        time.sleep(0.1)  # Let it start waiting
        self._stop_executor(executor)

        # Wait for stop to complete
        self._wait_for_completion(executor)

        # Verify it stopped quickly (not 10 seconds)
        # Total time should be < 1 second

    def test_record_start_stop(self):
        """Test record_start and record_stop commands"""
        executor = self._create_executor()
        script = self._parse_script("""
            record_start
            wait 100ms
            record_stop
            stop
        """)

        self._start_executor(executor, script)
        self._wait_for_completion(executor)

        # Verify recording commands were issued
        # (Actual verification depends on mock setup for record module)

    def test_executor_state_getters(self):
        """Test executor state getter functions"""
        executor = self._create_executor()
        script = self._parse_script("wait 100ms\\neffect Test\\nstop")

        # Initial state
        self.assertEqual(self._get_current_line(executor), 0)
        self.assertIsNone(self._get_current_command(executor))

        # During execution
        self._start_executor(executor, script)
        import time
        time.sleep(0.05)

        # Should be executing
        self.assertGreater(self._get_current_line(executor), 0)
        self.assertIsNotNone(self._get_current_command(executor))

        # After completion
        self._wait_for_completion(executor)
        status = self._get_status(executor)
        self.assertIn(status, ["COMPLETED", "IDLE"])

    # Helper methods (these would be implemented with ctypes in a real test)

    def _create_executor(self):
        """Create a mock executor"""
        return Mock()

    def _destroy_executor(self, executor):
        """Destroy executor"""
        pass

    def _parse_script(self, content):
        """Parse script from string"""
        return Mock(num_commands=content.count('\\n') + 1)

    def _start_executor(self, executor, script):
        """Start script execution"""
        return True

    def _stop_executor(self, executor):
        """Stop script execution"""
        pass

    def _is_running(self, executor):
        """Check if executor is running"""
        return False

    def _get_status(self, executor):
        """Get executor status"""
        return "IDLE"

    def _get_current_line(self, executor):
        """Get current line number"""
        return 0

    def _get_current_command(self, executor):
        """Get current command name"""
        return None

    def _get_error(self, executor):
        """Get error message"""
        return ""

    def _wait_for_completion(self, executor, timeout=5.0):
        """Wait for executor to complete"""
        import time
        start = time.time()
        while time.time() - start < timeout:
            if not self._is_running(executor):
                break
            time.sleep(0.05)


if __name__ == '__main__':
    print("C64 Script Executor Unit Tests")
    print("=" * 60)
    print()
    print("Note: This test suite is a TEMPLATE for testing the")
    print("script executor. Actual implementation requires ctypes")
    print("bindings to the C library or a test harness in C.")
    print()
    print("Current status: Mock tests (demonstrate test structure)")
    print()

    unittest.main(verbosity=2)
