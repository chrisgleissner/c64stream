#!/usr/bin/env python3
"""
Resource monitoring module for E2E performance testing.

Captures CPU, GPU (if available), and RAM usage at configurable intervals.
Exports results to CSV and JSON for analysis.
"""

import json
import os
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from statistics import median
from typing import Optional

# Optional: try to import psutil for more accurate measurements
try:
    import psutil

    HAVE_PSUTIL = True
except ImportError:
    HAVE_PSUTIL = False


@dataclass
class ResourceSample:
    """Single resource usage sample."""

    timestamp_ms: float
    cpu_percent: float  # System-wide CPU percentage
    ram_percent: float
    ram_mb: float
    gpu_percent: Optional[float] = None
    gpu_mem_percent: Optional[float] = None
    gpu_mem_mb: Optional[float] = None


@dataclass
class ResourceStats:
    """Statistics computed from samples."""

    min_val: float
    median_val: float
    max_val: float
    sample_count: int


@dataclass
class ResourceSummary:
    """Summary of all resource usage statistics."""

    cpu: ResourceStats  # System-wide CPU (normalized to effective CPUs)
    ram_percent: ResourceStats
    ram_mb: ResourceStats
    gpu: Optional[ResourceStats] = None
    gpu_mem_percent: Optional[ResourceStats] = None
    gpu_mem_mb: Optional[ResourceStats] = None
    duration_ms: float = 0
    sample_interval_ms: int = 500
    gpu_available: bool = False
    gpu_type: str = "none"  # "nvidia", "intel", "amd", or "none"
    effective_cpu_count: float = 1.0  # CPUs available (respects cgroup limits)
    physical_cpu_count: int = 1  # Physical/logical CPU count


