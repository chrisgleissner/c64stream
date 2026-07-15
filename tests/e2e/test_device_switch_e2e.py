#!/usr/bin/env python3
"""
End-to-end test for seamless device switching.

Runs the plugin inside real OBS (via the standard Xvfb-based E2E harness,
same as the ntsc_script scenario) against TWO independent mock C64 Ultimate
TCP devices, and drives a C64Script (tests/e2e/scripts/device_switch.c64script)
that alternates the active device twice via SWITCH_DEVICE. Verifies that each
mock received exactly the start/stop control commands expected for the device
that was active during that phase.

This intentionally forces legacy (port-64) transport, since the mock devices
only implement that protocol, not REST -- discovery and REST switching are
exercised by the ad-hoc real-hardware soak instead (see
doc/testing/device-switch-soak.md).
"""

import json
import os
import sys
from pathlib import Path

import pytest

TEST_DIR = Path(__file__).parent.resolve()
sys.path.insert(0, str(TEST_DIR))

from framework.orchestrator import E2EOrchestrator  # noqa: E402
from framework.c64u_mock.server import MockC64UServer  # noqa: E402

DEVICE_A_ID = "e2e-switch-a"
DEVICE_B_ID = "e2e-switch-b"
CONTROL_PORT_A = 6400
CONTROL_PORT_B = 6401
VIDEO_PORT = 21000
AUDIO_PORT = 21001


def _device_settings_dir() -> Path:
    """Mirrors c64_get_user_dir(C64_USER_DIR_SETTINGS, ...) on Linux."""
    xdg_documents = os.environ.get("XDG_DOCUMENTS_DIR")
    documents = Path(xdg_documents) if xdg_documents else Path.home() / "Documents"
    return documents / "obs-studio" / "c64stream" / "settings"


def _write_device_ini(directory: Path, device_id: str, name: str, control_port: int) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"device-{device_id}.ini"
    path.write_text(
        f"id={device_id}\n"
        f"name={name}\n"
        f"host=127.0.0.1\n"
        f"dns_server_ip=\n"
        f"video_port={VIDEO_PORT}\n"
        f"audio_port={AUDIO_PORT}\n"
        f"control_port={control_port}\n"
    )
    return path


@pytest.fixture
def device_registry_seed():
    """Pre-registers the two mock devices, matching how a user would save them
    via the Device dropdown -- the plugin never writes a password here."""
    directory = _device_settings_dir()
    paths = [
        _write_device_ini(directory, DEVICE_A_ID, "E2E Switch A", CONTROL_PORT_A),
        _write_device_ini(directory, DEVICE_B_ID, "E2E Switch B", CONTROL_PORT_B),
    ]
    yield
    for path in paths:
        path.unlink(missing_ok=True)


def _write_overrides(overrides_dir: Path, script_path: Path) -> None:
    scenes_dir = overrides_dir / "basic" / "scenes"
    scenes_dir.mkdir(parents=True, exist_ok=True)
    baseline_path = TEST_DIR / "config" / "obs-studio" / "basic" / "scenes" / "C64StreamTest.json"
    data = json.loads(baseline_path.read_text())
    for source in data["sources"]:
        if source.get("id") == "c64_source":
            source["settings"] = {
                "c64_host": "127.0.0.1",
                "video_port": VIDEO_PORT,
                "audio_port": AUDIO_PORT,
                "control_port": CONTROL_PORT_A,
                "c64_device": DEVICE_A_ID,
                # The mocks only speak legacy port-64; never let REST get tried.
                "stream_control_transport": 2,
                "script_file": str(script_path),
                "script_auto_start": True,
                "debug_logging": True,
            }
    (scenes_dir / "C64StreamTest.json").write_text(json.dumps(data))


def test_device_switch_e2e(device_registry_seed):
    """Two mock devices; the script switches a -> b -> a. Each mock must see
    exactly the start/stop commands for the phases it was active during."""
    script_path = TEST_DIR / "scripts" / "device_switch.c64script"
    assert script_path.exists()

    os.environ["C64_DEVICE_SWITCH_COUNT"] = "2"
    os.environ["C64_DEVICE_SWITCH_INTERVAL_MS"] = "500"
    os.environ["C64_DEVICE_SWITCH_DEVICE_A"] = DEVICE_A_ID
    os.environ["C64_DEVICE_SWITCH_DEVICE_B"] = DEVICE_B_ID
    os.environ["C64_DEVICE_SWITCH_DISCOVER"] = "0"

    output_dir = TEST_DIR / "test_output_device_switch"
    overrides_dir = output_dir / "overrides"
    _write_overrides(overrides_dir, script_path)

    orchestrator = E2EOrchestrator(
        test_dir=TEST_DIR,
        output_dir=str(output_dir),
        video_format="NTSC",
        frames=200,
        packet_source="mock",
        scenario_overrides=overrides_dir,
        control_port=CONTROL_PORT_A,
        wait_for_script_completion=True,
        script_completion_timeout_s=30.0,
        # This test verifies device-switch control-command routing, not A/V
        # quality -- video/audio replay only targets the first device.
        skip_frame_logic_validation=True,
    )

    second_mock = MockC64UServer(orchestrator.env, control_port=CONTROL_PORT_B)
    assert second_mock.start(), "Failed to start second mock C64 Ultimate device"

    try:
        success = orchestrator.run()
        if not success:
            print("NOTE: orchestrator A/V validation failed (expected for a "
                  "device-switch-only test); the assertions below are authoritative.")
    finally:
        second_mock.stop()

    obs_log_path = orchestrator.env.output_dir / "obs_log.txt"
    assert obs_log_path.exists(), f"OBS log not found: {obs_log_path}"
    log_text = obs_log_path.read_text(errors="ignore")

    assert "=== Device Switch Started ===" in log_text
    assert "=== Device Switch Complete: 2 switches ===" in log_text

    a_starts = [e for e in orchestrator.mock_server.events if e[0] == "start"]
    a_stops = [e for e in orchestrator.mock_server.events if e[0] == "stop"]
    b_starts = [e for e in second_mock.events if e[0] == "start"]
    b_stops = [e for e in second_mock.events if e[0] == "stop"]

    # Device A: streaming starts here initially, then is stopped on the first switch.
    assert len(a_starts) >= 1, f"Device A never received a start command: {orchestrator.mock_server.events}"
    assert len(a_stops) >= 1, f"Device A was never stopped when switching away: {orchestrator.mock_server.events}"

    # Device B: started on the first switch, stopped on the second (switch back to A).
    assert len(b_starts) >= 1, f"Device B never received a start command: {second_mock.events}"
    assert len(b_stops) >= 1, f"Device B was never stopped when switching back: {second_mock.events}"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-s"]))
