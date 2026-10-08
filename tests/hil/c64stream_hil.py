#!/usr/bin/env python3
"""Hardware-in-the-loop tests for device discovery and device switch-over.

Runs a real, headless OBS Studio instance (Xvfb) with the freshly built plugin
in an isolated HOME, so the developer's own OBS configuration, scenes and
device registry are never touched. The harness drives OBS through
obs-websocket and judges the result from what the source actually renders.

Each physical device is fingerprinted by temporarily setting its VIC-II border
colour over REST, so a source screenshot shows unambiguously which device's
video OBS is displaying. The original border colours are restored on exit.

Network faults are injected on the host with iptables/tcpdump running in a
privileged helper container (docker, --net=host), so no sudo is needed. All
rules live in a dedicated chain that is removed on exit.

LOCAL ONLY: requires real C64 Ultimate / Ultimate 64 devices on the LAN, docker
and Xvfb. Never run in CI. See doc/testing/discovery-hil.md.
"""

from __future__ import annotations

import argparse
import base64
import contextlib
import io
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

try:
    import websocket  # websocket-client
    from PIL import Image
except ImportError as error:  # pragma: no cover - local tool
    sys.exit(f"missing dependency: {error} (pip install websocket-client pillow)")

REPO_ROOT = Path(__file__).resolve().parents[2]
NETCTL_IMAGE = "c64stream-netctl:local"
CHAIN = "C64HIL"
SOURCE_NAME = "C64 HIL"
SCENE_NAME = "HIL"

# VIC-II colour index -> approximate PAL RGB (Pepto). Only the fingerprint
# colours need to be told apart, so a nearest-colour match is sufficient.
VIC_RGB = {
    2: (0x88, 0x39, 0x32),  # red
    5: (0x55, 0xA0, 0x49),  # green
    7: (0xBF, 0xCE, 0x72),  # yellow
    0: (0x00, 0x00, 0x00),  # black
    6: (0x40, 0x31, 0x8D),  # blue
    14: (0x6C, 0x5E, 0xB5),  # light blue
}


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


# --------------------------------------------------------------------------
# Devices


@dataclass
class Device:
    label: str
    host: str
    border: int
    info: dict = field(default_factory=dict)
    original_border: int | None = None
    marker_running: bool = False

    @property
    def device_id(self) -> str:
        uid = self.info.get("unique_id", "")
        return "".join(ch.lower() if ch.isalnum() else "-" for ch in uid).strip("-")

    def rest(self, method: str, path: str, timeout: float = 3.0, attempts: int = 3) -> bytes:
        # The Ultimate's web server occasionally stalls a single request.
        for attempt in range(attempts):
            try:
                request = urllib.request.Request(f"http://{self.host}{path}", method=method)
                with urllib.request.urlopen(request, timeout=timeout) as response:
                    return response.read()
            except OSError:
                if attempt == attempts - 1:
                    raise
                time.sleep(0.3)
        raise AssertionError("unreachable")

    def load_info(self) -> None:
        self.info = json.loads(self.rest("GET", "/v1/info"))

    def read_border(self) -> int:
        return self.rest("GET", "/v1/machine:readmem?address=D020&length=1")[0] & 0x0F

    def set_border(self, colour: int) -> None:
        self.rest("PUT", f"/v1/machine:writemem?address=D020&data={colour:02X}")

    def fingerprint(self) -> None:
        if self.original_border is None:
            self.original_border = self.read_border()
        self.set_border(self.border)

    def run_marker_program(self) -> None:
        """Runs a tiny program that paints this device's border colour and
        increments the first screen character forever. Every frame the device
        sends is then different from the previous one, so a recording shows
        live video apart from a frozen last frame."""
        loop = 0x0813
        code = bytes([0x78,                                  # SEI
                      0xA9, self.border, 0x8D, 0x20, 0xD0,   # LDA #border / STA $D020
                      0xEE, 0x00, 0x04,                      # loop: INC $0400
                      0x4C, loop & 0xFF, loop >> 8])         # JMP loop
        basic = bytes([0x0B, 0x08, 0x0A, 0x00, 0x9E]) + b"2061" + bytes([0x00, 0x00, 0x00])  # 10 SYS2061
        prg = bytes([0x01, 0x08]) + basic + code
        path = Path(tempfile.mkstemp(suffix=".prg")[1])
        path.write_bytes(prg)
        try:
            subprocess.run(["curl", "-s", "-f", "-m", "10", "-X", "POST", "-F", f"file=@{path}",
                            f"http://{self.host}/v1/runners:run_prg"], check=True, capture_output=True)
        finally:
            path.unlink()
        self.marker_running = True

    def reset_machine(self) -> None:
        with contextlib.suppress(Exception):
            self.rest("PUT", "/v1/machine:reset", attempts=2)

    # Palette Definition (Follow device tests) ----------------------------

    PALETTE_SETTING = "/v1/configs/U64%20Specific%20Settings/Palette%20Definition"

    def palette_setting(self) -> str:
        return json.loads(self.rest("GET", self.PALETTE_SETTING))["U64 Specific Settings"]["Palette Definition"]["current"]

    def set_palette_setting(self, name: str) -> None:
        self.rest("PUT", f"{self.PALETTE_SETTING}?value={urllib.parse.quote(name)}")

    def upload_palette(self, name: str, text: str) -> None:
        path = Path(tempfile.mkstemp(suffix=".vpl")[1])
        path.write_text(text)
        try:
            subprocess.run(["curl", "-s", "-f", "-m", "15", "--ftp-create-dirs", "-T", str(path),
                            f"ftp://{self.host}/Flash/data/{name}"], check=True, capture_output=True)
        finally:
            path.unlink()

    def has_flash_data_dir(self) -> bool:
        listing = subprocess.run(["curl", "-s", "-m", "10", "--list-only", f"ftp://{self.host}/Flash/"],
                                 capture_output=True, text=True)
        return "data" in listing.stdout.split()

    def remove_flash_data_dir(self) -> None:
        """Removes /Flash/data (only succeeds when it is empty)."""
        subprocess.run(["curl", "-s", "-m", "10", f"ftp://{self.host}/", "-Q", "RMD /Flash/data"],
                       capture_output=True)

    def delete_palette(self, name: str) -> None:
        subprocess.run(["curl", "-s", "-m", "10", f"ftp://{self.host}/", "-Q", f"DELE /Flash/data/{name}"],
                       capture_output=True)

    def graceful_power_off(self) -> None:
        """Shuts the machine down over REST before its outlet is cut. Refuses
        unless the machine powers itself on again when power returns, which
        is how it is brought back (REST cannot switch it on)."""
        config = json.loads(self.rest("GET", "/v1/configs/U64%20Specific%20Settings"))
        setting = config.get("U64 Specific Settings", {}).get("Power On After Power Loss")
        if setting != "On":
            raise RuntimeError(f"{self.label}: 'Power On After Power Loss' is {setting!r}, not 'On'")
        with contextlib.suppress(Exception):
            self.rest("PUT", "/v1/machine:poweroff", attempts=1)
        time.sleep(3)

    def restore(self) -> None:
        if self.marker_running:
            # The marker program owns the machine; a reset returns it to the
            # READY prompt it was found at (border colour included).
            self.reset_machine()
            return
        if self.original_border is not None:
            with contextlib.suppress(Exception):
                self.set_border(self.original_border)


