#!/usr/bin/env python3

import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from framework.environment import Environment
from framework.obs.logs import OBSLogManager


class TestOBSLogManager(unittest.TestCase):
    def test_script_status_from_text(self):
        self.assertTrue(OBSLogManager._script_status_from_text("Script completed successfully"))
        self.assertFalse(OBSLogManager._script_status_from_text("Script failed: timed out"))
        self.assertFalse(OBSLogManager._script_status_from_text("Auto-start script failed: missing file"))
        self.assertIsNone(OBSLogManager._script_status_from_text("Started script"))

    def test_wait_for_script_completion_detects_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            test_dir = tmp_path / "tests" / "e2e"
            output_dir = tmp_path / "results"
            logs_dir = tmp_path / "obs-logs"
            test_dir.mkdir(parents=True)
            output_dir.mkdir(parents=True)
            logs_dir.mkdir(parents=True)

            env = Environment(test_dir, str(output_dir))
            manager = OBSLogManager(env)
            manager.logs_dir = logs_dir

            log_path = logs_dir / "2026-03-19 12-00-00.txt"
            log_path.write_text("Started script\n", encoding="utf-8")
            start_time = time.time()

            def append_completion() -> None:
                time.sleep(0.2)
                log_path.write_text("Started script\nScript completed successfully\n", encoding="utf-8")

            import threading

            thread = threading.Thread(target=append_completion)
            thread.start()
            try:
                self.assertTrue(manager.wait_for_script_completion(timeout=2.0, start_time=start_time))
            finally:
                thread.join()


if __name__ == "__main__":
    unittest.main()
