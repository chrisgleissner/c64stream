from __future__ import annotations
import logging
import time
import shutil
from pathlib import Path
from typing import Optional, List

from ..environment import Environment

logger = logging.getLogger(__name__)

class RecordingValidator:
    """Finds and validates the recording file produced by OBS."""

    def __init__(self, env: Environment):
        self.env = env

    def check_recording_output(self) -> Optional[Path]:
        """Check if recording file was created successfully."""
        logger.info("Checking for recording output...")

        # Look for video files in multiple directories
        video_extensions = ['.mp4', '.hybrid_mp4']
        search_dirs = [
            self.env.output_dir,  # Our test output directory
            Path.home() / 'Videos',  # Default OBS recording directory
            Path.home(),  # Home directory
            Path('/tmp'),  # Temporary directory
        ]

        # Also accept plugin's own raw recording as valid evidence on CI
        plugin_recordings_base = Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'
        if plugin_recordings_base.exists():
            # Find latest session folder
            session_folders = [f for f in plugin_recordings_base.glob('session_*') if f.is_dir()]
            if session_folders:
                session_folders.sort(key=lambda f: f.stat().st_mtime, reverse=True)
                latest_session = session_folders[0]
                search_dirs.insert(0, latest_session)

        recording_files = []
        cutoff_time = time.time() - 600  # 10 minutes ago

        for search_dir in search_dirs:
            if search_dir.exists():
                for ext in video_extensions:
                    for file_path in search_dir.glob(f'*{ext}'):
                        try:
                            if file_path.stat().st_mtime > cutoff_time:
                                recording_files.append(file_path)
                        except (OSError, IOError):
                            continue

        if recording_files:
            recording_files.sort(key=lambda f: f.stat().st_mtime, reverse=True)

            for recording in recording_files:
                file_size = recording.stat().st_size
                abs_path = recording.resolve()
                logger.info(f"✅ Found recording: {abs_path} ({file_size} bytes)")

                # Basic validation - file should be larger than 10KB
                if file_size > 10240:
                    # Move to our output directory for easier access (avoid duplicates)
                    suffix = '.mp4' if recording.suffix == '.hybrid_mp4' else recording.suffix
                    dest_file = self.env.output_dir / f"c64_recording{suffix}"
                    try:
                        # If finding in output_dir with correct name, just return it
                        if recording.parent == self.env.output_dir and recording.name == f"c64_recording{suffix}":
                            return dest_file

                        # Move (not copy) to avoid duplicates
                        # If in output_dir but wrong name, rename it
                        if recording.parent == self.env.output_dir:
                            recording.rename(dest_file)
                            logger.info(f"📦 Renamed recording to {dest_file}")
                        else:
                            # Move from external location to output_dir
                            shutil.move(str(recording), str(dest_file))
                            logger.info(f"📦 Moved recording to {dest_file}")
                        return dest_file
                    except Exception as e:
                        logger.error(f"❌ Failed to move recording: {e}")
                else:
                    logger.warning(f"⚠️ Recording too small: {abs_path} ({file_size} bytes)")

        logger.warning("❌ No valid recording file found")
        return None
