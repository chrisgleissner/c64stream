#!/usr/bin/env python3
"""
C64 Stream Filter/Preset Performance Benchmark Tool

This script measures CPU and GPU impact of each CRT filter and effect preset
by running controlled E2E tests while monitoring system resources.

Requirements:
- intel_gpu_top (for Intel iGPU monitoring)
- Python 3.8+
- Existing E2E infrastructure

Output:
- JSON file with detailed measurements
- Human-readable summary

Usage:
    ./benchmark_filters.py --output results.json --duration 10
    ./benchmark_filters.py --preset "Green Monitor" --duration 5
    ./benchmark_filters.py --filter afterglow --values "0,40,80,120" --duration 5

Copyright (C) 2025 Christian Gleissner
Licensed under the GNU General Public License v2.0 or later.
"""

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

SCRIPT_DIR = Path(__file__).parent.absolute()
PROJECT_ROOT = SCRIPT_DIR.parent.parent
DATA_DIR = PROJECT_ROOT / "data"

# Effect presets from effect_presets.ini
EFFECT_PRESETS = [
    "Default",
    "Classic CRT",
    "Amber Monitor",
    "Green Monitor",
    "Sharp Pixels",
    "Phosphor Glow",
    "Vintage TV",
    "Arcade Cabinet",
]

# Individual filters and their test values
FILTER_TEST_VALUES = {
    "scan_line_distance": [0.0, 0.5, 1.0, 2.0],
    "scan_line_strength": [0.0, 0.5, 1.0],
    "blur_strength": [0.0, 0.3, 0.6, 1.0],
    "bloom_strength": [0.0, 0.3, 0.6, 1.0],
    "afterglow_duration_ms": [0, 40, 80, 120],
    "tint_mode": [0, 1, 2, 3],  # None, Amber, Green, Monochrome
    "tint_strength": [0.0, 0.5, 1.0],
    "pixel_width": [1.0, 2.0, 4.0],
    "pixel_height": [1.0, 2.0, 4.0],
}

# Baseline filter values (all effects off)
BASELINE_FILTERS = {
    "scan_line_distance": 0.0,
    "scan_line_strength": 0.6,
    "blur_strength": 0.0,
    "bloom_strength": 0.0,
    "afterglow_duration_ms": 0,
    "afterglow_curve": 0,
    "tint_mode": 0,
    "tint_strength": 1.0,
    "pixel_width": 1.0,
    "pixel_height": 1.0,
}


@dataclass
class ResourceSample:
    """Single resource measurement sample."""
    timestamp: float
    cpu_percent: float  # Overall CPU usage
    gpu_percent: float  # GPU 3D/Render engine usage
    gpu_freq_mhz: int   # Current GPU frequency
    gpu_power_w: float  # GPU power consumption (if available)


@dataclass
class BenchmarkResult:
    """Result of a single benchmark run."""
    name: str
    filter_name: Optional[str]
    filter_value: Optional[str]
    preset_name: Optional[str]
    duration_seconds: float
    samples: List[ResourceSample] = field(default_factory=list)
    
    # Computed statistics
    cpu_mean: float = 0.0
    cpu_max: float = 0.0
    cpu_min: float = 0.0
    gpu_mean: float = 0.0
    gpu_max: float = 0.0
    gpu_min: float = 0.0
    gpu_freq_mean: float = 0.0
    gpu_freq_max: float = 0.0
    
    # Timing
    wall_time_seconds: float = 0.0
    user_cpu_seconds: float = 0.0
    system_cpu_seconds: float = 0.0
    
    def compute_statistics(self):
        """Compute statistics from samples."""
        if not self.samples:
            return
        
        cpu_values = [s.cpu_percent for s in self.samples]
        gpu_values = [s.gpu_percent for s in self.samples]
        freq_values = [s.gpu_freq_mhz for s in self.samples]
        
        self.cpu_mean = sum(cpu_values) / len(cpu_values)
        self.cpu_max = max(cpu_values)
        self.cpu_min = min(cpu_values)
        
        self.gpu_mean = sum(gpu_values) / len(gpu_values)
        self.gpu_max = max(gpu_values)
        self.gpu_min = min(gpu_values)
        
        self.gpu_freq_mean = sum(freq_values) / len(freq_values)
        self.gpu_freq_max = max(freq_values)


