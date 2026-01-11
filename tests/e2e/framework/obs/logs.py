from __future__ import annotations
import shutil
import json
import re
import time
import logging
from pathlib import Path
from typing import Optional, Dict, Any

from ..environment import Environment

logger = logging.getLogger(__name__)

class OBSLogManager:
    """Manages collection, analysis and monitoring of OBS logs."""

    def __init__(self, env: Environment):
        self.env = env
        self.obs_config_dir = Path.home() / '.config' / 'obs-studio'
        self.logs_dir = self.obs_config_dir / 'logs'

    def collect_latest_log(self, start_time: Optional[float] = None) -> Optional[Path]:
        """Copy the most relevant OBS log to output_dir."""
        try:
            if not self.logs_dir.exists():
                return None

            log_files = [p for p in self.logs_dir.glob('*.txt') if p.is_file()]
            if not log_files:
                return None

            # Prefer logs written after we started OBS (with a little slack).
            if start_time is not None:
                cutoff = start_time - 5.0
                candidates = [p for p in log_files if p.stat().st_mtime >= cutoff]
                if candidates:
                    log_files = candidates

            log_files.sort(key=lambda p: p.stat().st_mtime, reverse=True)
            latest = log_files[0]
            dest = self.env.output_dir / 'obs_log.txt'
            shutil.copy2(latest, dest)
            return dest
        except Exception:
            return None

    def summarize_log(self, log_path: Path) -> Optional[Dict[str, Any]]:
        """Extract basic signals from an OBS log."""
        try:
            text = log_path.read_text(errors='replace')
            patterns = {
                'render_lagged_frames': re.compile(r"Number of lagged frames due to rendering lag:\s*(\d+)", re.I),
                'encode_lagged_frames': re.compile(r"Number of lagged frames due to encoding lag:\s*(\d+)", re.I),
                'dropped_frames': re.compile(r"Dropped frames:\s*(\d+)", re.I),
                'skipped_frames': re.compile(r"Skipped frames:\s*(\d+)", re.I),
            }

            summary: Dict[str, Any] = {
                'log_file': str(log_path),
            }

            for key, pat in patterns.items():
                m = pat.search(text)
                if m:
                    try:
                        summary[key] = int(m.group(1))
                    except Exception:
                        summary[key] = None

            # Capture common warnings/errors
            warn_lines = []
            for line in text.splitlines():
                l = line.lower()
                if 'warning:' in l or 'error:' in l or 'failed' in l:
                    warn_lines.append(line.strip())
            if warn_lines:
                summary['notable_lines'] = warn_lines[:200]

            out = self.env.output_dir / 'obs_log_summary.json'
            out.write_text(json.dumps(summary, indent=2))
            return summary
        except Exception:
            return None

    def analyze_logs(self):
        """Analyze OBS logs for debugging purposes."""
        if not self.logs_dir.exists():
            logger.info("  - OBS logs directory not found")
            return

        log_files = list(self.logs_dir.glob('*.txt'))
        log_files.sort(key=lambda f: f.stat().st_mtime, reverse=True)

        if not log_files:
            logger.info("  - No OBS log files found")
            return

        latest_log = log_files[0]
        logger.info(f"  - Checking latest log: {latest_log.name}")

        try:
            with open(latest_log, 'r') as f:
                content = f.read()

            # Look for plugin-related messages
            plugin_lines = [line for line in content.split('\n') if 'c64' in line.lower() or 'C64' in line]
            if plugin_lines:
                logger.info(f"  - Found {len(plugin_lines)} plugin-related log entries:")
                for line in plugin_lines[-10:]:
                    logger.info(f"    {line}")

            # Look for async/streaming messages
            async_lines = [line for line in content.split('\n') if 'async' in line.lower() or 'streaming' in line.lower() or 'retry' in line.lower()]
            if async_lines:
                logger.info(f"  - Found {len(async_lines)} async/streaming log entries:")
                for line in async_lines[-5:]:
                    logger.info(f"    {line}")

            # Look for errors
            error_lines = [line for line in content.split('\n') if 'error' in line.lower() or 'failed' in line.lower()]
            if error_lines:
                logger.info(f"  - Found {len(error_lines)} error/warning messages:")
                for line in error_lines[-5:]:
                    logger.info(f"    {line}")

            # Check properties file
            plugin_props_file = self.obs_config_dir / 'plugins' / 'c64stream' / 'data' / 'properties.ini'
            if plugin_props_file.exists():
                logger.info(f"  - Plugin properties file exists: {plugin_props_file}")
                try:
                    with open(plugin_props_file, 'r') as f:
                        props_content = f.read()
                    logger.info(f"  - Properties file content (first 300 chars):")
                    logger.info(f"    {props_content[:300]}...")
                except Exception as e:
                    logger.info(f"  - Could not read properties file: {e}")

        except Exception as e:
            logger.info(f"  - Failed to analyze log: {e}")

    def wait_for_initialization(self, timeout: float) -> bool:
        """Wait for C64 plugin to initialize by monitoring OBS logs."""
        logger.info(f"⏳ Monitoring OBS logs for C64 plugin initialization (timeout: {timeout}s)...")

        start_time = time.time()
        init_patterns = [
            "[C64]", "c64_source", "C64 Stream", "UDP socket", "TCP socket"
        ]

        last_size = 0
        latest_log = None

        while time.time() - start_time < timeout:
            # Find latest log
            if not self.logs_dir.exists():
                time.sleep(0.5)
                continue

            log_files = list(self.logs_dir.glob('*.txt'))
            if not log_files:
                time.sleep(0.5)
                continue

            # Use most recently modified file
            log_files.sort(key=lambda f: f.stat().st_mtime, reverse=True)
            current_latest = log_files[0]

            # If log file changed (created new one), reset offset
            if latest_log != current_latest:
                latest_log = current_latest
                last_size = 0

            try:
                if latest_log.stat().st_size > last_size:
                    with open(latest_log, 'r', errors='ignore') as f:
                        f.seek(last_size)
                        new_content = f.read()
                        last_size = f.tell()

                        for line in new_content.splitlines():
                            if any(pat in line for pat in init_patterns):
                                logger.info(f"✅ Plugin initialized: {line.strip()}")
                                return True
                            # Fail fast on some errors?
            except Exception:
                pass

            time.sleep(0.2)

        logger.warning(f"❌ Plugin initialization timeout after {timeout}s")
        return False
