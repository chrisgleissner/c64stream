#!/usr/bin/env python3
"""
C64 Stream - E2E Test Orchestrator
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

This script acts as the entry point for the modular E2E test framework.
It parses arguments, builds necessary tools (udp_replay), and launches the Orchestrator.
"""

import os
import sys
import argparse
import subprocess
import logging
import yaml
from pathlib import Path


# Ensure we can import the framework
TEST_DIR = Path(__file__).parent.resolve()
sys.path.insert(0, str(TEST_DIR))

from framework.orchestrator import E2EOrchestrator
from framework.validation.network import NetworkTimingValidator

# Expose validation function for unit tests
validate_network_timing = NetworkTimingValidator.validate

def build_udp_replay(test_dir: Path, is_ci: bool) -> Path:
    """Ensure udp_replay tool is built and available."""
    script_dir = test_dir
    udp_replay_src = script_dir / "util" / "udp_replay.c"

    default_udp_replay = script_dir / "util" / "udp_replay"
    tool_dir = test_dir / ".e2e-tools"
    tool_dir.mkdir(parents=True, exist_ok=True)
    built_udp_replay = tool_dir / "udp_replay"

    is_windows = sys.platform.startswith('win')

    target_path = built_udp_replay
    if not udp_replay_src.exists():
         if default_udp_replay.exists(): return default_udp_replay
         print(f"❌ UDP replay source not found: {udp_replay_src}")
         return None

    rebuild = False
    if not target_path.exists():
         rebuild = True
    elif udp_replay_src.stat().st_mtime > target_path.stat().st_mtime:
         rebuild = True
    elif is_ci and not is_windows:
         rebuild = True # Force rebuild on CI

    if rebuild:
         print(f"🔨 Building UDP replay tool: {target_path}")
         build_cmd = ["gcc", "-O2", "-o", str(target_path), str(udp_replay_src)]
         try:
             subprocess.run(build_cmd, check=True, capture_output=True, text=True)
             print(f"✅ Successfully built UDP replay tool: {target_path}")
         except subprocess.CalledProcessError as e:
             print("❌ Failed to build UDP replay tool:")
             print(f"   Command: {' '.join(build_cmd)}")
             print(f"   Error: {e.stderr}")
             return None
         except FileNotFoundError:
             print("❌ gcc compiler not found. Install build-essential package.")
             return None

    return target_path

