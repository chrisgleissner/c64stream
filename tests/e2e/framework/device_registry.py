"""
Seeds/removes c64stream device-registry .ini files so a scenario can start
with pre-registered devices (e.g. two mock devices for a device-switch test),
mirroring c64_get_user_dir(C64_USER_DIR_SETTINGS, ...) on Linux.
"""

from __future__ import annotations
import os
from pathlib import Path
from typing import Any, Dict, List


def settings_dir() -> Path:
    xdg_documents = os.environ.get("XDG_DOCUMENTS_DIR")
    documents = Path(xdg_documents) if xdg_documents else Path.home() / "Documents"
    return documents / "obs-studio" / "c64stream" / "settings"


def write_device_ini(device: Dict[str, Any]) -> Path:
    directory = settings_dir()
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"device-{device['id']}.ini"
    path.write_text(
        f"id={device['id']}\n"
        f"name={device.get('name', device['id'])}\n"
        f"host={device['host']}\n"
        f"dns_server_ip={device.get('dns_server_ip', '')}\n"
        f"video_port={device['video_port']}\n"
        f"audio_port={device['audio_port']}\n"
        f"control_port={device['control_port']}\n"
    )
    return path


def seed_devices(devices: List[Dict[str, Any]]) -> List[Path]:
    return [write_device_ini(device) for device in devices]


def remove_devices(paths: List[Path]) -> None:
    for path in paths:
        try:
            path.unlink(missing_ok=True)
        except Exception:
            pass
