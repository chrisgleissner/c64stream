from __future__ import annotations
import os
import shutil
import logging
from pathlib import Path

logger = logging.getLogger(__name__)

class Environment:
    def __init__(self, test_dir: Path | str, output_dir: str | None = None, csv_max_rows: int | None = None):
        self.test_dir = Path(test_dir).resolve()
        # Assume project root is 2 levels up from tests/e2e
        self.project_root = self.test_dir.parents[1]

        # Setup output directory
        if output_dir:
            out_path = Path(output_dir)
            if not out_path.is_absolute():
                out_path = self.test_dir / out_path
            self.output_dir = out_path
        else:
            self.output_dir = self.test_dir / 'test_output'

        self.packet_dir = self.test_dir / 'test_packets'
        self.csv_max_rows = csv_max_rows

        self.is_ci = self._detect_ci()
        self._configure_timeouts()
        self._setup_os_env()

    def prepare(self):
        """Prepare environment (create directories)."""
        if self.output_dir.exists():
            # Clean up previous results to ensure clean test
            try:
                for file in self.output_dir.glob('*'):
                    if file.is_file():
                        file.unlink()
            except Exception as e:
                logger.warning(f"Failed to clean output dir: {e}")
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def _setup_os_env(self):
        """Configure OS environment variables for OBS stability."""
        # Disable XDG desktop portal to prevent dbus hangs/crashes
        # "warning: ReadOne on org.freedesktop.portal.Settings returned an invalid reply"
        os.environ['QT_NO_XDG_DESKTOP_PORTAL'] = '1'

        # Ensure correct QPA platform (xcb is usually good for xvfb, fail-safe)
        os.environ['QT_QPA_PLATFORM'] = 'xcb'

        # Force software rendering for Xvfb stability
        os.environ['LIBGL_ALWAYS_SOFTWARE'] = '1'

        # Override Desktop Environment to avoid complex Portal interactions (e.g. KDE)
        os.environ['XDG_CURRENT_DESKTOP'] = 'XFCE'
        os.environ['KDE_FULL_SESSION'] = 'false'
        os.environ['GNOME_DESKTOP_SESSION_ID'] = ''
        os.environ['QT_QPA_PLATFORMTHEME'] = 'gtk2'
        os.environ['GALLIUM_DRIVER'] = 'llvmpipe'

        if not self.is_ci:
            # Local tweaks if needed
            pass

    def _detect_ci(self) -> bool:
        """Detect if running in CI environment."""
        ci_indicators = [
            'CI', 'CONTINUOUS_INTEGRATION', 'GITHUB_ACTIONS',
            'GITLAB_CI', 'JENKINS_URL', 'TRAVIS', 'CIRCLECI',
            'BUILDKITE', 'DRONE', 'TEAMCITY_VERSION'
        ]
        return any(os.environ.get(indicator) for indicator in ci_indicators)

    def _configure_timeouts(self):
        """Configure timeouts based on environment."""
        if self.is_ci:
            # CI environment: Extended timeouts
            self.plugin_init_timeout = 45  # Increased from 30s for more robust CI
            self.obs_startup_delay = 4     # Increased from 3s
            self.async_task_delay = 2.0    # Plugin async tasks
            self.websocket_settings_delay = 3  # Increased from 2s
            self.udp_socket_delay = 2.0    # Give OBS/plugin more time to bind UDP
            self.buffer_setup_delay = 1.0  # Increased from 0.5s
            logger.info("🏗️ CI environment detected - using extended timeouts")
        else:
            # Local environment: Short timeouts
            self.plugin_init_timeout = 10  # Increased from 6s for reliability
            self.obs_startup_delay = 1.0   # Increased from 0.5s
            self.async_task_delay = 0.2
            self.websocket_settings_delay = 0.5
            self.udp_socket_delay = 0.5
            logger.info("🚀 Local environment detected - using reasonable timeouts")
            self.buffer_setup_delay = 0.2
            logger.info("🚀 Local environment detected - using short timeouts")

    def copy_csv_truncated(self, src: Path, dest: Path):
        """Copy a CSV file, optionally truncating using csv_max_rows setting."""
        if self.csv_max_rows is None:
            shutil.copy2(src, dest)
            return

        with open(src, 'r') as f:
            lines = f.readlines()

        if len(lines) <= 1:
            shutil.copy2(src, dest)
            return

        header = lines[0]
        data_lines = lines[1:]
        total_data_rows = len(data_lines)

        # csv_max_rows is total lines including header
        max_total_lines = max(1, int(self.csv_max_rows))
        max_data_rows = max(0, max_total_lines - 1)

        if total_data_rows <= max_data_rows:
            shutil.copy2(src, dest)
            return

        output_lines = [header] + data_lines[:max_data_rows]
        truncated_count = total_data_rows - max_data_rows

        with open(dest, 'w') as f:
            f.writelines(output_lines)

        if truncated_count > 0:
            logger.info(f"📉 Truncated {truncated_count} rows from {src.name} "
                        f"(keeping first {max_total_lines} lines)")
