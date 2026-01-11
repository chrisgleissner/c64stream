from __future__ import annotations
import json
import csv
import logging
from pathlib import Path
from typing import Dict, Any, Optional, Tuple, List

from ..environment import Environment
from .network import NetworkTimingValidator
from .av_sync import AVSyncValidator

logger = logging.getLogger(__name__)

class ResultValidator:
    """Consolidated validation of E2E test results."""

    def __init__(self, env: Environment,
                 video_format: str = 'PAL',
                 frames: int = 100,
                 packet_source: str = 'mock',
                 network_simulation: Optional[Dict] = None):
        self.env = env
        self.format = video_format
        self.frames = frames
        self.packet_source = packet_source
        self.network_simulation = network_simulation or {}

    def validate(self,
                 replay_success: bool,
                 recording_path: Optional[Path],
                 counts: Dict[str, int]) -> Tuple[bool, Dict[str, Any]]:
        """Run comprehensive validation."""

        logger.info("\n" + "="*60)
        logger.info("E2E Test Validation Results")
        logger.info("="*60)

        results = {
            'success': False,
            'errors': [],
            'warnings': [],
            'details': {}
        }

        # 0. Check Cross Pollution (Mock runs MUST start with low seq numbers)
        self._check_cross_pollution(results)

        # 1. Check UDP Reception/Counts
        self._check_udp_counts(counts, results)

        # 2. Check Replay Status
        if not replay_success and self.packet_source == 'mock':
             results['errors'].append("Packet replay failed")

        # 3. Check Recording
        if not recording_path or not recording_path.exists():
            results['errors'].append("No recording file produced")
            results['details']['recording'] = "Missing"
        else:
            results['details']['recording'] = f"Found: {recording_path.name}"

        # 4. A/V Sync
        if recording_path:
             self._check_av_sync(recording_path, results)

        # 5. Network Timing
        self._check_network_timing(results)

        # Final Decision
        results['success'] = len(results['errors']) == 0

        # Log Summary
        if results['success']:
             logger.info("✅ OVERALL STATUS: PASS")
        else:
             logger.error("❌ OVERALL STATUS: FAIL")
             for err in results['errors']:
                  logger.error(f"  - {err}")

        if results['warnings']:
             logger.warning("⚠️ Warnings:")
             for warn in results['warnings']:
                  logger.warning(f"  - {warn}")

        return results['success'], results

    def _check_cross_pollution(self, results):
        network_csv = self.env.output_dir / 'network.csv'
        if network_csv.exists() and self.packet_source == 'mock':
            try:
                with open(network_csv, 'r') as f:
                    reader = csv.DictReader(f)
                    first_video = None
                    for row in reader:
                        if row.get('packet_type') == 'video':
                             first_video = int(row['sequence_num'])
                             break

                    if first_video is not None and first_video > 1000:
                         msg = f"Cross-pollution: video sequence {first_video} >> 1000 (Real device running?)"
                         logger.error(f"❌ {msg}")
                         results['errors'].append(msg)
            except Exception:
                pass

    def _check_udp_counts(self, counts, results):
        # counts comes from parsing CSVs earlier
        received = counts.get('network_packets', 0)
        video = counts.get('video_packets', 0)

        # Calculate expected
        if self.format == 'PAL':
            packets_per_frame = 68
        else:
            packets_per_frame = 60

        expected_video = self.frames * packets_per_frame

        if self.packet_source == 'mock':
            # Strict check
            logger.info(f"UDP Packet Check: Received {video} video packets (Expected ~{expected_video})")
            if video < expected_video * 0.95:
                 msg = f"Packet loss detected: {video} < {expected_video} expected"
                 results['errors'].append(msg)
        else:
            # Device mode lax check
            if received == 0:
                 results['errors'].append("No UDP packets received")

    def _check_av_sync(self, recording_path, results):
        # Assuming av-sync.csv is present
        av_csv = self.env.output_dir / 'av-sync.csv'
        if not av_csv.exists():
             logger.warning("No av-sync.csv found available for sync check")
             return

        passed, msg, offset = AVSyncValidator.validate(recording_path, av_csv, self.format)
        results['details']['av_sync'] = msg
        results['details']['av_sync_offset'] = offset

        if not passed:
             # In some modes AV sync failure might be just a warning, but usually error
             results['errors'].append(f"AV Sync Failed: {msg}")

    def _check_network_timing(self, results):
        network_json = self.env.output_dir / 'network.json'
        status, details, errors, warnings = NetworkTimingValidator.validate(
            network_json, self.format, self.frames, self.network_simulation, self.packet_source
        )

        results['details']['network_timing'] = details

        for e in errors:
            results['errors'].append(f"Network Timing: {e}")
        for w in warnings:
            results['warnings'].append(f"Network Timing: {w}")
