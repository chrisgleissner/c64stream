import os
import time
import subprocess
import logging

logger = logging.getLogger(__name__)

class XvfbController:
    def __init__(self, display: str = ':99'):
        self.display = display
        self.process = None
        self.managed = False

    def start(self):
        """Start Xvfb virtual framebuffer for headless testing."""
        logger.info(f"Starting Xvfb on display {self.display}")

        try:
            # Check if Xvfb is already running on this display
            try:
                result = subprocess.run(['pgrep', '-f', f'Xvfb.*{self.display}'],
                                      capture_output=True, check=False)
                if result.returncode == 0 and result.stdout.strip():
                    logger.info(f"✅ Xvfb already running on {self.display} (external)")
                    self._configure_env()
                    self.managed = False
                    return True
            except Exception:
                pass  # Ignore errors

            # Clean up any stale lock files
            display_num = self.display.lstrip(':')
            lock_file = f"/tmp/.X{display_num}-lock"
            try:
                if os.path.exists(lock_file):
                    os.remove(lock_file)
                    logger.info(f"Removed stale lock file: {lock_file}")
            except OSError:
                pass  # Ignore permission errors

            # Kill any existing Xvfb processes on this display
            try:
                subprocess.run(['pkill', '-f', f'Xvfb.*{self.display}'],
                             capture_output=True, check=False)
                time.sleep(1)
            except Exception:
                pass  # Ignore errors

            # Start Xvfb with stderr redirection to suppress xkbcomp warnings
            self.process = subprocess.Popen(
                ['Xvfb', self.display, '-screen', '0', '1280x720x24'],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL
            )
            self.managed = True

            self._configure_env()

            # Give Xvfb time to start
            time.sleep(2)

            if self.process.poll() is not None:
                raise RuntimeError(f"Xvfb process exited unexpectedly")

            logger.info("✅ Xvfb started successfully")
            return True

        except Exception as e:
            logger.error(f"❌ Failed to start Xvfb: {e}")
            return False

    def _configure_env(self):
        # Set DISPLAY environment variable
        os.environ['DISPLAY'] = self.display
        # Apply conservative Qt/GL settings when using Xvfb.
        os.environ.setdefault('QT_QPA_PLATFORM', 'xcb')
        os.environ.setdefault('QT_X11_NO_MITSHM', '1')
        os.environ.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
        logger.info("🧪 Applied headless Qt/GL environment variables")

    def stop(self):
        """Stop Xvfb and clean up lock files."""
        if not self.managed:
            return

        logger.info("Stopping Xvfb")

        if self.process:
            self.process.terminate()

            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()

            # Clean up lock files
            try:
                display_num = self.display.lstrip(':')
                lock_file = f"/tmp/.X{display_num}-lock"
                if os.path.exists(lock_file):
                    os.remove(lock_file)
                    logger.info(f"Cleaned up lock file: {lock_file}")
            except OSError:
                pass

            self.process = None
            self.managed = False