def classify_colour(rgb: tuple[int, int, int]) -> int:
    return min(VIC_RGB, key=lambda c: sum((a - b) ** 2 for a, b in zip(VIC_RGB[c], rgb)))


# --------------------------------------------------------------------------
# Network fault injection (privileged helper container on the host netns)


class NetCtl:
    def __init__(self) -> None:
        self.active = False

    def _run(self, script: str, check: bool = True) -> str:
        result = subprocess.run(
            ["docker", "--context", os.environ.get("C64HIL_DOCKER_CONTEXT", "default"), "run", "--rm", "--net=host", "--cap-add=NET_ADMIN", "--cap-add=NET_RAW", NETCTL_IMAGE,
             "sh", "-c", script],
            capture_output=True, text=True, timeout=120)
        if check and result.returncode != 0:
            raise RuntimeError(f"netctl failed: {result.stderr.strip()}")
        return result.stdout

    def setup(self) -> None:
        self._run(f"iptables -N {CHAIN} 2>/dev/null; iptables -F {CHAIN}; "
                  f"iptables -C INPUT -j {CHAIN} 2>/dev/null || iptables -I INPUT -j {CHAIN}; "
                  f"iptables -C OUTPUT -j {CHAIN} 2>/dev/null || iptables -I OUTPUT -j {CHAIN}")
        self.active = True

    def block(self, *hosts: str) -> None:
        rules = "; ".join(f"iptables -A {CHAIN} -s {h} -j DROP; iptables -A {CHAIN} -d {h} -j DROP" for h in hosts)
        self._run(rules)

    def loss(self, probability: float, *hosts: str) -> None:
        """Randomly drops this share of packets to and from the hosts."""
        rules = "; ".join(
            f"iptables -A {CHAIN} -s {h} -m statistic --mode random --probability {probability} -j DROP; "
            f"iptables -A {CHAIN} -d {h} -m statistic --mode random --probability {probability} -j DROP"
            for h in hosts)
        self._run(rules)

    def block_tcp_port(self, host: str, port: int) -> None:
        self._run(f"iptables -A {CHAIN} -d {host} -p tcp --dport {port} -j DROP")

    def unblock_all(self) -> None:
        self._run(f"iptables -F {CHAIN}", check=False)

    def teardown(self) -> None:
        if not self.active:
            return
        self._run(f"iptables -D INPUT -j {CHAIN}; iptables -D OUTPUT -j {CHAIN}; "
                  f"iptables -F {CHAIN}; iptables -X {CHAIN}", check=False)
        self.active = False

    def count_udp_from(self, host: str, seconds: float) -> int:
        """Counts UDP packets a device sends to this machine over a window."""
        out = self._run(f"timeout {seconds} tcpdump -i any -n -q udp and src host {host} 2>/dev/null | wc -l",
                        check=False)
        try:
            return int(out.strip() or "0")
        except ValueError:
            return 0


# --------------------------------------------------------------------------
# OBS


class ObsWs:
    def __init__(self, port: int) -> None:
        self.ws = websocket.create_connection(f"ws://127.0.0.1:{port}", timeout=15)
        hello = json.loads(self.ws.recv())
        assert hello["op"] == 0, hello
        self.ws.send(json.dumps({"op": 1, "d": {"rpcVersion": 1, "eventSubscriptions": 0}}))
        identified = json.loads(self.ws.recv())
        assert identified["op"] == 2, identified
        self.counter = 0

    def call(self, request_type: str, data: dict | None = None) -> dict:
        self.counter += 1
        request_id = f"r{self.counter}"
        self.ws.send(json.dumps({"op": 6, "d": {"requestType": request_type, "requestId": request_id,
                                                "requestData": data or {}}}))
        while True:
            message = json.loads(self.ws.recv())
            if message.get("op") == 7 and message["d"].get("requestId") == request_id:
                status = message["d"]["requestStatus"]
                if not status.get("result"):
                    raise RuntimeError(f"{request_type} failed: {status}")
                return message["d"].get("responseData", {})

    def close(self) -> None:
        with contextlib.suppress(Exception):
            self.ws.close()


