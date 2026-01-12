from __future__ import annotations
import logging
from pathlib import Path
from typing import Dict, Any, Tuple

logger = logging.getLogger(__name__)

try:
    from assertions.frame_progression import FrameProgressionAssertion
    FRAME_PROGRESSION_AVAILABLE = True
except ImportError:
    logger.warning("FrameProgressionAssertion not found - frame progression validation disabled")
    FRAME_PROGRESSION_AVAILABLE = False

class FrameLogicValidator:
    """Validates frame sequence logic using the full assertion framework."""

    @staticmethod
    def validate(recording_path: Path, settling_seconds: float = 0.0) -> Tuple[bool, Dict[str, Any]]:
        """Verify frame sequence box logic using FrameProgressionAssertion.
        Returns: (passed, full_results_dict)
        """
        if not FRAME_PROGRESSION_AVAILABLE:
            logger.warning("Frame progression validation not available")
            return True, {}

        if not recording_path.exists():
            return False, {'error': 'Recording not found'}

        try:
            logger.info(f"🔍 Analyzing frame sequence for: {recording_path.name}")

            # Use the real FrameProgressionAssertion from assertions framework
            assertion = FrameProgressionAssertion()
            result = assertion.verify(
                recording_path,
                properties={"settling_seconds": settling_seconds},
                preset=None,
                verbose=False
            )

            # Convert assertion result to validation format
            status_map = {
                'pass': 'pass',
                'warning': 'warning',
                'skip': 'skipped',
                'fail': 'fail',
            }

            frame_sequence_box = {
                'status': status_map.get(result.status.value, result.status.value),
                'message': result.message,
                'details': result.details,
                'metrics': result.metrics,
            }

            # Accept both 'pass' and 'warning' as successful (warnings are OK)
            passed = result.status.value in ('pass', 'warning')

            # Extract frame count for logging
            valid_frames = result.metrics.get('valid_frames', 0)

            status_icon = "✅" if passed else "❌"
            logger.info(f"{status_icon} Frame Sequence: {passed} ({valid_frames} frames analyzed)")

            return passed, {'frame_sequence_box': frame_sequence_box}

        except Exception as e:
            logger.error(f"❌ Frame sequence validation error: {e}")
            import traceback
            logger.error(traceback.format_exc())
