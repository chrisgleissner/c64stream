# A/V Sync E2E Test against Real Device

This document describes how to run the real-device A/V sync “pop” E2E test using OBS + the `c64stream` plugin against a physical C64 Ultimate device.

This is **LOCAL ONLY** (requires real hardware and a working GUI for OBS). Do not run this in CI/cloud.

## Purpose and overview

There are two complementary ways we check A/V synchronization in this project:

- **Mocked C64U (most E2E tests):** the harness generates deterministic UDP packets that emulate the device. These
  tests exercise the full OBS + plugin pipeline in a reproducible, CI-friendly way. See `doc/testing/e2e.md`.
- **Real C64U (this document):** run `av-sync-auto.prg` on a physical C64 Ultimate, record in OBS, and analyze the
  resulting CSVs/logs.

The A/V pop programs create deterministic markers that make A/V sync errors and long-term drift observable in logs and
CSVs:

- **Video pop:** a full-frame white flash (exactly one frame).
- **Audio pop:** a short, sharp low C3 pulse (~131 Hz).

When the C64 Stream source has **Debug** enabled, the plugin can detect these edges, log timing deltas, and annotate CSV
outputs for offline analysis.

Use these markers to:

- Compare Windows vs Linux behavior with the same test pattern
- Let end users verify their own setup
- Correlate `network.csv`, `obs.csv`, and OBS logs without manual timeline inspection

## C64 programs

### Manual mode: `tools/c64/av-sync.asm`

Behavior:

- Start: border black, background black, SID silent
- Hold SPACE: border white, background white, audio on
- Release SPACE: border black, background black, audio off

Audio:

- Pulse waveform, low C3 tone (~131 Hz), single SID voice
- ADSR: attack 0, decay 0, sustain max, release 0

### Automatic mode: `tools/c64/av-sync-auto.asm`

Behavior:

- Start: border black, background black, SID silent
- Every 48 frames: generate a one-frame A/V pop

Timing (current implementation):

- The automatic runner schedules a single raster IRQ at **raster line 0** once per frame.
- When the pop countdown reaches zero, the IRQ handler starts the pop on row 0 of the target frame.
- The pop is stopped by the IRQ at row 0 of the *following* frame, guaranteeing the intermediate frame is fully white.
- This avoids relying on end-of-frame/overscan behavior and yields consistent one-frame pops on PAL and NTSC.

Audio:

- Same C3 pulse tone as manual mode
- Exactly one frame long

### Build with the repo toolchain

```bash
cd tools/c64
./c64-build.sh av-sync.asm
./c64-build.sh av-sync-auto.asm
```

### Install on a C64 Ultimate (FTP or USB)

1. Copy the `.prg` files to the Ultimate via FTP or a USB drive.
2. Use the Ultimate’s file browser to select the PRG.
3. Run it from the file browser (or from BASIC with `SYS` if preferred).

## Prerequisites

- A reachable C64 Ultimate device:
  - REST API reachable at `http(s)://<host>/v1/...` (PRG start + optional reset)
  - UDP stream reachable from the machine running OBS (default ports `21000` video, `21001` audio)
  - Control socket reachable (TCP port `64`) if the runner needs it
- OBS installed and runnable from the command line as `obs`
- The `c64stream` plugin built and installed into OBS
- Python 3 available as `python3`
- OBS source **Debug** enabled (required for pop detection + CSV columns)

## OBS configuration

1. Add or select the **C64 Stream** source in OBS.
2. Open the source properties.
3. Enable the **Debug** checkbox (labeled “Show Debug Messages”).

Important:

- Pop detection and CSV extensions exist **only** when Debug is enabled.
- When Debug is disabled, all detection/annotation logic is bypassed.

## Running the test (Linux)

### 1) Build and install the plugin

From the repo root:

```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64

mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit"
mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/data"
cp build_x86_64/c64stream.so "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit/"
cp -r data/* "$HOME/.config/obs-studio/plugins/c64stream/data/"
```

### 2) Install Python deps (recommended)

```bash
python3 -m pip install -r tests/e2e/requirements.txt
```

### 3) Run the real-device runner

The user-facing entrypoint is:

- `tests/e2e/real-device-av-sync.sh`

Defaults (as of this repo state):

- `--host c64u`
- `--format NTSC`
- `--duration 10`

Examples:

```bash
# Default run
./tests/e2e/real-device-av-sync.sh

# Explicit host, PAL, longer duration
./tests/e2e/real-device-av-sync.sh --host 192.168.1.13 --format PAL --duration 60 --verbose

# Analyze existing artifacts only (no device access, no OBS run)
./tests/e2e/real-device-av-sync.sh --analyze-only tests/e2e/results/real_c64u_av_sync/session_YYYYmmdd_HHMMSS
```