def main():
    parser = argparse.ArgumentParser(description='C64 Stream E2E Test')

    # Core configuration
    parser.add_argument('--test-dir', type=str, default=str(TEST_DIR),
                        help='Base directory for E2E tests')
    parser.add_argument('--output-dir', type=str, default=None,
                        help='Directory for test outputs (default: test_dir/test_output)')
    parser.add_argument('--udp-replay', type=str, default=str(TEST_DIR / "util" / "udp_replay"),
                        help='Path to udp_replay tool')

    # Test Parameters
    parser.add_argument('--format', choices=['PAL', 'NTSC'], default='PAL',
                        help='Video format (PAL/NTSC)')
    parser.add_argument('--frames', type=int, default=100,
                        help='Number of frames to record')
    parser.add_argument('--packet-source', choices=['mock', 'device', 'media'], default='mock',
                        help='Source of packets (mock=generate/replay, device=wait for external, media=media source)')

    # Network / Simulation
    parser.add_argument('--video-port', type=int, default=21000,
                        help='UDP port for video packets (default: 21000)')
    parser.add_argument('--audio-port', type=int, default=21001,
                        help='UDP port for audio packets (default: 21001)')
    parser.add_argument('--control-port', type=int, default=6400,
                        help='TCP control port (default: 6400)')
    parser.add_argument('--scenario-overrides', type=str, default=None,
                        help='Path to directory containing scenario-specific OBS config overrides')
    parser.add_argument('--scenario-yaml', type=str, default=None,
                        help='Path to scenario YAML file (for loading network_simulation config)')

    # Features
    parser.add_argument('--enable-websocket', action='store_true',
                        help='Enable OBS WebSocket control')
    parser.add_argument('--enable-resource-monitoring', action='store_true',
                        help='Enable CPU/Memory monitoring')
    parser.add_argument('--monitor-resource-duration', type=float, default=0.2,
                        help='Resource monitoring interval in seconds')
    parser.add_argument('--full-frame-pop', action='store_true',
                        help='Enable full-frame analysis for pop detection (slower)')
    parser.add_argument('--csv-max-rows', type=int, default=0,
                        help='Limit CSV rows copied to output (0=unlimited)')

    # Debugging
    parser.add_argument('--verbose', action='store_true',
                        help='Enable verbose logging')

    # Legacy/Ignored args
    parser.add_argument('--scenario-name', type=str, default=None, help='(Legacy)')
    parser.add_argument('--scenario-id', type=str, default=None, help='(Legacy)')
    parser.add_argument('--perf-profile', action='store_true', help='(Legacy)')
    parser.add_argument('--perf-frequency-hz', type=int, default=99, help='(Legacy)')
    parser.add_argument('--perf-callgraph', choices=['dwarf', 'fp'], default='dwarf', help='(Legacy)')
    parser.add_argument('--perf-duration', type=float, default=None, help='(Legacy)')
    parser.add_argument('--perf-flamegraph', action='store_true', help='(Legacy)')
    parser.add_argument('--settling-duration', '--settling-seconds', dest='settling_seconds', type=float, default=0.0,
                        help='(Legacy)')

    args = parser.parse_args()

    # Configure Logging
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format='%(asctime)s [%(levelname)s] %(name)s: %(message)s',
        datefmt='%H:%M:%S'
    )

    # Load Network Simulation and Tolerances
    network_simulation = {}
    av_sync_tolerance_mode = None  # None, 'lenient', or numeric value
    obs_start_recording = True
    if args.scenario_yaml:
        try:
            with open(args.scenario_yaml, 'r') as f:
                scenario_data = yaml.safe_load(f)
                network_simulation = scenario_data.get('network_simulation', {})
                if 'obs_start_recording' in scenario_data:
                    obs_start_recording = bool(scenario_data['obs_start_recording'])

                # Support both old and new tolerance formats
                # Old: av_sync_tolerance_ms: 40
                # New: tolerances.av_sync.ci: lenient
                if 'av_sync_tolerance_ms' in scenario_data:
                    av_sync_tolerance_mode = scenario_data['av_sync_tolerance_ms']
                elif 'tolerances' in scenario_data and 'av_sync' in scenario_data['tolerances']:
                    av_sync_tol = scenario_data['tolerances']['av_sync']
                    is_ci = os.environ.get('CI', '').lower() in ('1', 'true', 'yes') or \
                            os.environ.get('GITHUB_ACTIONS', '').lower() in ('1', 'true', 'yes')

                    # Check for CI-specific or local-specific tolerance
                    if is_ci and 'ci' in av_sync_tol:
                        av_sync_tolerance_mode = av_sync_tol['ci']
                    elif not is_ci and 'local' in av_sync_tol:
                        av_sync_tolerance_mode = av_sync_tol['local']

                print(f"📡 Loaded scenario config from {Path(args.scenario_yaml).name}")
        except Exception as e:
            print(f"⚠️  Failed to load scenario YAML: {e}")

    # Build udp_replay
    is_ci = os.environ.get('CI', '').lower() in ('1', 'true', 'yes')
    if args.packet_source == 'mock':
        udp_replay_path = Path(args.udp_replay)
        if udp_replay_path == Path(TEST_DIR / "util" / "udp_replay") or not udp_replay_path.exists():
             built = build_udp_replay(Path(args.test_dir), is_ci)
             if built:
                  udp_replay_path = built
             elif not udp_replay_path.exists():
                  return 1
    else:
        udp_replay_path = None

    # Instantiate Orchestrator
    orchestrator = E2EOrchestrator(
        test_dir=Path(os.path.abspath(args.test_dir)),
        output_dir=args.output_dir,
        video_format=args.format,
        frames=args.frames,
        udp_replay_path=str(udp_replay_path) if udp_replay_path else None,
        packet_source=args.packet_source,
        scenario_overrides=Path(args.scenario_overrides) if args.scenario_overrides else None,
        network_simulation=network_simulation,
        obs_start_recording=obs_start_recording,
        enable_websocket=args.enable_websocket,
        enable_resource_monitoring=args.enable_resource_monitoring,
        monitor_resource_interval_ms=int(args.monitor_resource_duration * 1000),
        control_port=args.control_port,
        csv_max_rows=args.csv_max_rows if args.csv_max_rows > 0 else None,
        verbose=args.verbose,
        full_frame_pop=args.full_frame_pop,
        av_sync_tolerance_mode=av_sync_tolerance_mode
    )

    return 0 if orchestrator.run() else 1

if __name__ == '__main__':
    sys.exit(main())
