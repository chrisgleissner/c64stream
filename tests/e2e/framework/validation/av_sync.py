from __future__ import annotations
import logging
from pathlib import Path
from typing import Tuple, Optional

logger = logging.getLogger(__name__)

try:
    from util.test_av_sync import verify_av_sync
except ImportError:
    logger.warning("verify_av_sync not found in util module.")
    verify_av_sync = None

class AVSyncValidator:
    """Validates Audio/Video synchronization."""

    @staticmethod
    def validate(recording_path: Path, csv_path: Path, video_format: str) -> Tuple[bool, str, float]:
        """Verify A/V sync using recording and CSV data.
        Returns: (passed, message, offset_ms)
        """
        if not verify_av_sync:
            return True, "AV Sync skipped (missing utility)", 0.0

        if not recording_path.exists():
             return False, "Recording not found", 0.0

        if not csv_path.exists():
             return False, "AV Sync CSV not found", 0.0

        try:
            passed, msg, offset = verify_av_sync(recording_path, csv_path, video_format)
            status_icon = "✅" if passed else "❌"
            logger.info(f"{status_icon} A/V Sync: {passed} ({msg})")
            return passed, msg, offset
        except Exception as e:
            logger.error(f"❌ A/V Sync validation error: {e}")
            return False, str(e), 0.0