## Running on Windows

### Recommended: WSL2 (Windows + Linux userland)

The real-device shell runner is Bash + Linux-OBS oriented. The most reliable way to run it on Windows is inside **WSL2**
(with WSLg for GUI support).

High-level steps:

1. Install WSL2 + Ubuntu.
2. Inside WSL, install dependencies (including OBS).
3. Build + install the plugin in WSL.
4. Run the same command as on Linux.

Example (inside WSL):

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build pkg-config \
  python3 python3-pip \
  curl \
  obs-studio

python3 -m pip install -r tests/e2e/requirements.txt

cmake --preset ubuntu-x86_64
cmake --build build_x86_64

mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit"
mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/data"
cp build_x86_64/c64stream.so "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit/"
cp -r data/* "$HOME/.config/obs-studio/plugins/c64stream/data/"

./tests/e2e/real-device-av-sync.sh --host 192.168.1.13 --format PAL --duration 60 --verbose
```

Notes:

- If `c64u` does not resolve inside WSL, use `--host <ip>`.

### Native Windows

There is a Python runner (`tests/e2e/real_device_av_sync.py`) that can be executed natively, but it requires a working
native OBS installation and a bit of environment setup (paths, config locations, etc.). Prefer WSL2 unless you
specifically need a native flow.

## Artifacts

Each run creates a session directory under the output base dir:

- `tests/e2e/results/real_c64u_av_sync/session_YYYYmmdd_HHMMSS/`

Typical contents:

- Recording: `*.mp4`
- `obs.csv` and `network.csv`
- `obs_log.txt`
- Analyzer output: `av_pop_report.json`

## Analyzer inputs and interpretation

### OBS log examples (Debug enabled)

```text
[c64stream] VIDEO: A/V pop video #3: frame=528 ts=123456789000 ns, audio_delta_ms=2.4
[c64stream] AUDIO: A/V pop audio #3: ts=123456791400 ns, audio_delta_ms=2.4
```

`audio_delta_ms` is `audio_time - video_time`:

- Positive values mean audio lags video
- Negative values mean audio leads video

### `network.csv` (packet-level, Debug enabled)

Two extra columns are appended:

- `is_all_white` (video packets only): 1 if the UDP payload is all `0x11`
- `has_signal` (audio packets only): 1 if any sample in the packet is non-silent

### `obs.csv` (OBS submissions, Debug enabled)

Two extra columns are appended:

- `is_all_white` (video rows): 1 if the submitted frame is all white
- `has_signal` (audio rows): 1 if any sample in the buffer is non-silent

### Correlating logs and CSVs

- Prefer CSV-based analysis if `obs.csv` / `network.csv` are present.
- Use OBS log parsing only as a fallback.

For automatic runs, pops occur every 48 frames, so longer captures make drift easy to spot.

## Recorded results (example ledger)

The table below is a snapshot of real-device runs captured on **2026-01-07**.

Acceptance criteria used:

- `p50_delta_ms <= 20`
- `p95_delta_ms <= 40`
- `max_delta_ms <= 60`

| Duration | Status | p50 (ms) | p95 (ms) | max (ms) | Pop count | Standard | Results dir                                                   |
| -------: | :----: | -------: | -------: | -------: | --------: | :------: | ------------------------------------------------------------- |
|      10s |  pass  |    1.101 |    3.211 |    3.211 |        15 |   PAL    | `tests/e2e/results/real_c64u_av_sync/session_20260107_185615` |
|      60s |  pass  |    1.104 |    3.276 |    3.333 |        67 |   PAL    | `tests/e2e/results/real_c64u_av_sync/session_20260107_185641` |
|     600s |  pass  |    1.093 |    3.263 |    6.493 |       610 |   PAL    | `tests/e2e/results/real_c64u_av_sync/session_20260107_185808` |

NTSC run note (same date): the analyzer reported `authoritative_source = network_csv` because `obs.csv` contained no pop
events.

| Duration | Status | p50 (ms) | p95 (ms) | max (ms) | Pop count | Standard | Results dir                                                   |
| -------: | :----: | -------: | -------: | -------: | --------: | :------: | ------------------------------------------------------------- |
|     600s |  pass  |    1.695 |    4.371 |   22.712 |      1360 |   NTSC   | `tests/e2e/results/real_c64u_av_sync/session_20260107_175147` |

## References

- Protocol details and device control: `doc/c64u-stream-spec.md`
- Mock E2E scenarios and harness behavior: `doc/testing/e2e.md`
