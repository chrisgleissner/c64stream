from __future__ import annotations
import logging
from pathlib import Path
from typing import Optional

logger = logging.getLogger(__name__)

try:
    from util.resource_monitor import ResourceMonitor
    RESOURCE_MONITOR_AVAILABLE = True
except ImportError:
    logger.warning("ResourceMonitor not found in util module.")
    ResourceMonitor = None
    RESOURCE_MONITOR_AVAILABLE = False

class ResourceManager:
    """Manages system resource monitoring (CPU, Memory)."""

    def __init__(self, env, pid: int, interval_ms: int = 200, output_csv: Optional[Path] = None):
        self.env = env
        self.pid = pid
        self.interval_sec = interval_ms / 1000.0
        self.output_csv = output_csv or (env.output_dir / 'resource_usage.csv')
        self._monitor: Optional[ResourceMonitor] = None
        self.enabled = RESOURCE_MONITOR_AVAILABLE

    def start(self):
        """Start monitoring."""
        if not self.enabled:
            return

        try:
            self._monitor = ResourceMonitor(
                interval_ms=int(self.interval_sec * 1000),
                tracked_pids={'obs': self.pid}
            )
            self._monitor.start()
            logger.info(f"📊 Resource monitoring started (pid={self.pid}, interval={self.interval_sec}s)")
        except Exception as e:
            logger.warning(f"Failed to start resource monitor: {e}")
            self._monitor = None

    def stop(self):
        """Stop monitoring."""
        if self._monitor:
            try:
                self._monitor.stop()
                if self.output_csv:
                     self.output_csv.parent.mkdir(parents=True, exist_ok=True)
                     self._monitor.save_csv(self.output_csv)

                     # Also save JSON for report generator
                     json_path = self.output_csv.with_suffix('.json')
                     if self.output_csv.name == 'resource_usage.csv':
                         # If default name, also try saving as 'resource.json' to match report expectation
                         alt_json_path = self.output_csv.parent / 'resource.json'
                         self._monitor.save_json(alt_json_path)
                     else:
                         self._monitor.save_json(json_path)

                logger.info("📊 Resource monitoring stopped")
            except Exception as e:
                logger.warning(f"Error stopping resource monitor: {e}")
            finally:
                self._monitor = None
