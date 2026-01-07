# Real-device A/V sync test (C64 Ultimate)

This describes how to run the real-device A/V sync “pop” test using OBS + the `c64stream` plugin against a physical C64 Ultimate device.

This is **LOCAL ONLY** (requires a working graphical environment and a reachable device). Do not run this in CI/cloud.

Related docs:

- Results ledger: `doc/real-device-av-sync-results.md`
- E2E overview (mock stream scenarios): `doc/e2e.md`

## What the test does

- Uploads and starts `tools/c64/av-sync-auto.prg` on the C64 Ultimate via REST.
- Runs OBS with a dedicated profile/scene collection and records for `--duration` seconds.
- Collects artifacts (recording, `obs.csv`, `network.csv`, OBS log).
- Computes A/V delta stats from detected “pops” (full-white video frame + audio tone).

The authoritative signal source is `obs.csv` when present.

## Prerequisites (all platforms)

- A reachable C64 Ultimate device:
  - REST API reachable at `http://<host>/v1/...` (see `--host`, `--rest-scheme`, endpoints).
  - UDP stream reachable from the machine running OBS (default ports `21000` video, `21001` audio).
- OBS installed and runnable from the command line as `obs`.
- The `c64stream` plugin built and installed into OBS.
- Python 3 available as `python3`.

## Linux (native)

### 1) Build and install the plugin

From the repo root:

```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64

# Install into the user OBS config (same behavior as the VS Code task)
mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit"
mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/data"
cp build_x86_64/c64stream.so "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit/"
cp -r data/* "$HOME/.config/obs-studio/plugins/c64stream/data/"
```

### 2) Install Python deps (recommended)

The analyzer and some helper scripts use Python packages:

```bash
python3 -m pip install -r tests/e2e/requirements.txt
```

### 3) Run the test

Basic run (defaults to `--host c64u` and `--duration 10`):

```bash
./tests/e2e/real-device-av-sync.sh --format PAL --duration 10 --verbose
```

Common variants:

```bash
# Use explicit device IP/hostname
./tests/e2e/real-device-av-sync.sh --host 192.168.1.13 --format PAL --duration 60 --verbose

# Override ports if needed
./tests/e2e/real-device-av-sync.sh --host c64u --video-port 21000 --audio-port 21001 --format PAL

# Tighten/loosen acceptance thresholds
./tests/e2e/real-device-av-sync.sh --format PAL --duration 600 \
  --p50-max-ms 20 --p95-max-ms 40 --max-max-ms 60
```

## Windows

### Supported way: Windows + WSL2 (recommended)

The real-device runner (`tests/e2e/real-device-av-sync.sh`) is a Bash + Linux-OBS oriented flow. The most reliable way to run it on Windows is inside **WSL2** with a Linux userland.

1) Install WSL2 + Ubuntu.
   - On Windows 11, WSLg usually provides GUI support out of the box.
   - On Windows 10, you may need an X server (e.g. VcXsrv) to run Linux GUI apps.

2) Inside WSL, install the dependencies:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build pkg-config \
  python3 python3-pip \
  curl \
  obs-studio xvfb

python3 -m pip install -r tests/e2e/requirements.txt
```

3) Build + install the plugin inside WSL (Linux build), then run the same command as on Linux:

```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64

mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit"
mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/data"
cp build_x86_64/c64stream.so "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit/"
cp -r data/* "$HOME/.config/obs-studio/plugins/c64stream/data/"

./tests/e2e/real-device-av-sync.sh --format PAL --duration 60 --verbose
```

Notes:

- If `c64u` does not resolve inside WSL, use `--host <ip>`.
- The runner always attempts to reset the device at the end of a full run.

### Native Windows (PowerShell)

This can be run natively on Windows with a few setup steps and small path shims. The runner is
`tests/e2e/real_device_av_sync.py` (Python), so you do not need Bash.

#### 1) Install required software

Use `winget` where possible (PowerShell):

```powershell
winget install --id OBSProject.OBSStudio -e
winget install --id Kitware.CMake -e
winget install --id Python.Python.3 -e
winget install --id 64tass.64tass -e
```

Notes:
- If you already have Python, make sure it is on PATH (`python --version`).
- If you prefer to avoid building the PRG, you can use the existing `tools/c64/av-sync-auto.prg`
  from the repo (skip 64tass).

#### 2) Install the c64stream plugin (Windows)

Option A (recommended): install from a release ZIP and copy into OBS:

```
%APPDATA%\obs-studio\plugins\c64stream\bin\64bit\c64stream.dll
%APPDATA%\obs-studio\plugins\c64stream\data\*
```

Option B (build locally): follow the Windows build steps in `doc/developer.md`, then copy the
resulting DLL into the same plugin paths above.

#### 3) Create an OBS config junction for the runner

The Python runner expects Linux-style config paths at `~/.config/obs-studio`. On Windows, create
a junction so those paths map to your real OBS config folder:

```powershell
$obsConfig = "$env:APPDATA\obs-studio"
$shimConfig = "$env:USERPROFILE\.config\obs-studio"
if (!(Test-Path "$env:USERPROFILE\.config")) { New-Item -ItemType Directory "$env:USERPROFILE\.config" | Out-Null }
cmd /c mklink /J "$shimConfig" "$obsConfig"
```

#### 4) Add OBS to PATH (so `obs` works)

Create a small shim in a folder on PATH (for example, `C:\tools\bin\obs.cmd`):

```powershell
@"
""C:\Program Files\obs-studio\bin\64bit\obs64.exe"" %*
"@ | Set-Content -Encoding ASCII C:\tools\bin\obs.cmd

$env:Path += ";C:\tools\bin"
```

#### 5) Install Python dependencies

From the repo root:

```powershell
python -m pip install -r tests/e2e/requirements.txt
```

#### 6) Run the test (native Windows)

First start the PRG on the C64U (PowerShell uses `curl.exe`):

```powershell
$hostName = "c64u"
curl.exe -f -F "file=@tools\c64\av-sync-auto.prg" "http://$hostName/v1/runners:run_prg"
```

Then run the Python runner:

```powershell
$env:DISPLAY=":0"
python tests\e2e\real_device_av_sync.py --host c64u --duration 60 --verbose
```

Artifacts land in:

```
tests\e2e\results\real_c64u_av_sync\session_YYYYmmdd_HHMMSS\
```

Notes:
- If your REST endpoint or auth differs, use `curl.exe` with the correct URL/headers.
- If the device hostname does not resolve, use `--host <ip>`.

## Artifacts

Each run creates a session directory:

- `tests/e2e/results/real_c64u_av_sync/session_YYYYmmdd_HHMMSS/`

Typical contents:

- Recording: `*.mp4`
- `obs.csv` and `network.csv`
- `obs_log.txt`
- Analyzer output: `av_pop_report.json`

## Analyze existing artifacts only

To re-run analysis without touching the device or OBS:

```bash
# Directory created by the runner
./tests/e2e/real-device-av-sync.sh --analyze-only tests/e2e/results/real_c64u_av_sync/session_YYYYmmdd_HHMMSS

# Or point at specific files
./tests/e2e/real-device-av-sync.sh --obs-csv /path/to/obs.csv --network-csv /path/to/network.csv
```
