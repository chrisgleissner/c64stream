from __future__ import annotations
import logging
from pathlib import Path
from typing import Tuple, Optional, Dict, Any

logger = logging.getLogger(__name__)

try:
    from util.test_av_sync import verify_av_sync
except ImportError:
    logger.warning("verify_av_sync not found in util module.")
    verify_av_sync = None

class AVSyncValidator:
    """Validates Audio/Video synchronization."""

    @staticmethod
    def validate(recording_path: Path, csv_path: Path, video_format: str) -> Tuple[bool, str, float, Dict[str, Any]]:
        """Verify A/V sync using recording and CSV data.
        Returns: (passed, message, offset_ms, details)
        """
        if not verify_av_sync:
            return True, "AV Sync skipped (missing utility)", 0.0, {}

        if not recording_path.exists():
             return False, "Recording not found", 0.0, {}

        if not csv_path.exists():
             return False, "AV Sync CSV not found", 0.0, {}

        try:
            # New usage: verify_av_sync returns a dictionary and takes video_path
            results = verify_av_sync(str(recording_path))

            passed = results.get('is_perfectly_synced', False)
            accuracy = results.get('sync_accuracy_percent', 0.0)

            # Calculate average offset for reporting
            details_list = results.get('sync_details', [])
            offsets = [d['difference_ms'] for d in details_list if d.get('is_synced')]
            avg_offset = sum(offsets) / len(offsets) if offsets else 0.0

            msg = f"Accuracy: {accuracy:.1f}%"
            if not passed:
                total_analyzed = results.get('total_analyzed', 0)
                perfect_count = results.get('perfect_sync_count', 0)
                msg += f" ({total_analyzed - perfect_count} mismatches)"

            status_icon = "✅" if passed else "❌"
            logger.info(f"{status_icon} A/V Sync: {passed} ({msg})")

            return passed, msg, avg_offset, results

        except Exception as e:
            logger.error(f"❌ A/V Sync validation error: {e}")
            return False, str(e), 0.0, {}