@dataclass
class BenchmarkReport:
    """Complete benchmark report."""
    timestamp: str
    system_info: Dict
    duration_per_test: float
    filter_results: List[BenchmarkResult] = field(default_factory=list)
    preset_results: List[BenchmarkResult] = field(default_factory=list)
    
    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON serialization."""
        return {
            "timestamp": self.timestamp,
            "system_info": self.system_info,
            "duration_per_test": self.duration_per_test,
            "filter_results": [
                {
                    "name": r.name,
                    "filter_name": r.filter_name,
                    "filter_value": r.filter_value,
                    "preset_name": r.preset_name,
                    "cpu_mean": round(r.cpu_mean, 2),
                    "cpu_max": round(r.cpu_max, 2),
                    "cpu_min": round(r.cpu_min, 2),
                    "gpu_mean": round(r.gpu_mean, 2),
                    "gpu_max": round(r.gpu_max, 2),
                    "gpu_min": round(r.gpu_min, 2),
                    "gpu_freq_mean": round(r.gpu_freq_mean, 1),
                    "gpu_freq_max": round(r.gpu_freq_max, 1),
                    "wall_time_seconds": round(r.wall_time_seconds, 2),
                    "user_cpu_seconds": round(r.user_cpu_seconds, 2),
                    "system_cpu_seconds": round(r.system_cpu_seconds, 2),
                }
                for r in self.filter_results
            ],
            "preset_results": [
                {
                    "name": r.name,
                    "preset_name": r.preset_name,
                    "cpu_mean": round(r.cpu_mean, 2),
                    "cpu_max": round(r.cpu_max, 2),
                    "cpu_min": round(r.cpu_min, 2),
                    "gpu_mean": round(r.gpu_mean, 2),
                    "gpu_max": round(r.gpu_max, 2),
                    "gpu_min": round(r.gpu_min, 2),
                    "gpu_freq_mean": round(r.gpu_freq_mean, 1),
                    "gpu_freq_max": round(r.gpu_freq_max, 1),
                    "wall_time_seconds": round(r.wall_time_seconds, 2),
                    "user_cpu_seconds": round(r.user_cpu_seconds, 2),
                    "system_cpu_seconds": round(r.system_cpu_seconds, 2),
                }
                for r in self.preset_results
            ],
        }


class ResourceMonitor:
    """Monitor CPU and GPU resources during benchmark."""
    
    def __init__(self, sample_interval_ms: int = 200):
        self.sample_interval = sample_interval_ms / 1000.0
        self.samples: List[ResourceSample] = []
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._gpu_monitor_proc: Optional[subprocess.Popen] = None
        self._gpu_samples: List[Dict] = []
        
    def start(self):
        """Start resource monitoring."""
        self.samples = []
        self._gpu_samples = []
        self._stop_event.clear()
        
        # Start intel_gpu_top in JSON mode
        self._start_gpu_monitor()
        
        # Start CPU monitoring thread
        self._thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self._thread.start()
    
    def stop(self) -> List[ResourceSample]:
        """Stop monitoring and return samples."""
        self._stop_event.set()
        
        if self._thread:
            self._thread.join(timeout=2.0)
        
        self._stop_gpu_monitor()
        
        # Merge GPU samples with CPU samples
        self._merge_gpu_samples()
        
        return self.samples
    
    def _start_gpu_monitor(self):
        """Start intel_gpu_top for GPU monitoring."""
        try:
            # Run intel_gpu_top with JSON output, 200ms interval
            self._gpu_monitor_proc = subprocess.Popen(
                ["sudo", "intel_gpu_top", "-J", "-s", str(int(self.sample_interval * 1000))],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
            )
        except Exception as e:
            print(f"Warning: Could not start intel_gpu_top: {e}", file=sys.stderr)
            self._gpu_monitor_proc = None
    
    def _stop_gpu_monitor(self):
        """Stop intel_gpu_top."""
        if self._gpu_monitor_proc:
            try:
                self._gpu_monitor_proc.send_signal(signal.SIGINT)
                stdout, _ = self._gpu_monitor_proc.communicate(timeout=2.0)
                self._parse_gpu_output(stdout)
            except Exception:
                self._gpu_monitor_proc.kill()
            self._gpu_monitor_proc = None
    
    def _parse_gpu_output(self, output: str):
        """Parse intel_gpu_top JSON output."""
        if not output:
            return
        
        # intel_gpu_top outputs one JSON object per sample
        # Each line is a complete JSON object
        for line in output.strip().split('\n'):
            line = line.strip()
            if not line or not line.startswith('{'):
                continue
            try:
                data = json.loads(line)
                self._gpu_samples.append(data)
            except json.JSONDecodeError:
                continue
    
    def _merge_gpu_samples(self):
        """Merge GPU samples into resource samples."""
        # If we have GPU samples, use them to fill in GPU data
        for i, sample in enumerate(self.samples):
            if i < len(self._gpu_samples):
                gpu_data = self._gpu_samples[i]
                # Extract render/3D engine usage
                engines = gpu_data.get("engines", {})
                render_busy = 0.0
                for engine_name, engine_data in engines.items():
                    if "Render" in engine_name or "3D" in engine_name:
                        render_busy = max(render_busy, engine_data.get("busy", 0.0))
                sample.gpu_percent = render_busy
                
                # Extract frequency
                freq = gpu_data.get("frequency", {})
                sample.gpu_freq_mhz = int(freq.get("actual", 0))
    
    def _monitor_loop(self):
        """Main monitoring loop for CPU."""
        prev_idle = 0
        prev_total = 0
        
        while not self._stop_event.is_set():
            timestamp = time.time()
            
            # Read CPU stats from /proc/stat
            try:
                with open("/proc/stat", "r") as f:
                    line = f.readline()
                parts = line.split()
                # user, nice, system, idle, iowait, irq, softirq, steal
                times = [int(x) for x in parts[1:9]]
                idle = times[3] + times[4]  # idle + iowait
                total = sum(times)
                
                if prev_total > 0:
                    diff_idle = idle - prev_idle
                    diff_total = total - prev_total
                    cpu_percent = 100.0 * (1.0 - diff_idle / diff_total) if diff_total > 0 else 0.0
                else:
                    cpu_percent = 0.0
                
                prev_idle = idle
                prev_total = total
            except Exception:
                cpu_percent = 0.0
            
            # Read GPU frequency from sysfs (fallback)
            gpu_freq = 0
            try:
                freq_path = Path("/sys/class/drm/card1/gt_cur_freq_mhz")
                if freq_path.exists():
                    gpu_freq = int(freq_path.read_text().strip())
            except Exception:
                pass
            
            sample = ResourceSample(
                timestamp=timestamp,
                cpu_percent=cpu_percent,
                gpu_percent=0.0,  # Filled in by GPU monitor
                gpu_freq_mhz=gpu_freq,
                gpu_power_w=0.0,
            )
            self.samples.append(sample)
            
            time.sleep(self.sample_interval)


def get_system_info() -> Dict:
    """Gather system information for the report."""
    info = {
        "cpu": "Unknown",
        "gpu": "Unknown",
        "os": "Unknown",
        "obs_version": "Unknown",
        "kernel": "Unknown",
    }
    
    # CPU
    try:
        with open("/proc/cpuinfo", "r") as f:
            for line in f:
                if "model name" in line:
                    info["cpu"] = line.split(":")[1].strip()
                    break
    except Exception:
        pass
    
    # GPU
    try:
        result = subprocess.run(
            ["lspci", "-nn"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        for line in result.stdout.split('\n'):
            if "VGA" in line and "Intel" in line:
                info["gpu"] = line.split(": ")[1] if ": " in line else line
                break
    except Exception:
        pass
    
    # OS
    try:
        with open("/etc/os-release", "r") as f:
            for line in f:
                if line.startswith("PRETTY_NAME="):
                    info["os"] = line.split("=")[1].strip().strip('"')
                    break
    except Exception:
        pass
    
    # Kernel
    try:
        result = subprocess.run(["uname", "-r"], capture_output=True, text=True, timeout=5)
        info["kernel"] = result.stdout.strip()
    except Exception:
        pass
    
    # OBS version
    try:
        result = subprocess.run(["obs", "--version"], capture_output=True, text=True, timeout=5)
        match = re.search(r"(\d+\.\d+\.\d+)", result.stdout + result.stderr)
        if match:
            info["obs_version"] = match.group(1)
    except Exception:
        pass
    
    return info


def create_test_scene_json(output_path: Path, filter_overrides: Dict) -> None:
    """Create an OBS scene JSON with specific filter settings."""
    # Start with baseline (all effects off)
    settings = {**BASELINE_FILTERS, **filter_overrides}
    
    # Map filter names to OBS property names
    scene = {
        "current_scene": "C64StreamTest",
        "current_program_scene": "C64StreamTest",
        "scene_order": [{"name": "C64StreamTest"}],
        "sources": [
            {
                "name": "C64 Stream Benchmark",
                "uuid": "benchmark-source-uuid",
                "id": "c64_source",
                "versioned_id": "c64_source",
                "settings": {
                    "c64u_host": "0.0.0.0",
                    "video_port": 11000,
                    "audio_port": 11001,
                    "buffer_delay_ms": 10,
                    "scan_line_distance": settings["scan_line_distance"],
                    "scan_line_strength": settings["scan_line_strength"],
                    "blur_strength": settings["blur_strength"],
                    "bloom_strength": settings["bloom_strength"],
                    "afterglow_duration_ms": settings["afterglow_duration_ms"],
                    "afterglow_curve": settings.get("afterglow_curve", 0),
                    "tint_mode": settings["tint_mode"],
                    "tint_strength": settings["tint_strength"],
                    "pixel_width": settings["pixel_width"],
                    "pixel_height": settings["pixel_height"],
                },
                "flags": 0,
                "enabled": True,
            }
        ],
    }
    
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w") as f:
        json.dump(scene, f, indent=2)


def run_benchmark_scenario(
    name: str,
    filter_overrides: Dict,
    duration_seconds: int,
    verbose: bool = False,
) -> BenchmarkResult:
    """Run a single benchmark scenario and measure resources."""
    
    result = BenchmarkResult(
        name=name,
        filter_name=None,
        filter_value=None,
        preset_name=None,
        duration_seconds=duration_seconds,
    )
    
    # Create temporary directory for this test
    with tempfile.TemporaryDirectory(prefix="c64_benchmark_") as temp_dir:
        temp_path = Path(temp_dir)
        
        # Create scene JSON with filter settings
        scene_path = temp_path / "scenes" / "C64StreamTest.json"
        create_test_scene_json(scene_path, filter_overrides)
        
        # Build the e2e.sh command
        e2e_cmd = [
            str(SCRIPT_DIR / "e2e.sh"),
            "--format", "NTSC",
            "--duration", str(duration_seconds),
            "--scenario-overrides", str(temp_path),
        ]
        
        if not verbose:
            e2e_cmd.append("--quiet")
        
        # Start resource monitor
        monitor = ResourceMonitor(sample_interval_ms=200)
        monitor.start()
        
        start_time = time.time()
        
        try:
            # Run E2E test
            proc = subprocess.run(
                e2e_cmd,
                cwd=SCRIPT_DIR,
                capture_output=not verbose,
                text=True,
                timeout=duration_seconds * 4 + 60,  # Allow extra time for setup/teardown
            )
            
            # Get rusage for CPU time
            # Note: This only captures the e2e.sh process, not OBS
            # For accurate CPU time, we rely on /proc/stat sampling
            
        except subprocess.TimeoutExpired:
            print(f"Warning: Benchmark '{name}' timed out", file=sys.stderr)
        except Exception as e:
            print(f"Warning: Benchmark '{name}' failed: {e}", file=sys.stderr)
        
        wall_time = time.time() - start_time
        result.wall_time_seconds = wall_time
        
        # Stop monitor and get samples
        result.samples = monitor.stop()
        result.compute_statistics()
    
    return result


def run_filter_benchmark(
    filter_name: str,
    values: List,
    duration_seconds: int,
    verbose: bool = False,
) -> List[BenchmarkResult]:
    """Benchmark a single filter across multiple values."""
    results = []
    
    for value in values:
        name = f"{filter_name}={value}"
        print(f"  Benchmarking: {name}")
        
        filter_overrides = {filter_name: value}
        result = run_benchmark_scenario(name, filter_overrides, duration_seconds, verbose)
        result.filter_name = filter_name
        result.filter_value = str(value)
        results.append(result)
    
    return results


def run_preset_benchmark(
    preset_name: str,
    duration_seconds: int,
    verbose: bool = False,
) -> BenchmarkResult:
    """Benchmark a preset using the existing E2E scenario."""
    
    # Map preset name to scenario name
    scenario_map = {
        "Default": "ntsc_default",
        "Classic CRT": "ntsc_classic_crt",
        "Amber Monitor": "ntsc_amber_monitor",
        "Green Monitor": "ntsc_green_monitor",
        "Sharp Pixels": "ntsc_sharp_pixels",
        "Phosphor Glow": "ntsc_phosphor_glow",
        "Vintage TV": "ntsc_vintage_tv",
        "Arcade Cabinet": "ntsc_arcade_cabinet",
    }
    
    scenario_name = scenario_map.get(preset_name)
    if not scenario_name:
        print(f"Warning: No scenario for preset '{preset_name}'", file=sys.stderr)
        return BenchmarkResult(
            name=preset_name,
            filter_name=None,
            filter_value=None,
            preset_name=preset_name,
            duration_seconds=duration_seconds,
        )
    
    result = BenchmarkResult(
        name=preset_name,
        filter_name=None,
        filter_value=None,
        preset_name=preset_name,
        duration_seconds=duration_seconds,
    )
    
    # Build the e2e.sh command using existing scenario
    e2e_cmd = [
        str(SCRIPT_DIR / "e2e.sh"),
        "--scenario", scenario_name,
        "--duration", str(duration_seconds),
    ]
    
    if not verbose:
        # Redirect output but don't use --quiet as it may not exist
        pass
    
    # Start resource monitor
    monitor = ResourceMonitor(sample_interval_ms=200)
    monitor.start()
    
    start_time = time.time()
    
    try:
        proc = subprocess.run(
            e2e_cmd,
            cwd=SCRIPT_DIR,
            capture_output=True,
            text=True,
            timeout=duration_seconds * 4 + 120,
        )
        
        if proc.returncode != 0 and verbose:
            print(f"E2E output: {proc.stdout[-500:]}", file=sys.stderr)
            
    except subprocess.TimeoutExpired:
        print(f"Warning: Preset benchmark '{preset_name}' timed out", file=sys.stderr)
    except Exception as e:
        print(f"Warning: Preset benchmark '{preset_name}' failed: {e}", file=sys.stderr)
    
    result.wall_time_seconds = time.time() - start_time
    result.samples = monitor.stop()
    result.compute_statistics()
    
    return result


def print_summary_table(report: BenchmarkReport):
    """Print a human-readable summary table."""
    
    print("\n" + "=" * 80)
    print("FILTER PERFORMANCE BENCHMARK RESULTS")
    print("=" * 80)
    print(f"System: {report.system_info.get('cpu', 'Unknown')}")
    print(f"GPU: {report.system_info.get('gpu', 'Unknown')}")
    print(f"Duration per test: {report.duration_per_test}s")
    print("=" * 80)
    
    if report.preset_results:
        print("\n### PRESET RESULTS ###")
        print("-" * 80)
        print(f"{'Preset':<20} {'CPU Mean':>10} {'CPU Max':>10} {'GPU Mean':>10} {'GPU Max':>10} {'GPU Freq':>10}")
        print(f"{'':20} {'(%)':>10} {'(%)':>10} {'(%)':>10} {'(%)':>10} {'(MHz)':>10}")
        print("-" * 80)
        
        for r in report.preset_results:
            print(f"{r.name:<20} {r.cpu_mean:>10.1f} {r.cpu_max:>10.1f} "
                  f"{r.gpu_mean:>10.1f} {r.gpu_max:>10.1f} {r.gpu_freq_mean:>10.0f}")
    
    if report.filter_results:
        print("\n### INDIVIDUAL FILTER RESULTS ###")
        print("-" * 80)
        print(f"{'Filter':<30} {'CPU Mean':>10} {'CPU Max':>10} {'GPU Mean':>10} {'GPU Max':>10}")
        print(f"{'':30} {'(%)':>10} {'(%)':>10} {'(%)':>10} {'(%)':>10}")
        print("-" * 80)
        
        for r in report.filter_results:
            print(f"{r.name:<30} {r.cpu_mean:>10.1f} {r.cpu_max:>10.1f} "
                  f"{r.gpu_mean:>10.1f} {r.gpu_max:>10.1f}")
    
    print("=" * 80)


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark C64 Stream CRT filters and presets",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Run all presets benchmark
  ./benchmark_filters.py --presets-only --duration 10 --output results.json

  # Run all filters benchmark
  ./benchmark_filters.py --filters-only --duration 5 --output results.json

  # Run full benchmark (presets + filters)
  ./benchmark_filters.py --duration 10 --output results.json

  # Benchmark specific preset
  ./benchmark_filters.py --preset "Green Monitor" --duration 10

  # Benchmark specific filter with custom values
  ./benchmark_filters.py --filter afterglow_duration_ms --values "0,40,80,120"
""",
    )
    
    parser.add_argument(
        "--output", "-o",
        type=Path,
        default=SCRIPT_DIR / "benchmark_results.json",
        help="Output JSON file path (default: benchmark_results.json)",
    )
    parser.add_argument(
        "--duration", "-d",
        type=int,
        default=10,
        help="Duration in seconds for each test (default: 10)",
    )
    parser.add_argument(
        "--presets-only",
        action="store_true",
        help="Only benchmark effect presets",
    )
    parser.add_argument(
        "--filters-only",
        action="store_true",
        help="Only benchmark individual filters",
    )
    parser.add_argument(
        "--preset",
        type=str,
        help="Benchmark a specific preset only",
    )
    parser.add_argument(
        "--filter",
        type=str,
        help="Benchmark a specific filter only",
    )
    parser.add_argument(
        "--values",
        type=str,
        help="Comma-separated values to test for --filter",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Show verbose output",
    )
    parser.add_argument(
        "--skip-gpu",
        action="store_true",
        help="Skip GPU monitoring (if intel_gpu_top unavailable)",
    )
    
    args = parser.parse_args()
    
    # Check for required tools
    if not shutil.which("intel_gpu_top") and not args.skip_gpu:
        print("Warning: intel_gpu_top not found. GPU stats will be limited.", file=sys.stderr)
        print("Install with: sudo apt install intel-gpu-tools", file=sys.stderr)
    
    # Check if we need sudo for intel_gpu_top
    if not args.skip_gpu:
        try:
            result = subprocess.run(
                ["sudo", "-n", "true"],
                capture_output=True,
                timeout=5,
            )
            if result.returncode != 0:
                print("Warning: sudo access needed for intel_gpu_top GPU monitoring.", file=sys.stderr)
                print("Run with --skip-gpu to disable GPU monitoring, or configure passwordless sudo.", file=sys.stderr)
        except Exception:
            pass
    
    print("C64 Stream Filter Performance Benchmark")
    print("=" * 50)
    
    # Gather system info
    print("Gathering system information...")
    system_info = get_system_info()
    
    report = BenchmarkReport(
        timestamp=datetime.now().isoformat(),
        system_info=system_info,
        duration_per_test=args.duration,
    )
    
    # Run benchmarks
    if args.preset:
        # Single preset
        print(f"\nBenchmarking preset: {args.preset}")
        result = run_preset_benchmark(args.preset, args.duration, args.verbose)
        report.preset_results.append(result)
        
    elif args.filter:
        # Single filter
        if args.values:
            values = [float(v) if '.' in v else int(v) for v in args.values.split(',')]
        else:
            values = FILTER_TEST_VALUES.get(args.filter, [0, 1])
        
        print(f"\nBenchmarking filter: {args.filter}")
        results = run_filter_benchmark(args.filter, values, args.duration, args.verbose)
        report.filter_results.extend(results)
        
    else:
        # Full benchmark
        if not args.filters_only:
            print("\n### Benchmarking Effect Presets ###")
            for preset in EFFECT_PRESETS:
                print(f"  Benchmarking preset: {preset}")
                result = run_preset_benchmark(preset, args.duration, args.verbose)
                report.preset_results.append(result)
        
        if not args.presets_only:
            print("\n### Benchmarking Individual Filters ###")
            for filter_name, values in FILTER_TEST_VALUES.items():
                print(f"  Filter: {filter_name}")
                results = run_filter_benchmark(filter_name, values, args.duration, args.verbose)
                report.filter_results.extend(results)
    
    # Save JSON report
    print(f"\nSaving results to: {args.output}")
    with open(args.output, "w") as f:
        json.dump(report.to_dict(), f, indent=2)
    
    # Print summary
    print_summary_table(report)
    
    print(f"\nResults saved to: {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
