from __future__ import annotations
import subprocess
import os
import sys
import shutil
import time
import signal
import logging
from pathlib import Path
from typing import Optional, List

from ..environment import Environment
from .logs import OBSLogManager

logger = logging.getLogger(__name__)

class OBSProcessManager:
    """Manages the OBS Studio process lifecycle."""

    def __init__(self, env: Environment, log_manager: OBSLogManager):
        self.env = env
        self.log_manager = log_manager
        self.process: Optional[subprocess.Popen] = None
        self._start_time = None

    def start(self, profile_name: str = "C64StreamTest", collection_name: str = "C64StreamTest") -> bool:
        """Start OBS with specific profile and collection."""
        logger.info("Starting OBS Studio...")

        cmd = ['obs']
        if sys.platform.startswith('linux') and shutil.which('dbus-run-session'):
            logger.info("Wrapping OBS in dbus-run-session for headless stability")
            cmd = ['dbus-run-session', '--', 'obs']

        cmd.extend([
            '--verbose',
            '--startrecording',  # Auto-start recording
            '--profile', profile_name,
            '--disable-missing-files-check', # Prevent "Missing Files" dialog
        ])

        # Add portable mode flag if needed (usually handled by config copying, but let's see)
        # e2e.py didn't use --portable, just standard paths.

        # Try launch flags logic
        collection_flags = ['--collection', '--scene-collection']

        launched = False
        init_ok = False
        last_error = None

        for flag in collection_flags:
            # Ensure clean slate
            self.stop()

            try:
                launch_cmd = cmd + [flag, collection_name]
                if self._launch_process(launch_cmd):
                    launched = True

                    # Quick probe
                    short_timeout = max(2.0, min(self.env.plugin_init_timeout, 6)) if not self.env.is_ci else 8.0
                    logger.info(f"🔍 Probing plugin init with {flag} (timeout: {short_timeout}s)...")

                    if self.log_manager.wait_for_initialization(timeout=short_timeout):
                        init_ok = True
                        break
                    else:
                        logger.warning(f"⚠️ Plugin init not detected quickly with {flag}; trying next...")
            except Exception as e:
                last_error = e
                logger.warning(f"⚠️ OBS launch attempt failed: {e}")
                continue

        if not launched:
            # If we didn't launch but verify failed, show that.
            # If process creation failed, show last_error.
            # If process crashed immediately, verify it.
            if self.process and self.process.poll() is not None:
                # Get stdout/stderr if possible
                header = f"\n=== OBS CRASH LOG ({self.env.output_dir / 'obs_stdout.log'}) ===\n"
                try:
                    with open(self.env.output_dir / 'obs_stdout.log', 'r') as f:
                        log_content = f.read()[-1000:] # Last 1000 chars
                    last_error = f"Process crashed immediately.\n{header}{log_content}\n=================="
                except:
                    last_error = "Process crashed immediately (log unreadable)"

            raise RuntimeError(f"OBS failed to start: {last_error}")

        if not init_ok:
            logger.info("⏳ Running full plugin init wait on last OBS launch...")
            if not self.log_manager.wait_for_initialization(self.env.plugin_init_timeout):
                self.log_manager.analyze_logs()
                raise RuntimeError("C64 plugin failed to initialize within timeout")
            else:
                logger.info("✅ C64 plugin initialization complete")

        self.boost_priority()

        # Give OBS a moment to fully settle
        time.sleep(1.0)
        return True

    def _launch_process(self, cmd: List[str]) -> bool:
        """Internal launch helper."""
        # Set minimal env vars if needed, but usually inherit
        env = os.environ.copy()

        # We should capture stdout/stderr to debug crashes
        try:
            # Use a log file for stdout/stderr
            out_file = self.env.output_dir / 'obs_stdout.log'
            self._stdout_file = open(out_file, 'w')

            logger.info(f"Running: {' '.join(cmd)}")
            self.process = subprocess.Popen(
                cmd,
                stdout=self._stdout_file,
                stderr=subprocess.STDOUT,
                env=env,
                preexec_fn=os.setsid  # Create new process group
            )
            self._start_time = time.time()

            # Brief wait to catch immediate crash
            time.sleep(self.env.obs_startup_delay)

            if self.process.poll() is not None:
                # Crashed
                return False

            # XdoTool mitigation for Safe Mode dialog (Experimental)
            if shutil.which("xdotool") and sys.platform.startswith('linux'):
                 import threading
                 def press_keys():
                    time.sleep(2.0) # Wait for dialog
                    try:
                        logger.info("🤖 xdotool: Sending interaction keys...")
                        # Try to dismiss dialogs
                        subprocess.run(["xdotool", "key", "Escape"], check=False)
                        time.sleep(0.5)
                        subprocess.run(["xdotool", "key", "Return"], check=False)
                    except Exception as e:
                        logger.warning(f"xdotool failed: {e}")
                 threading.Thread(target=press_keys, daemon=True).start()

            return True
        except Exception as e:
            logger.error(f"Failed to Popen OBS: {e}")
            raise

    def stop(self):
        """Stop OBS process."""
        if self.process:
            logger.info("Stopping OBS...")
            try:
                # Graceful termination first
                # Use SIGINT (Ctrl+C) prevents dbus-run-session from killing child immediately?
                # Send to process group to ensure all processes (dbus, obs) receive it.
                os.killpg(os.getpgid(self.process.pid), signal.SIGINT)
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    logger.warning("OBS did not exit gracefully, killing...")
                    os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
                    self.process.wait(timeout=2)
            except Exception as e:
                logger.warning(f"Error stopping OBS: {e}")
            finally:
                if self._stdout_file:
                    self._stdout_file.close()
                self.process = None

    def boost_priority(self):
        """Renice OBS process for better performance."""
        if not self.process:
            return

        try:
            pid = self.process.pid
            os.system(f"sudo renice -n -10 -p {pid} 2>/dev/null || true")
        except Exception:
            pass

    def check_alive(self):
        """Raise error if OBS died."""
        if self.process and self.process.poll() is not None:
            raise RuntimeError("OBS process exited unexpectedly")