class Obs:
    def __init__(self, workdir: Path, plugin_so: Path, display: str, ws_port: int) -> None:
        self.workdir = workdir
        self.home = workdir / "home"
        self.plugin_so = plugin_so
        self.display = display
        self.ws_port = ws_port
        self.process: subprocess.Popen | None = None
        self.ws: ObsWs | None = None
        self.source_index = 0
        self.source_name = SOURCE_NAME
        self.undestroyed: list[str] = []

    @property
    def config_dir(self) -> Path:
        return self.home / ".config" / "obs-studio"

    @property
    def documents(self) -> Path:
        return self.home / "Documents"

    @property
    def registry_dir(self) -> Path:
        return self.documents / "obs-studio" / "c64stream" / "settings"

    def prepare(self) -> None:
        config = self.config_dir
        (config / "basic" / "profiles" / "HIL").mkdir(parents=True, exist_ok=True)
        (config / "basic" / "scenes").mkdir(parents=True, exist_ok=True)
        self.documents.mkdir(parents=True, exist_ok=True)
        general = ("[General]\nEnableAutoUpdates=false\nFirstRun=false\nLastVersion=999999999\n"
                   "ConfirmOnExit=false\nWarnBeforeStoppingRecord=false\n"
                   "[BasicWindow]\nSysTrayEnabled=false\n")
        for name in ("global.ini", "user.ini"):
            (config / name).write_text(general + "[Basic]\nProfile=HIL\nProfileDir=HIL\n"
                                       f"SceneCollection={SCENE_NAME}\nSceneCollectionFile={SCENE_NAME}\n")
        # The canvas is exactly one PAL C64 frame, so the source fills the
        # program output and the preview, and recordings need no cropping.
        (config / "basic" / "profiles" / "HIL" / "basic.ini").write_text(
            "[General]\nName=HIL\n[Video]\nBaseCX=384\nBaseCY=272\nOutputCX=384\nOutputCY=272\n"
            "FPSType=1\nFPSInt=60\nFPSCommon=60\n"
            "[Audio]\nSampleRate=48000\nChannelSetup=Stereo\n"
            "[Output]\nMode=Simple\n"
            f"[SimpleOutput]\nFilePath={self.workdir}\nRecFormat2=mkv\nRecQuality=HQ\nRecEncoder=x264\n"
            "FilenameFormatting=obs-%CCYY%MM%DD-%hh%mm%ss\n")
        (config / "basic" / "scenes" / f"{SCENE_NAME}.json").write_text(json.dumps({
            "name": SCENE_NAME, "current_scene": SCENE_NAME, "current_program_scene": SCENE_NAME,
            "scene_order": [{"name": SCENE_NAME}],
            "sources": [{"id": "scene", "versioned_id": "scene", "name": SCENE_NAME, "settings": {"items": []}}],
        }))
        ws_dir = config / "plugin_config" / "obs-websocket"
        ws_dir.mkdir(parents=True, exist_ok=True)
        (ws_dir / "config.json").write_text(json.dumps({
            "alerts_enabled": False, "auth_required": False, "first_load": False,
            "server_enabled": True, "server_password": "", "server_port": self.ws_port}))
        plugin_root = config / "plugins" / "c64stream"
        (plugin_root / "bin" / "64bit").mkdir(parents=True, exist_ok=True)
        shutil.copy2(self.plugin_so, plugin_root / "bin" / "64bit" / "c64stream.so")
        data_dst = plugin_root / "data"
        if data_dst.exists():
            shutil.rmtree(data_dst)
        shutil.copytree(REPO_ROOT / "data", data_dst)

    def start(self) -> None:
        env = dict(os.environ)
        env.update({"HOME": str(self.home), "XDG_CONFIG_HOME": str(self.home / ".config"),
                    "XDG_DOCUMENTS_DIR": str(self.documents), "DISPLAY": self.display,
                    "QT_QPA_PLATFORM": "xcb", "LIBGL_ALWAYS_SOFTWARE": "1", "QT_NO_XDG_DESKTOP_PORTAL": "1"})
        env.pop("WAYLAND_DISPLAY", None)
        stdout = open(self.workdir / "obs_stdout.log", "ab")
        self.process = subprocess.Popen(
            ["obs", "--multi", "--verbose", "--disable-updater", "--disable-missing-files-check",
             "--profile", "HIL", "--collection", SCENE_NAME],
            env=env, stdout=stdout, stderr=subprocess.STDOUT, cwd=self.workdir, start_new_session=True)
        deadline = time.time() + 60
        while time.time() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"OBS exited early ({self.process.returncode}); see {self.workdir}")
            with contextlib.suppress(OSError):
                with socket.create_connection(("127.0.0.1", self.ws_port), timeout=1):
                    break
            time.sleep(0.5)
        else:
            raise RuntimeError("obs-websocket did not come up")
        time.sleep(1.0)
        self.ws = ObsWs(self.ws_port)

    def stop(self) -> None:
        if self.ws:
            self.ws.close()
            self.ws = None
        if self.process and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait()
        self.process = None

    def log_text(self) -> str:
        logs = sorted((self.config_dir / "logs").glob("*.txt"), key=lambda p: p.stat().st_mtime)
        return logs[-1].read_text(errors="replace") if logs else ""

    # Source helpers -------------------------------------------------------

    def create_source(self, settings: dict) -> None:
        # A removed source can linger while a discovery scan still holds a
        # reference to it, so every scenario gets a fresh input name.
        self.source_index += 1
        self.source_name = f"{SOURCE_NAME} {self.source_index}"
        self.ws.call("CreateInput", {"sceneName": SCENE_NAME, "inputName": self.source_name, "inputKind": "c64_source",
                                     "inputSettings": settings, "sceneItemEnabled": True})

    def remove_source(self, timeout: float = 30.0) -> bool:
        """Removes the current source and waits until OBS has destroyed it. A
        source that is never destroyed keeps streaming and holds its UDP ports
        (and a hung destroy blocks every later destroy), so this is checked."""
        with contextlib.suppress(RuntimeError):
            self.ws.call("RemoveInput", {"inputName": self.source_name})
        marker = f"source '{self.source_name}' destroyed"
        deadline = time.time() + timeout
        while time.time() < deadline:
            if marker in self.log_text():
                return True
            time.sleep(0.5)
        self.undestroyed.append(self.source_name)
        return False

    def settings(self) -> dict:
        return self.ws.call("GetInputSettings", {"inputName": self.source_name})["inputSettings"]

    def update(self, settings: dict) -> None:
        self.ws.call("SetInputSettings", {"inputName": self.source_name, "inputSettings": settings, "overlay": True})

    def border_colour(self) -> int | None:
        """VIC colour index of the rendered border, or None if no C64 frame."""
        data = self.ws.call("GetSourceScreenshot", {"sourceName": self.source_name, "imageFormat": "png",
                                                     "imageWidth": 384})["imageData"]
        image = Image.open(io.BytesIO(base64.b64decode(data.split(",", 1)[1]))).convert("RGB")
        width, height = image.size
        samples = [image.getpixel((x, y)) for x, y in ((4, 4), (width - 5, 4), (4, height - 5),
                                                        (width - 5, height - 5), (width // 2, 6))]
        colours = {classify_colour(rgb) for rgb in samples}
        if len(colours) != 1:
            return None
        colour = colours.pop()
        # Pure black is what the source shows before any frame has arrived.
        return None if colour == 0 and samples[0] == (0, 0, 0) else colour


# --------------------------------------------------------------------------
# Test context


class Hil:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.workdir = Path(args.workdir or tempfile.mkdtemp(prefix="c64stream-hil-"))
        self.devices = [Device("C64U", args.c64u, 5), Device("U64", args.u64, 2)]
        self.netctl = NetCtl()
        self.xvfb: subprocess.Popen | None = None
        self.obs: Obs | None = None
        self.results: list[tuple[str, bool, str]] = []

    def device(self, label: str) -> Device:
        return next(d for d in self.devices if d.label == label)

    def __enter__(self) -> "Hil":
        try:
            return self._enter()
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def _enter(self) -> "Hil":
        log(f"workdir {self.workdir}")
        for device in self.devices:
            device.load_info()
            device.fingerprint()
            log(f"{device.label}: {device.info.get('product')} id={device.device_id} host={device.host} "
                f"border->{device.border}")
        self.netctl.setup()
        display = f":{self.args.display}"
        self.xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "1600x1000x24", "-nolisten", "tcp"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.0)
        self.obs = Obs(self.workdir, Path(self.args.plugin), display, self.args.ws_port)
        self.obs.prepare()
        return self

    def __exit__(self, *exc) -> None:
        if self.obs:
            self.obs.stop()
        if self.xvfb:
            self.xvfb.terminate()
        self.netctl.unblock_all()
        self.netctl.teardown()
        for device in self.devices:
            device.restore()

    def seed_registry(self) -> None:
        """Registers both devices the way a completed discovery would, so the
        switch scenarios do not depend on scan timing."""
        self.obs.registry_dir.mkdir(parents=True, exist_ok=True)
        for device in self.devices:
            (self.obs.registry_dir / f"device-{device.device_id}.ini").write_text(
                f"id={device.device_id}\nname={device.label} ({device.host})\nhost={device.host}\npeer_host=\n"
                f"dns_server_ip=\nvideo_port={VIDEO_PORT}\naudio_port={VIDEO_PORT + 1}\ncontrol_port=64\n")

    def prove_live(self, device: Device, timeout: float = 45) -> tuple[bool, float, str]:
        """A frozen last frame looks identical to live video, so change the
        device's border to a colour it has not shown yet and require that
        change to be rendered."""
        start = time.time()
        while True:
            try:
                device.set_border(7)
                break
            except Exception:
                if time.time() - start > timeout:
                    return False, timeout, "device REST unreachable"
                time.sleep(0.5)
        ok, seconds, detail = self.wait_for_border(7, max(1.0, timeout - (time.time() - start)))
        device.set_border(device.border)
        self.wait_for_border(device.border, 10)
        return ok, time.time() - start, detail

    def restart_obs(self, clear_registry: bool = False) -> None:
        self.obs.stop()
        if clear_registry and self.obs.registry_dir.exists():
            shutil.rmtree(self.obs.registry_dir)
        self.obs.start()

    def record(self, name: str, ok: bool, detail: str) -> None:
        self.results.append((name, ok, detail))
        log(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    def wait_for_border(self, expected: int, timeout: float, forbid: int | None = None) -> tuple[bool, float, str]:
        """Waits until the source renders the expected device. A frame from the
        forbidden device after the expected one appeared is a failure."""
        start = time.time()
        seen_at = None
        history: list[int | None] = []
        while time.time() - start < timeout:
            colour = self.obs.border_colour()
            history.append(colour)
            if colour == expected and seen_at is None:
                seen_at = time.time() - start
                # Keep sampling briefly to catch a stale frame from the old device.
                timeout = min(timeout, seen_at + 2.0)
            elif seen_at is not None and forbid is not None and colour == forbid:
                return False, seen_at, f"stale frame from previous device after switch ({history[-12:]})"
            time.sleep(0.1)
        if seen_at is None:
            return False, timeout, f"expected border {expected} never rendered (last {history[-8:]})"
        return True, seen_at, f"video after {seen_at:.2f}s"

    def registry(self) -> dict[str, dict]:
        entries = {}
        for path in sorted(self.obs.registry_dir.glob("device-*.ini")):
            values = dict(line.split("=", 1) for line in path.read_text().splitlines() if "=" in line)
            entries[values.get("id", path.stem)] = values
        return entries


# --------------------------------------------------------------------------
# Scenarios


# UDP ports the test OBS receives on; --video-port moves them (audio is the
# next port) when another OBS on this machine already holds 11000/11001.
VIDEO_PORT = 11000


def base_settings(**overrides) -> dict:
    settings = {"debug_logging": True, "stream_control_transport": 0, "video_port": VIDEO_PORT,
                "audio_port": VIDEO_PORT + 1}
    settings.update(overrides)
    return settings


def scenario_fresh_discovery(hil: Hil) -> None:
    """Empty registry, default host: the startup scan registers both devices
    and the source starts streaming without any manual configuration."""
    hil.restart_obs(clear_registry=True)
    c64u = hil.device("C64U")
    hil.obs.create_source(base_settings())
    ok, seconds, detail = hil.wait_for_border(c64u.border, 40)
    hil.record("fresh_discovery_streams_default_host", ok, detail)
    ids = {d.device_id for d in hil.devices}
    deadline = time.time() + 30
    while time.time() < deadline:
        registry = hil.registry()
        missing = ids - set(registry)
        if not missing:
            break
        time.sleep(1)
    hil.record("fresh_discovery_registers_all_devices", not missing,
               f"registry={ {k: (v.get('host'), v.get('peer_host')) for k, v in registry.items()} }")
    hil.obs.remove_source()


def scenario_switch_soak(hil: Hil) -> None:
    """Alternates between the two physical devices via the Device dropdown
    setting and checks every switch renders the target and never the old one."""
    c64u, u64 = hil.device("C64U"), hil.device("U64")
    hil.obs.create_source(base_settings(c64_device=c64u.device_id))
    ok, _, detail = hil.wait_for_border(c64u.border, 30)
    hil.record("switch_soak_initial", ok, detail)
    if not ok:
        hil.obs.remove_source()
        return
    timings, failures = [], []
    current, other = c64u, u64
    for index in range(hil.args.switches):
        current, other = other, current
        hil.obs.update({"c64_device": current.device_id})
        ok, seconds, detail = hil.wait_for_border(current.border, 15, forbid=other.border)
        timings.append(seconds)
        if not ok:
            failures.append(f"#{index + 1}->{current.label}: {detail}")
        time.sleep(hil.args.dwell)
    worst = max(timings) if timings else 0
    hil.record("switch_soak", not failures,
               f"{len(timings)} switches, worst {worst:.2f}s, mean {sum(timings) / max(1, len(timings)):.2f}s"
               + (f"; failures: {failures[:5]}" if failures else ""))
    # The device switched away from must have stopped streaming to us.
    time.sleep(2)
    stray = hil.netctl.count_udp_from(other.host, 3)
    hil.record("switch_soak_old_device_stopped", stray < 20, f"{stray} UDP packets from {other.label} in 3s")
    hil.obs.remove_source()


def scenario_rapid_switch(hil: Hil) -> None:
    """Switches faster than a start can complete; the last choice must win and
    the other device must end up stopped."""
    c64u, u64 = hil.device("C64U"), hil.device("U64")
    hil.obs.create_source(base_settings(c64_device=c64u.device_id))
    hil.wait_for_border(c64u.border, 30)
    sequence = [u64, c64u] * 6 + [u64]
    for device in sequence:
        hil.obs.update({"c64_device": device.device_id})
        time.sleep(0.25)
    ok, _, detail = hil.wait_for_border(u64.border, 20, forbid=c64u.border)
    hil.record("rapid_switch_last_wins", ok, detail)
    time.sleep(3)
    stray = hil.netctl.count_udp_from(c64u.host, 3)
    hil.record("rapid_switch_old_device_stopped", stray < 20, f"{stray} UDP packets from C64U in 3s")
    hil.obs.remove_source()


def scenario_wifi_host(hil: Hil) -> None:
    """The user enters the C64U's Wi-Fi address. A/V always leaves the wired
    port, so the plugin must still show video."""
    c64u = hil.device("C64U")
    wifi = hil.args.c64u_wifi
    if not wifi:
        hil.record("wifi_host", True, "skipped (no --c64u-wifi)")
        return
    hil.obs.create_source(base_settings(c64_device="", c64_host=wifi, c64_device_peer_host=""))
    ok, _, detail = hil.wait_for_border(c64u.border, 30)
    hil.record("wifi_host_streams", ok, detail)
    hil.obs.remove_source()


def scenario_wifi_learn(hil: Hil) -> None:
    """Discovery cannot see the wired address (its control port is blocked),
    so the source is left on the Wi-Fi address with no known peer. The plugin
    must recognise the wired sender as the same device and show its video."""
    c64u = hil.device("C64U")
    wifi, wired = hil.args.c64u_wifi, hil.args.c64u_wired
    if not wifi or not wired:
        hil.record("wifi_learn", True, "skipped (needs --c64u-wifi and --c64u-wired)")
        return
    hil.netctl.block_tcp_port(wired, 64)
    try:
        hil.obs.create_source(base_settings(c64_device="", c64_host=wifi, c64_device_peer_host=""))
        ok, _, detail = hil.wait_for_border(c64u.border, 30)
        adopted = "sends A/V from its other address" in hil.obs.log_text()
        hil.record("wifi_learn_streams", ok and adopted, f"{detail}; adoption logged={adopted}")
    finally:
        hil.netctl.unblock_all()
        hil.obs.remove_source()


def scenario_outage_recovery(hil: Hil) -> None:
    """The streaming device drops off the network and comes back."""
    c64u = hil.device("C64U")
    hil.obs.create_source(base_settings(c64_device=c64u.device_id))
    ok, _, detail = hil.wait_for_border(c64u.border, 30)
    if not ok:
        hil.record("outage_recovery", False, f"no initial video: {detail}")
        hil.obs.remove_source()
        return
    for seconds in hil.args.outages:
        hosts = [c64u.host] + ([hil.args.c64u_wifi] if hil.args.c64u_wifi else [])
        hil.netctl.block(*hosts)
        time.sleep(seconds)
        hil.netctl.unblock_all()
        ok, recovery, detail = hil.prove_live(c64u)
        hil.record(f"outage_recovery_{seconds}s", ok, f"live video {recovery:.2f}s after network returned ({detail})")
    hil.obs.remove_source()


def scenario_switch_to_offline(hil: Hil) -> None:
    """Switch to a device that is unreachable, then it appears; then switch back."""
    c64u, u64 = hil.device("C64U"), hil.device("U64")
    hil.obs.create_source(base_settings(c64_device=c64u.device_id))
    hil.wait_for_border(c64u.border, 30)
    hil.netctl.block(u64.host)
    hil.obs.update({"c64_device": u64.device_id})
    time.sleep(8)
    colour = hil.obs.border_colour()
    hil.record("switch_to_offline_hides_old_device", colour != c64u.border, f"border while target offline={colour}")
    stray = hil.netctl.count_udp_from(c64u.host, 3)
    hil.record("switch_to_offline_old_device_stopped", stray < 20, f"{stray} UDP packets from C64U in 3s")
    hil.netctl.unblock_all()
    ok, seconds, detail = hil.prove_live(u64)
    hil.record("switch_to_offline_target_appears", ok, f"live after {seconds:.2f}s ({detail})")
    hil.obs.update({"c64_device": c64u.device_id})
    ok, _, detail = hil.wait_for_border(c64u.border, 15, forbid=u64.border)
    hil.record("switch_to_offline_switch_back", ok, detail)
    hil.obs.remove_source()


def scenario_restart_keeps_selection(hil: Hil) -> None:
    """OBS restarts while the selected device is offline and the other one is
    online: the source must keep the user's choice, not hop to the other."""
    c64u, u64 = hil.device("C64U"), hil.device("U64")
    hil.obs.create_source(base_settings(c64_device=u64.device_id))
    hil.wait_for_border(u64.border, 30)
    hil.netctl.block(u64.host, *([hil.args.u64_wifi] if hil.args.u64_wifi else []))
    hil.restart_obs()
    time.sleep(25)  # startup scan + retry
    selected = hil.obs.settings().get("c64_device")
    colour = hil.obs.border_colour()
    hil.record("restart_keeps_offline_selection", selected == u64.device_id and colour != c64u.border,
               f"selected={selected} border={colour}")
    hil.netctl.unblock_all()
    ok, seconds, detail = hil.prove_live(u64)
    hil.record("restart_offline_selection_recovers", ok, f"live after {seconds:.2f}s ({detail})")
    hil.obs.remove_source()


def scenario_device_powered_on_later(hil: Hil) -> None:
    """The source is created while its device is unreachable (OBS started
    before the C64 was switched on). It must connect once the device appears,
    without any user action."""
    c64u = hil.device("C64U")
    hosts = [c64u.host] + [h for h in (hil.args.c64u_wifi, hil.args.c64u_wired) if h]
    hil.netctl.block(*hosts)
    try:
        hil.obs.create_source(base_settings(c64_device=c64u.device_id))
        time.sleep(hil.args.offline_seconds)
    finally:
        hil.netctl.unblock_all()
    ok, seconds, detail = hil.prove_live(c64u)
    hil.record("device_powered_on_later_connects", ok, f"live {seconds:.2f}s after it became reachable ({detail})")
    hil.obs.remove_source()


def scenario_power_cycle(hil: Hil) -> None:
    """The streaming device loses mains power and reboots (it then streams
    nothing until told to). Uses a smart outlet through powerctl; the outlet
    is named by MAC so no other outlet can be switched by mistake."""
    if not hil.args.c64u_outlet_mac:
        hil.record("power_cycle", True, "skipped (no --c64u-outlet-mac)")
        return
    c64u = hil.device("C64U")
    hil.obs.create_source(base_settings(c64_device=c64u.device_id))
    ok, _, detail = hil.wait_for_border(c64u.border, 30)
    if not ok:
        hil.record("power_cycle", False, f"no initial video: {detail}")
        hil.obs.remove_source()
        return
    for index in range(hil.args.power_cycles):
        c64u.graceful_power_off()
        result = subprocess.run(
            ["powerctl", "cycle", hil.args.c64u_outlet_mac, "--yes", "--json", "--off-seconds", "8",
             "--wait-host", hil.args.c64u_wired or c64u.host, "--wait-port", "80", "--wait-timeout", "180"],
            capture_output=True, text=True, timeout=300)
        if result.returncode != 0:
            hil.record(f"power_cycle_{index + 1}", False, f"powerctl exit {result.returncode}: {result.stdout[-300:]}")
            if result.returncode == 5:
                subprocess.run(["powerctl", "on", hil.args.c64u_outlet_mac, "--json"], timeout=60)
            break
        ok, seconds, detail = hil.prove_live(c64u, timeout=60)
        hil.record(f"power_cycle_{index + 1}", ok, f"live {seconds:.2f}s after REST came back ({detail})")
    hil.obs.remove_source()


def scenario_lossy_switch(hil: Hil) -> None:
    """Switches between both devices while a share of all packets to and from
    them is lost. Every switch must still land on the right device."""
    c64u, u64 = hil.device("C64U"), hil.device("U64")
    hosts = [d.host for d in hil.devices] + [h for h in (hil.args.c64u_wifi, hil.args.c64u_wired, hil.args.u64_wifi) if h]
    hil.obs.create_source(base_settings(c64_device=c64u.device_id))
    hil.wait_for_border(c64u.border, 30)
    for probability in hil.args.loss:
        hil.netctl.loss(probability, *hosts)
        timings, failures = [], []
        current, other = c64u, u64
        for index in range(hil.args.loss_switches):
            current, other = other, current
            hil.obs.update({"c64_device": current.device_id})
            ok, seconds, detail = hil.wait_for_border(current.border, 30, forbid=other.border)
            timings.append(seconds)
            if not ok:
                failures.append(f"#{index + 1}->{current.label}: {detail}")
        hil.netctl.unblock_all()
        hil.record(f"lossy_switch_{int(probability * 100)}pct", not failures,
                   f"{len(timings)} switches, worst {max(timings):.2f}s"
                   + (f"; failures: {failures[:3]}" if failures else ""))
        time.sleep(3)
    hil.obs.remove_source()


GAP_RE = None


def switch_gaps(log_text: str) -> list[tuple[float, float]]:
    """(first frame after request, longest pause without video) per switch, ms."""
    import re
    global GAP_RE
    GAP_RE = GAP_RE or re.compile(r"Device switch from \S*: first frame (\d+) ms after the request; "
                                  r"longest pause without video (\d+) ms")
    return [(float(a), float(b)) for a, b in GAP_RE.findall(log_text)]


def _switch_and_check(hil: Hil, target: Device, previous: Device, timeout: float, label: str,
                      failures: list[str], latencies: list[float]) -> None:
    hil.obs.update({"c64_device": target.device_id})
    ok, seconds, detail = hil.wait_for_border(target.border, timeout, forbid=previous.border)
    latencies.append(seconds)
    if not ok:
        failures.append(f"{label}->{target.label}: {detail}")


def scenario_soak(hil: Hil) -> None:
    """Back-and-forth stress and soak: live<->live with random dwell, rapid
    fire, one device unreachable, and one device powered off since discovery.
    The pause a viewer sees on a switch between live devices must stay below
    one second on every switch."""
    import random
    rng = random.Random(hil.args.seed)
    c64u, u64 = hil.device("C64U"), hil.device("U64")
    limit_ms = hil.args.gap_limit_ms
    hil.obs.create_source(base_settings(c64_device=c64u.device_id))
    ok, _, detail = hil.wait_for_border(c64u.border, 30)
    if not ok:
        hil.record("soak", False, f"no initial video: {detail}")
        return

    # 1. live <-> live, random dwell
    log_before = len(switch_gaps(hil.obs.log_text()))
    failures: list[str] = []
    latencies: list[float] = []
    current, other = c64u, u64
    for index in range(hil.args.soak_switches):
        current, other = other, current
        _switch_and_check(hil, current, other, 10, f"#{index + 1}", failures, latencies)
        time.sleep(rng.uniform(0.15, 2.5))
    gaps = switch_gaps(hil.obs.log_text())[log_before:]
    worst_gap = max((g for _, g in gaps), default=0.0)
    over = [g for _, g in gaps if g > limit_ms]
    hil.record("soak_live_switches", not failures and len(gaps) >= len(latencies) and not over,
               f"{len(latencies)} switches; pause without video worst {worst_gap:.0f} ms, "
               f"median {sorted(g for _, g in gaps)[len(gaps) // 2] if gaps else 0:.0f} ms; "
               f"new device on screen worst {max(latencies):.2f}s; over {limit_ms} ms: {len(over)}"
               + (f"; failures {failures[:3]}" if failures else ""))

    # 2. rapid fire: faster than a start completes; the last choice wins
    for index in range(hil.args.rapid_switches):
        current, other = other, current
        hil.obs.update({"c64_device": current.device_id})
        time.sleep(rng.uniform(0.03, 0.3))
    ok, seconds, detail = hil.wait_for_border(current.border, 5, forbid=other.border)
    time.sleep(2)
    stray = hil.netctl.count_udp_from(other.host, 3)
    hil.record("soak_rapid_fire", ok and stray < 20,
               f"{hil.args.rapid_switches} switches 30-300 ms apart; settled on {current.label} ({detail}); "
               f"{stray} packets from {other.label} afterwards")

    # 3. one device unreachable since discovery
    hosts = [u64.host] + ([hil.args.u64_wifi] if hil.args.u64_wifi else [])
    hil.obs.update({"c64_device": c64u.device_id})
    hil.wait_for_border(c64u.border, 10)
    hil.netctl.block(*hosts)
    failures, latencies, hidden = [], [], []
    try:
        for index in range(hil.args.offline_switches):
            hil.obs.update({"c64_device": u64.device_id})
            start = time.time()
            while time.time() - start < 3 and hil.obs.border_colour() == c64u.border:
                time.sleep(0.05)
            hidden.append(time.time() - start)
            time.sleep(rng.uniform(0.2, 1.5))
            _switch_and_check(hil, c64u, u64, 5, f"#{index + 1}", failures, latencies)
            time.sleep(rng.uniform(0.2, 1.5))
    finally:
        hil.netctl.unblock_all()
    hil.record("soak_offline_target", not failures and max(latencies) <= limit_ms / 1000 + 0.15 and max(hidden) < 3,
               f"{len(latencies)} round trips to an unreachable device; back on the live device worst "
               f"{max(latencies):.2f}s; previous picture cleared worst {max(hidden):.2f}s"
               + (f"; failures {failures[:3]}" if failures else ""))
    hil.obs.update({"c64_device": u64.device_id})
    ok, seconds, detail = hil.prove_live(u64)
    hil.record("soak_offline_target_returns", ok, f"unreachable device live {seconds:.2f}s after it returned")

    # 4. device powered off since discovery (smart outlet), then back on
    if hil.args.c64u_outlet_mac:
        hil.obs.update({"c64_device": u64.device_id})
        hil.wait_for_border(u64.border, 10)
        c64u.graceful_power_off()
        subprocess.run(["powerctl", "off", hil.args.c64u_outlet_mac, "--yes", "--json"], capture_output=True,
                       timeout=60, check=True)
        time.sleep(5)
        failures, latencies = [], []
        try:
            for index in range(hil.args.offline_switches):
                hil.obs.update({"c64_device": c64u.device_id})
                time.sleep(rng.uniform(0.3, 1.5))
                _switch_and_check(hil, u64, c64u, 5, f"#{index + 1}", failures, latencies)
                time.sleep(rng.uniform(0.2, 1.0))
        finally:
            subprocess.run(["powerctl", "on", hil.args.c64u_outlet_mac, "--json"], capture_output=True, timeout=60)
        hil.record("soak_powered_off_target", not failures and max(latencies) <= limit_ms / 1000 + 0.15,
                   f"{len(latencies)} round trips to a powered-off C64U; back on the U64 worst {max(latencies):.2f}s"
                   + (f"; failures {failures[:3]}" if failures else ""))
        deadline = time.time() + 120
        while time.time() < deadline:
            with contextlib.suppress(Exception):
                c64u.load_info()
                break
            time.sleep(1)
        hil.obs.update({"c64_device": c64u.device_id})
        ok, seconds, detail = hil.prove_live(c64u, timeout=60)
        hil.record("soak_powered_off_target_returns", ok, f"C64U live {seconds:.2f}s after it booted")
    hil.obs.remove_source()


# --------------------------------------------------------------------------
# Visual ground truth: what OBS outputs and what the screen shows


def _decode_frames(path: Path, width: int, height: int, fps: float):
    """Yields (time_s, numpy RGB frame) for a constant-frame-rate video."""
    import numpy as np
    proc = subprocess.Popen(["ffmpeg", "-v", "error", "-i", str(path), "-vf", f"fps={fps},scale={width}:{height}",
                             "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
    size = width * height * 3
    index = 0
    try:
        while True:
            raw = proc.stdout.read(size)
            if len(raw) != size:
                break
            yield index / fps, np.frombuffer(raw, dtype=np.uint8).reshape((height, width, 3))
            index += 1
    finally:
        proc.stdout.close()
        proc.wait(timeout=30)


def _nearest_device(rgb, devices: list[Device], tolerance: int = 70) -> str | None:
    best, best_distance = None, None
    for device in devices:
        reference = VIC_RGB[device.border]
        distance = max(abs(int(a) - int(b)) for a, b in zip(reference, rgb))
        if best_distance is None or distance < best_distance:
            best, best_distance = device.label, distance
    return best if best_distance is not None and best_distance <= tolerance else None


def analyse_program_recording(path: Path, devices: list[Device], fps: float = 60.0) -> dict:
    """Frame-by-frame analysis of OBS's own recording of the program output
    (the canvas is exactly the C64 frame). A frame belongs to a device when
    all border samples show its colour; it is live when the marker character
    changed since the previous frame. For every change of device the visible
    gap is the time from the previous device's last live frame to the new
    device's first live frame. A frame of the previous device shown after the
    new device's first frame is reported as stale."""
    import numpy as np
    width, height = 384, 272
    samples = [(4, 4), (379, 4), (4, 267), (379, 267), (192, 6), (6, 136)]
    marker = (slice(30, 50), slice(28, 48))  # first screen character, PAL
    previous = None
    current, last_live = None, {}
    transitions, stale, frames = [], [], 0
    first_live_of_current = None
    for t, frame in _decode_frames(path, width, height, fps):
        frames += 1
        labels = {_nearest_device(frame[y, x], devices) for x, y in samples}
        label = labels.pop() if len(labels) == 1 else None
        live = previous is not None and int(np.abs(frame[marker].astype(int) - previous[marker].astype(int)).max()) > 40
        previous = frame
        if label is None:
            continue
        if live:
            if current is not None and label != current:
                transitions.append({"from": current, "to": label, "at_s": round(t, 3),
                                    "gap_ms": round((t - last_live.get(current, t)) * 1000)})
                first_live_of_current = t
            elif current is None:
                first_live_of_current = t
            current = label
            last_live[label] = t
        elif current is not None and label != current and first_live_of_current is not None \
                and t - first_live_of_current < 0.5:
            stale.append({"device": label, "at_s": round(t, 3)})
    gaps = sorted(item["gap_ms"] for item in transitions)
    return {"frames": frames, "transitions": transitions, "stale_frames": stale,
            "worst_gap_ms": gaps[-1] if gaps else None, "median_gap_ms": gaps[len(gaps) // 2] if gaps else None}


def analyse_screen_grab(path: Path, devices: list[Device], fps: float = 30.0) -> dict:
    """Coarse analysis of the screen recording of the OBS window: which
    device colour dominates the preview in each frame, and how long the
    preview shows neither device when it changes."""
    import numpy as np
    width, height = 800, 500
    current, since_current = None, None
    changes, frames = [], 0
    for t, frame in _decode_frames(path, width, height, fps):
        frames += 1
        counts = {}
        for device in devices:
            reference = np.array(VIC_RGB[device.border], dtype=int)
            counts[device.label] = int((np.abs(frame.astype(int) - reference).max(axis=2) <= 50).sum())
        label, count = max(counts.items(), key=lambda item: item[1])
        label = label if count > width * height * 0.01 else None
        if label is None:
            continue
        if current is not None and label != current:
            changes.append({"from": current, "to": label, "at_s": round(t, 3),
                            "gap_ms": round((t - since_current) * 1000)})
        current, since_current = label, t
    gaps = sorted(item["gap_ms"] for item in changes)
    return {"frames": frames, "changes": changes, "worst_gap_ms": gaps[-1] if gaps else None,
            "median_gap_ms": gaps[len(gaps) // 2] if gaps else None}


def scenario_visual(hil: Hil) -> None:
    """Records what OBS outputs (its own recording of the program) and what
    the display shows (screen grab of the OBS window) while switching, and
    measures every switch from the video itself."""
    import random
    rng = random.Random(hil.args.seed)
    c64u, u64 = hil.device("C64U"), hil.device("U64")
    for device in hil.devices:
        device.run_marker_program()
    time.sleep(2)
    hil.obs.create_source(base_settings(c64_device=c64u.device_id))
    ok, _, detail = hil.wait_for_border(c64u.border, 30)
    if not ok:
        hil.record("visual", False, f"no initial video: {detail}")
        return
    screen_path = hil.workdir / "screen-grab.mp4"
    grab = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "x11grab", "-video_size", "1600x1000",
                             "-framerate", "30", "-i", f":{hil.args.display}", "-c:v", "libx264",
                             "-preset", "ultrafast", "-crf", "18", "-pix_fmt", "yuv420p", str(screen_path)],
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    hil.obs.ws.call("StartRecord")
    time.sleep(3)
    current, other = c64u, u64
    switches = hil.args.visual_switches
    for _ in range(switches):
        current, other = other, current
        hil.obs.update({"c64_device": current.device_id})
        time.sleep(rng.uniform(1.0, 2.0))
    for _ in range(10):  # rapid fire, settles on the last choice
        current, other = other, current
        hil.obs.update({"c64_device": current.device_id})
        time.sleep(rng.uniform(0.05, 0.2))
    time.sleep(3)
    output = hil.obs.ws.call("StopRecord").get("outputPath")
    grab.communicate(b"q", timeout=30)
    time.sleep(2)
    recording = Path(output) if output else max(hil.workdir.glob("obs-*.mkv"), key=lambda p: p.stat().st_mtime)
    program = analyse_program_recording(recording, hil.devices)
    screen = analyse_screen_grab(screen_path, hil.devices)
    (hil.workdir / "visual-analysis.json").write_text(json.dumps({"program": program, "screen": screen}, indent=2))
    limit = hil.args.gap_limit_ms
    slow = [item for item in program["transitions"] if item["gap_ms"] > limit]
    hil.record("visual_program_recording",
               len(program["transitions"]) >= switches and not slow and not program["stale_frames"],
               f"{len(program['transitions'])} device changes in {program['frames']} recorded frames; visible gap "
               f"worst {program['worst_gap_ms']} ms, median {program['median_gap_ms']} ms; over {limit:.0f} ms: "
               f"{len(slow)}; stale previous-device frames: {len(program['stale_frames'])} ({recording.name})")
    slow_screen = [item for item in screen["changes"] if item["gap_ms"] > limit]
    hil.record("visual_screen_grab", len(screen["changes"]) >= switches and not slow_screen,
               f"{len(screen['changes'])} changes in {screen['frames']} screen frames; preview gap worst "
               f"{screen['worst_gap_ms']} ms, median {screen['median_gap_ms']} ms; over {limit:.0f} ms: "
               f"{len(slow_screen)} ({screen_path.name})")
    hil.obs.remove_source()


def _vpl(light_blue: tuple[int, int, int]) -> str:
    """A full VPL whose colour 14 (the READY screen border) is light_blue."""
    colors = [(0, 0, 0), (255, 255, 255), (136, 57, 50), (103, 182, 189), (139, 63, 150), (85, 160, 73),
              (64, 49, 141), (191, 206, 114), (139, 84, 41), (87, 66, 0), (184, 105, 98), (80, 80, 80),
              (120, 120, 120), (148, 224, 137), (120, 105, 196), (159, 159, 159)]
    colors[14] = light_blue
    return "# c64stream HIL test palette\n" + "".join("%02X %02X %02X\n" % c for c in colors)


def scenario_palette_follow(hil: Hil) -> None:
    """Follow device without stream palette packets:
    the plugin reads the device's Palette Definition, downloads the VPL over
    FTP and applies it. The device border stays colour 14; the test palettes
    map colour 14 to red (A) or green (B), so the rendered border shows which
    palette OBS applied, and when."""
    u64, c64u = hil.device("U64"), hil.device("C64U")
    names = ("c64stream-test-a.vpl", "c64stream-test-b.vpl")
    original = {d.label: d.palette_setting() for d in (u64, c64u)}
    had_data_dir = {d.label: d.has_flash_data_dir() for d in (u64, c64u)}
    for device in (u64, c64u):
        device.set_border(14)
        device.upload_palette(names[0], _vpl((255, 0, 0)))
        device.upload_palette(names[1], _vpl((0, 255, 0)))
        device.set_palette_setting("")
    mark = [0]

    def step() -> float:
        mark[0] = len(hil.obs.log_text())
        return time.time()

    def log_seen(text: str) -> bool:
        """The plugin logged text since the current step began."""
        return text in hil.obs.log_text()[mark[0]:]

    try:
        step()
        hil.obs.create_source(base_settings(c64_device=u64.device_id, palette="__device__",
                                            stream_control_transport=0))
        ok, _, detail = hil.wait_for_border(14, 30)
        hil.record("palette_follow_builtin", ok and log_seen("built-in palette"),
                   f"device setting empty: built-in palette ({detail})")

        for name, colour, label in ((names[0], 2, "red"), (names[1], 5, "green")):
            start = step()
            u64.set_palette_setting(name)
            ok, seconds, detail = hil.wait_for_border(colour, 10)
            hil.record(f"palette_follow_{label}", ok and log_seen(f'file "{name}"'),
                       f"{name} applied {time.time() - start:.2f}s after the device setting changed ({detail})")

        # Same name, new content: picked up by the periodic recheck.
        start = step()
        u64.upload_palette(names[1], _vpl((0, 0, 0)))
        ok, _, detail = hil.wait_for_border(0, 20)
        hil.record("palette_follow_file_overwritten", ok,
                   f"overwritten {names[1]} applied after {time.time() - start:.2f}s ({detail})")

        # Legacy stream control cannot request palette information.
        step()
        hil.obs.update({"stream_control_transport": 2})
        ok, _, detail = hil.wait_for_border(14, 15)
        hil.record("palette_follow_legacy_uses_default", ok and log_seen("Follow device reads the palette over REST"),
                   f"legacy transport shows the default palette ({detail})")
        hil.obs.update({"stream_control_transport": 0})
        ok, _, detail = hil.wait_for_border(0, 15)
        hil.record("palette_follow_rest_again", ok, f"back on REST, device palette again ({detail})")

        # Device switch: each device's own palette, never the other's.
        u64.set_palette_setting(names[0])
        c64u.set_palette_setting(names[1])
        hil.wait_for_border(2, 10)
        failures = []
        for target, colour in ((c64u, 5), (u64, 2), (c64u, 5), (u64, 2)):
            hil.obs.update({"c64_device": target.device_id})
            ok, seconds, detail = hil.wait_for_border(colour, 10, forbid=2 if colour == 5 else 5)
            if not ok:
                failures.append(f"{target.label}: {detail}")
        hil.record("palette_follow_device_switch", not failures,
                   "each device shown with its own palette after every switch" if not failures else str(failures))
    finally:
        hil.obs.remove_source()
        for device in (u64, c64u):
            with contextlib.suppress(Exception):
                device.set_palette_setting(original[device.label])
            for name in names:
                device.delete_palette(name)
            if not had_data_dir[device.label]:
                device.remove_flash_data_dir()
            with contextlib.suppress(Exception):
                device.set_border(device.border)


SCENARIOS = {
    "palette": scenario_palette_follow,
    "visual": scenario_visual,
    "soak": scenario_soak,
    "lossy": scenario_lossy_switch,
    "power": scenario_power_cycle,
    "late": scenario_device_powered_on_later,
    "fresh": scenario_fresh_discovery,
    "switch": scenario_switch_soak,
    "rapid": scenario_rapid_switch,
    "wifi": scenario_wifi_host,
    "wifi_learn": scenario_wifi_learn,
    "outage": scenario_outage_recovery,
    "offline_switch": scenario_switch_to_offline,
    "restart_selection": scenario_restart_keeps_selection,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenarios", nargs="*", default=list(SCENARIOS), choices=[*SCENARIOS, []])
    parser.add_argument("--plugin", default=str(REPO_ROOT / "build_x86_64" / "c64stream.so"))
    parser.add_argument("--c64u", default="c64u")
    parser.add_argument("--c64u-wifi", default="")
    parser.add_argument("--c64u-wired", default="")
    parser.add_argument("--u64", default="u64")
    parser.add_argument("--u64-wifi", default="")
    parser.add_argument("--switches", type=int, default=20)
    parser.add_argument("--dwell", type=float, default=1.0)
    parser.add_argument("--outages", type=float, nargs="*", default=[3, 15, 40])
    parser.add_argument("--c64u-outlet-mac", default="")
    parser.add_argument("--power-cycles", type=int, default=2)
    parser.add_argument("--loss", type=float, nargs="*", default=[0.01, 0.03])
    parser.add_argument("--loss-switches", type=int, default=8)
    parser.add_argument("--soak-switches", type=int, default=200)
    parser.add_argument("--visual-switches", type=int, default=40)
    parser.add_argument("--rapid-switches", type=int, default=60)
    parser.add_argument("--offline-switches", type=int, default=15)
    parser.add_argument("--gap-limit-ms", type=float, default=1000)
    parser.add_argument("--seed", type=int, default=64)
    parser.add_argument("--offline-seconds", type=float, default=20)
    parser.add_argument("--display", type=int, default=97)
    parser.add_argument("--ws-port", type=int, default=4466)
    parser.add_argument("--video-port", type=int, default=11000)
    parser.add_argument("--workdir")
    args = parser.parse_args()
    global VIDEO_PORT
    VIDEO_PORT = args.video_port

    with Hil(args) as hil:
        hil.seed_registry()
        hil.obs.start()
        for name in args.scenarios:
            log(f"=== scenario {name}")
            try:
                SCENARIOS[name](hil)
            except Exception as error:  # keep going; record the failure
                hil.record(name, False, f"exception: {error!r}")
                with contextlib.suppress(Exception):
                    hil.obs.remove_source()
        hil.record("removed_sources_destroyed", not hil.obs.undestroyed,
                   f"never destroyed: {hil.obs.undestroyed}" if hil.obs.undestroyed else "every removed source destroyed")
        (hil.workdir / "obs_log_final.txt").write_text(hil.obs.log_text())
        failed = [r for r in hil.results if not r[1]]
        log(f"=== {len(hil.results) - len(failed)}/{len(hil.results)} checks passed; logs in {hil.workdir}")
        for name, ok, detail in hil.results:
            print(f"  {'PASS' if ok else 'FAIL'}  {name}: {detail}")
        return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