class ResourceMonitor:
    """Monitors system resource usage during E2E tests."""

    def __init__(self, interval_ms: int = 500, verbose: bool = False):
        """
        Initialize the resource monitor.

        Args:
            interval_ms: Sampling interval in milliseconds (default 500ms)
            verbose: Enable verbose logging
        """
        self.interval_ms = interval_ms
        self.verbose = verbose
        self.samples: list[ResourceSample] = []
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._start_time_ms: float = 0
        self._gpu_type, self._gpu_info = self._detect_gpu()
        self._gpu_available = self._gpu_type != "none"

        # Detect effective CPU count (respects cgroup limits in containers/CI)
        self._effective_cpu_count = self._get_effective_cpu_count()
        self._physical_cpu_count = os.cpu_count() or 1

        if self.verbose:
            print(f"[ResourceMonitor] Initialized with {interval_ms}ms interval")
            print(f"[ResourceMonitor] psutil available: {HAVE_PSUTIL}")
            print(f"[ResourceMonitor] CPUs: {self._effective_cpu_count} effective / {self._physical_cpu_count} physical")
            if self._gpu_available:
                print(f"[ResourceMonitor] GPU monitoring: {self._gpu_type} - {self._gpu_info.get('name', 'unknown')}")
            else:
                print("[ResourceMonitor] GPU monitoring: not available")

    def _get_effective_cpu_count(self) -> float:
        """
        Get the effective number of CPUs available to this process.

        In containers/CI environments, the process may be limited by cgroup
        CPU quotas. This method detects those limits to provide accurate
        CPU utilization relative to available resources.

        Returns:
            Effective CPU count (can be fractional, e.g., 2.5 cores)
        """
        # Try cgroup v2 first (modern Linux, GitHub Actions uses this)
        try:
            cpu_max_path = Path("/sys/fs/cgroup/cpu.max")
            if cpu_max_path.exists():
                content = cpu_max_path.read_text().strip()
                parts = content.split()
                if len(parts) >= 2 and parts[0] != "max":
                    quota = int(parts[0])
                    period = int(parts[1])
                    if period > 0:
                        effective = quota / period
                        if self.verbose:
                            print(f"[ResourceMonitor] cgroup v2 CPU limit: {effective:.2f} cores")
                        return effective
        except Exception:
            pass

        # Try cgroup v1 (older Linux)
        try:
            quota_path = Path("/sys/fs/cgroup/cpu/cpu.cfs_quota_us")
            period_path = Path("/sys/fs/cgroup/cpu/cpu.cfs_period_us")
            if quota_path.exists() and period_path.exists():
                quota = int(quota_path.read_text().strip())
                period = int(period_path.read_text().strip())
                if quota > 0 and period > 0:
                    effective = quota / period
                    if self.verbose:
                        print(f"[ResourceMonitor] cgroup v1 CPU limit: {effective:.2f} cores")
                    return effective
        except Exception:
            pass

        # Try reading from /proc/self/cgroup for container detection
        try:
            cgroup_path = Path("/proc/self/cgroup")
            if cgroup_path.exists():
                content = cgroup_path.read_text()
                # Parse cgroup path and check for CPU controller limits
                for line in content.split("\n"):
                    if "cpu" in line.lower():
                        parts = line.split(":")
                        if len(parts) >= 3:
                            cgroup_cpu_path = Path("/sys/fs/cgroup" + parts[2] + "/cpu.max")
                            if cgroup_cpu_path.exists():
                                max_content = cgroup_cpu_path.read_text().strip().split()
                                if len(max_content) >= 2 and max_content[0] != "max":
                                    quota = int(max_content[0])
                                    period = int(max_content[1])
                                    if period > 0:
                                        return quota / period
        except Exception:
            pass

        # Fallback to psutil or os.cpu_count
        if HAVE_PSUTIL:
            return psutil.cpu_count() or 1
        return os.cpu_count() or 1

    def _detect_gpu(self) -> tuple[str, dict]:
        """
        Detect the primary GPU and its type.

        Returns:
            Tuple of (gpu_type, gpu_info_dict)
            gpu_type is one of: "nvidia", "intel", "amd", "none"
        """
        # First, try to find what GPU is actually being used for rendering
        # Check DRM devices to find the primary/render GPU
        primary_gpu = self._find_primary_gpu()

        if primary_gpu:
            if self.verbose:
                print(f"[ResourceMonitor] Primary GPU detected: {primary_gpu}")

        # Check NVIDIA first (discrete GPU usually takes priority for rendering)
        nvidia_info = self._check_nvidia_gpu()
        if nvidia_info:
            return "nvidia", nvidia_info

        # Check Intel GPU (common for integrated graphics)
        intel_info = self._check_intel_gpu()
        if intel_info:
            return "intel", intel_info

        # Check AMD GPU
        amd_info = self._check_amd_gpu()
        if amd_info:
            return "amd", amd_info

        return "none", {}

    def _find_primary_gpu(self) -> Optional[str]:
        """Find the primary GPU being used for rendering via DRM."""
        try:
            # Check /sys/class/drm for GPU devices
            drm_path = Path("/sys/class/drm")
            if not drm_path.exists():
                return None

            for card in sorted(drm_path.iterdir()):
                if card.name.startswith("card") and card.name[4:].isdigit():
                    device_path = card / "device"
                    if device_path.exists():
                        # Try to read vendor info
                        vendor_path = device_path / "vendor"
                        if vendor_path.exists():
                            vendor = vendor_path.read_text().strip()
                            # 0x8086 = Intel, 0x10de = NVIDIA, 0x1002 = AMD
                            if vendor == "0x8086":
                                return f"Intel ({card.name})"
                            elif vendor == "0x10de":
                                return f"NVIDIA ({card.name})"
                            elif vendor == "0x1002":
                                return f"AMD ({card.name})"
        except Exception:
            pass
        return None

    def _check_nvidia_gpu(self) -> Optional[dict]:
        """Check for NVIDIA GPU via nvidia-smi."""
        try:
            result = subprocess.run(
                ["nvidia-smi", "--query-gpu=name,utilization.gpu", "--format=csv,noheader,nounits"],
                capture_output=True,
                text=True,
                timeout=5,
            )
            if result.returncode == 0:
                parts = result.stdout.strip().split(",")
                return {"name": parts[0].strip() if parts else "NVIDIA GPU"}
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass
        return None

    def _check_intel_gpu(self) -> Optional[dict]:
        """Check for Intel GPU via sysfs."""
        try:
            # Look for Intel GPU in DRM subsystem
            drm_path = Path("/sys/class/drm")
            if not drm_path.exists():
                return None

            for card in sorted(drm_path.iterdir()):
                if not card.name.startswith("card") or not card.name[4:].isdigit():
                    continue

                device_path = card / "device"
                vendor_path = device_path / "vendor"

                if vendor_path.exists():
                    vendor = vendor_path.read_text().strip()
                    if vendor == "0x8086":  # Intel vendor ID
                        # Found Intel GPU, get more info
                        info = {"name": "Intel GPU", "card": card.name}

                        # Try to get device name from /proc/driver/i915
                        try:
                            # Check for i915 driver
                            i915_path = Path("/sys/kernel/debug/dri") / card.name[4:] / "i915_frequency_info"
                            if i915_path.exists():
                                info["driver"] = "i915"
                        except Exception:
                            pass

                        # Try to read GPU frequency info
                        gt_freq_path = device_path / "drm" / card.name / "gt_cur_freq_mhz"
                        gt_max_freq_path = device_path / "drm" / card.name / "gt_max_freq_mhz"

                        # Alternative paths
                        if not gt_freq_path.exists():
                            gt_freq_path = device_path / "gt_cur_freq_mhz"
                        if not gt_max_freq_path.exists():
                            gt_max_freq_path = device_path / "gt_max_freq_mhz"

                        if gt_freq_path.exists():
                            info["freq_path"] = str(gt_freq_path)
                        if gt_max_freq_path.exists():
                            info["max_freq_path"] = str(gt_max_freq_path)

                        # Get GPU name from device ID
                        device_id_path = device_path / "device"
                        if device_id_path.exists():
                            device_id = device_id_path.read_text().strip()
                            info["device_id"] = device_id
                            # Map common Intel GPU device IDs to names
                            intel_gpu_names = {
                                "0x1912": "Intel HD Graphics 530",  # Skylake (6700K)
                                "0x1916": "Intel HD Graphics 520",
                                "0x191b": "Intel HD Graphics 530",
                                "0x5912": "Intel HD Graphics 630",  # Kaby Lake
                                "0x3e92": "Intel UHD Graphics 630",  # Coffee Lake
                                "0x9a49": "Intel Xe Graphics",  # Tiger Lake
                            }
                            info["name"] = intel_gpu_names.get(device_id, f"Intel GPU ({device_id})")

                        return info
        except Exception:
            pass
        return None

    def _check_amd_gpu(self) -> Optional[dict]:
        """Check for AMD GPU via sysfs or radeontop."""
        try:
            # Look for AMD GPU in DRM subsystem
            drm_path = Path("/sys/class/drm")
            if not drm_path.exists():
                return None

            for card in sorted(drm_path.iterdir()):
                if not card.name.startswith("card") or not card.name[4:].isdigit():
                    continue

                device_path = card / "device"
                vendor_path = device_path / "vendor"

                if vendor_path.exists():
                    vendor = vendor_path.read_text().strip()
                    if vendor == "0x1002":  # AMD vendor ID
                        info = {"name": "AMD GPU", "card": card.name}

                        # Check for GPU busy percent (amdgpu driver)
                        gpu_busy_path = device_path / "gpu_busy_percent"
                        if gpu_busy_path.exists():
                            info["busy_path"] = str(gpu_busy_path)

                        return info
        except Exception:
            pass
        return None

    def _get_cpu_percent(self) -> float:
        """
        Get current CPU usage percentage, normalized to effective CPUs.

        In CI environments with cgroup limits, this reflects actual utilization
        of the allocated CPU resources rather than system-wide metrics that
        include other jobs running on the same host.
        """
        if HAVE_PSUTIL:
            # psutil.cpu_percent gives system-wide percentage
            raw_percent = psutil.cpu_percent(interval=None)

            # In cgroup-limited environments, we want to report usage relative
            # to our allocation. If we're allocated 2 cores on a 16-core machine,
            # and system shows 12.5% (2/16), we should report closer to 100%.
            if self._effective_cpu_count < self._physical_cpu_count:
                # Scale the percentage based on our allocation ratio
                # This assumes our job is the primary consumer of allocated cores
                scale_factor = self._physical_cpu_count / self._effective_cpu_count
                scaled = raw_percent * scale_factor
                # Cap at 100% - we can't use more than 100% of our allocation
                return min(100.0, scaled)
            return raw_percent
        else:
            # Fallback: parse /proc/stat
            try:
                with open("/proc/stat", "r") as f:
                    line = f.readline()
                    parts = line.split()
                    if parts[0] == "cpu":
                        user = int(parts[1])
                        nice = int(parts[2])
                        system = int(parts[3])
                        idle = int(parts[4])
                        total = user + nice + system + idle
                        busy = user + nice + system
                        return (busy / total) * 100.0 if total > 0 else 0.0
            except Exception:
                return 0.0
        return 0.0

    def _get_ram_usage(self) -> tuple[float, float]:
        """Get RAM usage percentage and MB used."""
        if HAVE_PSUTIL:
            mem = psutil.virtual_memory()
            return mem.percent, mem.used / (1024 * 1024)
        else:
            # Fallback: parse /proc/meminfo
            try:
                meminfo = {}
                with open("/proc/meminfo", "r") as f:
                    for line in f:
                        parts = line.split()
                        if len(parts) >= 2:
                            key = parts[0].rstrip(":")
                            value = int(parts[1])  # in kB
                            meminfo[key] = value

                total_kb = meminfo.get("MemTotal", 1)
                available_kb = meminfo.get("MemAvailable", meminfo.get("MemFree", 0))
                used_kb = total_kb - available_kb
                percent = (used_kb / total_kb) * 100.0 if total_kb > 0 else 0.0
                used_mb = used_kb / 1024.0
                return percent, used_mb
            except Exception:
                return 0.0, 0.0

    def _get_gpu_usage(self) -> tuple[Optional[float], Optional[float], Optional[float]]:
        """
        Get GPU usage based on detected GPU type.

        Returns:
            Tuple of (gpu_percent, gpu_mem_percent, gpu_mem_mb) or (None, None, None)
        """
        if not self._gpu_available:
            return None, None, None

        if self._gpu_type == "nvidia":
            return self._get_nvidia_gpu_usage()
        elif self._gpu_type == "intel":
            return self._get_intel_gpu_usage()
        elif self._gpu_type == "amd":
            return self._get_amd_gpu_usage()

        return None, None, None

    def _get_nvidia_gpu_usage(self) -> tuple[Optional[float], Optional[float], Optional[float]]:
        """Get NVIDIA GPU usage via nvidia-smi."""
        try:
            result = subprocess.run(
                [
                    "nvidia-smi",
                    "--query-gpu=utilization.gpu,memory.used,memory.total",
                    "--format=csv,noheader,nounits",
                ],
                capture_output=True,
                text=True,
                timeout=2,
            )
            if result.returncode == 0:
                line = result.stdout.strip().split("\n")[0]
                parts = [p.strip() for p in line.split(",")]
                if len(parts) >= 3:
                    gpu_percent = float(parts[0])
                    mem_used_mb = float(parts[1])
                    mem_total_mb = float(parts[2])
                    mem_percent = (mem_used_mb / mem_total_mb) * 100.0 if mem_total_mb > 0 else 0.0
                    return gpu_percent, mem_percent, mem_used_mb
        except (ValueError, subprocess.TimeoutExpired, FileNotFoundError):
            pass
        return None, None, None

    def _get_intel_gpu_usage(self) -> tuple[Optional[float], Optional[float], Optional[float]]:
        """
        Get Intel GPU usage via sysfs.

        Intel GPUs don't have a direct "utilization" metric like NVIDIA.
        We estimate usage based on frequency scaling (cur_freq / max_freq).
        """
        try:
            gpu_percent = None
            mem_percent = None
            mem_mb = None

            # Method 1: Try to read from i915 performance counters via intel_gpu_top output
            # This requires intel-gpu-tools package but gives accurate usage
            try:
                result = subprocess.run(
                    ["intel_gpu_top", "-l", "-s", "100"],
                    capture_output=True,
                    text=True,
                    timeout=0.5,
                )
                if result.returncode == 0:
                    # Parse intel_gpu_top output for render/3D usage
                    for line in result.stdout.split("\n"):
                        if "Render/3D" in line or "render" in line.lower():
                            parts = line.split()
                            for part in parts:
                                if "%" in part:
                                    try:
                                        gpu_percent = float(part.rstrip("%"))
                                        break
                                    except ValueError:
                                        continue
                            if gpu_percent is not None:
                                break
            except (FileNotFoundError, subprocess.TimeoutExpired):
                pass

            # Method 2: Frequency-based estimation (fallback)
            if gpu_percent is None:
                freq_path = self._gpu_info.get("freq_path")
                max_freq_path = self._gpu_info.get("max_freq_path")

                if freq_path and max_freq_path:
                    cur_freq = float(Path(freq_path).read_text().strip())
                    max_freq = float(Path(max_freq_path).read_text().strip())
                    if max_freq > 0:
                        # Frequency ratio as proxy for utilization
                        # This is imperfect but gives some indication of GPU activity
                        gpu_percent = (cur_freq / max_freq) * 100.0

            # Method 3: Try reading from /sys/kernel/debug/dri/0/i915_gem_objects for memory
            # (requires root, so may fail)
            try:
                gem_path = Path("/sys/kernel/debug/dri/0/i915_gem_objects")
                if gem_path.exists():
                    content = gem_path.read_text()
                    # Parse memory usage from gem objects
                    for line in content.split("\n"):
                        if "total" in line.lower() and "bytes" in line.lower():
                            parts = line.split()
                            for i, part in enumerate(parts):
                                if part.isdigit():
                                    mem_mb = float(part) / (1024 * 1024)
                                    break
            except (PermissionError, FileNotFoundError):
                pass

            return gpu_percent, mem_percent, mem_mb

        except Exception:
            pass
        return None, None, None

    def _get_amd_gpu_usage(self) -> tuple[Optional[float], Optional[float], Optional[float]]:
        """Get AMD GPU usage via sysfs (amdgpu driver)."""
        try:
            gpu_percent = None
            mem_percent = None
            mem_mb = None

            # Read GPU busy percent from amdgpu driver
            busy_path = self._gpu_info.get("busy_path")
            if busy_path:
                gpu_percent = float(Path(busy_path).read_text().strip())

            # Try to get VRAM usage
            card_name = self._gpu_info.get("card", "card0")
            card_num = card_name[4:] if card_name.startswith("card") else "0"
            vram_used_path = Path(f"/sys/class/drm/{card_name}/device/mem_info_vram_used")
            vram_total_path = Path(f"/sys/class/drm/{card_name}/device/mem_info_vram_total")

            if vram_used_path.exists() and vram_total_path.exists():
                vram_used = int(vram_used_path.read_text().strip())
                vram_total = int(vram_total_path.read_text().strip())
                mem_mb = vram_used / (1024 * 1024)
                mem_percent = (vram_used / vram_total) * 100.0 if vram_total > 0 else 0.0

            return gpu_percent, mem_percent, mem_mb

        except Exception:
            pass
        return None, None, None

    def _sample_worker(self):
        """Background worker that collects samples at the specified interval."""
        interval_sec = self.interval_ms / 1000.0
        first_sample = True

        while self._running:
            timestamp_ms = (time.time() * 1000) - self._start_time_ms

            # For the first sample, use blocking mode to get accurate CPU reading
            # This adds a small delay but ensures no 0% readings
            if first_sample and HAVE_PSUTIL:
                cpu_percent = psutil.cpu_percent(interval=0.1)  # 100ms blocking measurement
                first_sample = False
            else:
                cpu_percent = self._get_cpu_percent()

            ram_percent, ram_mb = self._get_ram_usage()
            gpu_percent, gpu_mem_percent, gpu_mem_mb = self._get_gpu_usage()

            sample = ResourceSample(
                timestamp_ms=timestamp_ms,
                cpu_percent=cpu_percent,
                ram_percent=ram_percent,
                ram_mb=ram_mb,
                gpu_percent=gpu_percent,
                gpu_mem_percent=gpu_mem_percent,
                gpu_mem_mb=gpu_mem_mb,
            )
            self.samples.append(sample)

            if self.verbose:
                gpu_str = f", GPU: {gpu_percent:.1f}%" if gpu_percent is not None else ""
                print(
                    f"[ResourceMonitor] {timestamp_ms:.0f}ms - CPU: {cpu_percent:.1f}%, RAM: {ram_percent:.1f}%{gpu_str}"
                )

            time.sleep(interval_sec)

    def warmup(self):
        """
        Prime CPU measurement before actual monitoring begins.

        Call this BEFORE the data processing starts to ensure the first
        sample after start() has valid CPU data. This separates warmup
        time from actual measurement time.
        """
        if HAVE_PSUTIL:
            # First call establishes baseline (returns 0)
            psutil.cpu_percent(interval=None)
            # Wait long enough for CPU activity to accumulate
            # This ensures the next call has meaningful data
            time.sleep(0.2)
            # This call now measures activity since first call
            # and resets the baseline for start()
            psutil.cpu_percent(interval=None)
            # Final brief pause to ensure next measurement has fresh baseline
            time.sleep(0.05)

        if self.verbose:
            print("[ResourceMonitor] Warmup complete - CPU measurement primed")

    def start(self):
        """
        Start resource monitoring.

        Note: Call warmup() before start() if you want accurate CPU readings
        from the first sample. The warmup primes the CPU measurement system.
        """
        if self._running:
            return

        self._running = True
        self._start_time_ms = time.time() * 1000
        self.samples = []

        self._thread = threading.Thread(target=self._sample_worker, daemon=True)
        self._thread.start()

        if self.verbose:
            print("[ResourceMonitor] Started")

    def stop(self) -> ResourceSummary:
        """
        Stop monitoring and compute statistics.

        Returns:
            ResourceSummary with computed statistics
        """
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)

        if self.verbose:
            print(f"[ResourceMonitor] Stopped, collected {len(self.samples)} samples")

        return self._compute_summary()

    def filter_samples_by_window(self, start_ms: float, end_ms: float) -> list[ResourceSample]:
        """
        Filter samples to those within a time window.

        This is used to extract only samples that fall within the actual
        processing window (from first UDP packet to last OBS frame).

        Args:
            start_ms: Start of window in milliseconds (relative to monitor start)
            end_ms: End of window in milliseconds (relative to monitor start)

        Returns:
            List of ResourceSample objects within the window
        """
        return [s for s in self.samples if start_ms <= s.timestamp_ms <= end_ms]

    def compute_summary_from_samples(self, samples: list[ResourceSample]) -> ResourceSummary:
        """
        Compute summary statistics from a specific list of samples.

        This allows computing stats from filtered samples rather than all samples.

        Args:
            samples: List of ResourceSample objects to compute stats from

        Returns:
            ResourceSummary computed from the provided samples
        """
        if not samples:
            return ResourceSummary(
                cpu=self._compute_stats([]),
                ram_percent=self._compute_stats([]),
                ram_mb=self._compute_stats([]),
                duration_ms=0,
                sample_interval_ms=self.interval_ms,
                gpu_available=self._gpu_available,
                gpu_type=self._gpu_type,
                effective_cpu_count=self._effective_cpu_count,
                physical_cpu_count=self._physical_cpu_count,
            )

        cpu_values = [s.cpu_percent for s in samples]
        ram_percent_values = [s.ram_percent for s in samples]
        ram_mb_values = [s.ram_mb for s in samples]

        # Duration is from first to last sample in this subset
        duration_ms = samples[-1].timestamp_ms - samples[0].timestamp_ms if len(samples) > 1 else 0

        summary = ResourceSummary(
            cpu=self._compute_stats(cpu_values),
            ram_percent=self._compute_stats(ram_percent_values),
            ram_mb=self._compute_stats(ram_mb_values),
            duration_ms=duration_ms,
            sample_interval_ms=self.interval_ms,
            gpu_available=self._gpu_available,
            gpu_type=self._gpu_type,
            effective_cpu_count=self._effective_cpu_count,
            physical_cpu_count=self._physical_cpu_count,
        )

        # GPU stats if available
        gpu_values = [s.gpu_percent for s in samples if s.gpu_percent is not None]
        if gpu_values:
            summary.gpu = self._compute_stats(gpu_values)

        gpu_mem_percent_values = [s.gpu_mem_percent for s in samples if s.gpu_mem_percent is not None]
        if gpu_mem_percent_values:
            summary.gpu_mem_percent = self._compute_stats(gpu_mem_percent_values)

        gpu_mem_mb_values = [s.gpu_mem_mb for s in samples if s.gpu_mem_mb is not None]
        if gpu_mem_mb_values:
            summary.gpu_mem_mb = self._compute_stats(gpu_mem_mb_values)

        return summary

    def save_csv_from_samples(self, output_path: Path, samples: list[ResourceSample]):
        """
        Save specific samples to CSV file.

        This allows saving filtered samples (e.g., only those within processing window).

        Args:
            output_path: Path to output CSV file
            samples: List of ResourceSample objects to save
        """
        output_path.parent.mkdir(parents=True, exist_ok=True)

        with open(output_path, "w") as f:
            # Header
            header = ["timestamp_ms", "cpu_percent", "ram_percent", "ram_mb"]
            if self._gpu_available:
                header.extend(["gpu_percent", "gpu_mem_percent", "gpu_mem_mb"])
            f.write(",".join(header) + "\n")

            # Data rows
            for sample in samples:
                row = [
                    f"{sample.timestamp_ms:.0f}",
                    f"{sample.cpu_percent:.2f}",
                    f"{sample.ram_percent:.2f}",
                    f"{sample.ram_mb:.2f}",
                ]
                if self._gpu_available:
                    gpu_pct = f"{sample.gpu_percent:.2f}" if sample.gpu_percent is not None else ""
                    gpu_mem_pct = f"{sample.gpu_mem_percent:.2f}" if sample.gpu_mem_percent is not None else ""
                    gpu_mem_mb = f"{sample.gpu_mem_mb:.2f}" if sample.gpu_mem_mb is not None else ""
                    row.extend([gpu_pct, gpu_mem_pct, gpu_mem_mb])
                f.write(",".join(row) + "\n")

        if self.verbose:
            print(f"[ResourceMonitor] Saved {len(samples)} samples to {output_path}")

    def _compute_stats(self, values: list[float]) -> ResourceStats:
        """Compute min/median/max statistics for a list of values."""
        if not values:
            return ResourceStats(min_val=0, median_val=0, max_val=0, sample_count=0)

        return ResourceStats(
            min_val=min(values),
            median_val=median(values),
            max_val=max(values),
            sample_count=len(values),
        )

    def _compute_summary(self) -> ResourceSummary:
        """Compute summary statistics from collected samples."""
        if not self.samples:
            return ResourceSummary(
                cpu=self._compute_stats([]),
                ram_percent=self._compute_stats([]),
                ram_mb=self._compute_stats([]),
                duration_ms=0,
                sample_interval_ms=self.interval_ms,
                gpu_available=self._gpu_available,
                gpu_type=self._gpu_type,
                effective_cpu_count=self._effective_cpu_count,
                physical_cpu_count=self._physical_cpu_count,
            )

        cpu_values = [s.cpu_percent for s in self.samples]
        ram_percent_values = [s.ram_percent for s in self.samples]
        ram_mb_values = [s.ram_mb for s in self.samples]

        duration_ms = self.samples[-1].timestamp_ms if self.samples else 0

        summary = ResourceSummary(
            cpu=self._compute_stats(cpu_values),
            ram_percent=self._compute_stats(ram_percent_values),
            ram_mb=self._compute_stats(ram_mb_values),
            duration_ms=duration_ms,
            sample_interval_ms=self.interval_ms,
            gpu_available=self._gpu_available,
            gpu_type=self._gpu_type,
            effective_cpu_count=self._effective_cpu_count,
            physical_cpu_count=self._physical_cpu_count,
        )

        # GPU stats if available
        gpu_values = [s.gpu_percent for s in self.samples if s.gpu_percent is not None]
        if gpu_values:
            summary.gpu = self._compute_stats(gpu_values)

        gpu_mem_percent_values = [s.gpu_mem_percent for s in self.samples if s.gpu_mem_percent is not None]
        if gpu_mem_percent_values:
            summary.gpu_mem_percent = self._compute_stats(gpu_mem_percent_values)

        gpu_mem_mb_values = [s.gpu_mem_mb for s in self.samples if s.gpu_mem_mb is not None]
        if gpu_mem_mb_values:
            summary.gpu_mem_mb = self._compute_stats(gpu_mem_mb_values)

        return summary

    def save_csv(self, output_path: Path):
        """
        Save samples to CSV file.

        Args:
            output_path: Path to output CSV file
        """
        output_path.parent.mkdir(parents=True, exist_ok=True)

        with open(output_path, "w") as f:
            # Header
            header = ["timestamp_ms", "cpu_percent", "ram_percent", "ram_mb"]
            if self._gpu_available:
                header.extend(["gpu_percent", "gpu_mem_percent", "gpu_mem_mb"])
            f.write(",".join(header) + "\n")

            # Data rows
            for sample in self.samples:
                row = [
                    f"{sample.timestamp_ms:.0f}",
                    f"{sample.cpu_percent:.2f}",
                    f"{sample.ram_percent:.2f}",
                    f"{sample.ram_mb:.2f}",
                ]
                if self._gpu_available:
                    gpu_pct = f"{sample.gpu_percent:.2f}" if sample.gpu_percent is not None else ""
                    gpu_mem_pct = f"{sample.gpu_mem_percent:.2f}" if sample.gpu_mem_percent is not None else ""
                    gpu_mem_mb = f"{sample.gpu_mem_mb:.2f}" if sample.gpu_mem_mb is not None else ""
                    row.extend([gpu_pct, gpu_mem_pct, gpu_mem_mb])
                f.write(",".join(row) + "\n")

        if self.verbose:
            print(f"[ResourceMonitor] Saved {len(self.samples)} samples to {output_path}")

    def save_json(self, output_path: Path, summary: Optional[ResourceSummary] = None):
        """
        Save summary statistics to JSON file.

        Args:
            output_path: Path to output JSON file
            summary: Pre-computed summary, or None to compute from samples
        """
        if summary is None:
            summary = self._compute_summary()

        output_path.parent.mkdir(parents=True, exist_ok=True)

        def stats_to_dict(stats: ResourceStats) -> dict:
            return {
                "min": round(stats.min_val, 2),
                "median": round(stats.median_val, 2),
                "max": round(stats.max_val, 2),
                "sample_count": stats.sample_count,
            }

        data = {
            "duration_ms": round(summary.duration_ms, 0),
            "sample_interval_ms": summary.sample_interval_ms,
            "sample_count": summary.cpu.sample_count,
            "allocated_cpu_cores": round(summary.effective_cpu_count, 2),
            "total_cpu_cores": summary.physical_cpu_count,
            "cpu_percent": stats_to_dict(summary.cpu),
            "ram_percent": stats_to_dict(summary.ram_percent),
            "ram_mb": stats_to_dict(summary.ram_mb),
            "gpu_available": summary.gpu_available,
            "gpu_type": summary.gpu_type,
        }

        if summary.gpu is not None:
            data["gpu_percent"] = stats_to_dict(summary.gpu)
        if summary.gpu_mem_percent is not None:
            data["gpu_mem_percent"] = stats_to_dict(summary.gpu_mem_percent)
        if summary.gpu_mem_mb is not None:
            data["gpu_mem_mb"] = stats_to_dict(summary.gpu_mem_mb)

        with open(output_path, "w") as f:
            json.dump(data, f, indent=2)

        if self.verbose:
            print(f"[ResourceMonitor] Saved summary to {output_path}")

    def get_markdown_summary(self, summary: Optional[ResourceSummary] = None) -> str:
        """
        Generate markdown summary of resource usage.

        Args:
            summary: Pre-computed summary, or None to compute from samples

        Returns:
            Markdown formatted string
        """
        if summary is None:
            summary = self._compute_summary()

        if summary.cpu.sample_count == 0:
            return "No resource data collected.\n"

        # Add CPU context (helps understand measurements on shared CI runners)
        cpu_context = ""
        if summary.effective_cpu_count < summary.physical_cpu_count:
            cpu_context = f" (cgroup-limited to {summary.effective_cpu_count:.1f} of {summary.physical_cpu_count} cores)"
        else:
            cpu_context = f" ({summary.physical_cpu_count} cores)"

        lines = [
            "## Resource Usage\n",
            f"- **Duration**: {summary.duration_ms/1000:.1f}s",
            f"- **Sample Interval**: {summary.sample_interval_ms}ms",
            f"- **Samples Collected**: {summary.cpu.sample_count}",
            f"- **CPU Environment**: {cpu_context.strip()}",
            "",
            "### System-Wide CPU",
            f"- Min: {summary.cpu.min_val:.1f}%",
            f"- Median: {summary.cpu.median_val:.1f}%",
            f"- Max: {summary.cpu.max_val:.1f}%",
        ]

        lines.extend(
            [
                "",
                "### RAM",
                f"- Min: {summary.ram_percent.min_val:.1f}% ({summary.ram_mb.min_val:.0f} MB)",
                f"- Median: {summary.ram_percent.median_val:.1f}% ({summary.ram_mb.median_val:.0f} MB)",
                f"- Max: {summary.ram_percent.max_val:.1f}% ({summary.ram_mb.max_val:.0f} MB)",
            ]
        )

        if summary.gpu is not None:
            # Format GPU type nicely for display
            gpu_type_display = {
                "nvidia": "NVIDIA",
                "intel": "Intel",
                "amd": "AMD",
            }.get(summary.gpu_type, summary.gpu_type.upper())

            gpu_name = self._gpu_info.get("name", gpu_type_display) if hasattr(self, "_gpu_info") else gpu_type_display

            lines.extend(
                [
                    "",
                    f"### GPU ({gpu_name})",
                    f"- Min: {summary.gpu.min_val:.1f}%",
                    f"- Median: {summary.gpu.median_val:.1f}%",
                    f"- Max: {summary.gpu.max_val:.1f}%",
                ]
            )

            if summary.gpu_mem_mb is not None and summary.gpu_mem_percent is not None:
                lines.extend(
                    [
                        "",
                        "### GPU Memory",
                        f"- Min: {summary.gpu_mem_percent.min_val:.1f}% ({summary.gpu_mem_mb.min_val:.0f} MB)",
                        f"- Median: {summary.gpu_mem_percent.median_val:.1f}% ({summary.gpu_mem_mb.median_val:.0f} MB)",
                        f"- Max: {summary.gpu_mem_percent.max_val:.1f}% ({summary.gpu_mem_mb.max_val:.0f} MB)",
                    ]
                )
        elif not summary.gpu_available:
            lines.extend(["", "### GPU", "- GPU monitoring not available (no supported GPU detected)"])

        return "\n".join(lines) + "\n"


# CLI for standalone testing
if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Test resource monitoring")
    parser.add_argument("--interval", type=int, default=500, help="Sample interval in ms")
    parser.add_argument("--duration", type=int, default=5, help="Duration in seconds")
    parser.add_argument("--output", type=str, default=None, help="Output directory for CSV/JSON")
    parser.add_argument("--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    monitor = ResourceMonitor(interval_ms=args.interval, verbose=args.verbose)

    # Warmup before measurement to get accurate CPU readings from first sample
    print("Warming up CPU measurement...")
    monitor.warmup()

    print(f"Monitoring for {args.duration} seconds...")
    monitor.start()
    time.sleep(args.duration)
    summary = monitor.stop()

    if args.output:
        output_dir = Path(args.output)
        monitor.save_csv(output_dir / "resource_use.csv")
        monitor.save_json(output_dir / "resource_use.json", summary)

    print(monitor.get_markdown_summary(summary))
