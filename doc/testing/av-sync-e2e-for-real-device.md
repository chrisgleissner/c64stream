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

## Running the test

### Quick start (recommended): deterministic A/V sync suite

Run the unified suite runner. It enforces the required ordering to detect cross-test pollution:

1. Mocked sender (baseline)
2. Real device
3. Mocked sender again (verifies the device run was cleanly disabled/reset)

```bash
cd tests/e2e
./run_avsync_suite.sh --duration 10 --verbose
```

This produces artifacts under:

- `tests/e2e/results/avsync_suite/01_mock_pre/`
- `tests/e2e/results/avsync_suite/02_device/`
- `tests/e2e/results/avsync_suite/03_mock_post/`

### Running only the device-backed scenario

```bash
cd tests/e2e
./e2e.sh --scenario ntsc_default_avsync_device --duration 10 --verbose
```

The device scenario enables A/V sync detection at runtime by toggling the plugin property `record_av_sync` via OBS WebSocket, and disables it again at teardown to reset/stop device activity.

### Prerequisites

- A reachable C64 Ultimate device:
   - UDP stream reachable from the machine running OBS (default ports `21000` video, `21001` audio)
   - Control socket reachable (TCP port `64`) as required by the plugin
- OBS installed and runnable from the command line as `obs`
- OBS WebSocket enabled (OBS v28+ includes it; the E2E harness uses it to toggle `record_av_sync`)
- The `c64stream` plugin built and installed into OBS (including `data/` files)
- Python 3 available as `python3`

Optional (only if you want to rebuild/inspect the C64 programs manually): `64tass`.

### Installation steps

#### 1) Build and install the plugin

From the repo root:

**Linux:**
```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64

mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit"
mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/data"
cp build_x86_64/c64stream.so "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit/"
cp -r data/* "$HOME/.config/obs-studio/plugins/c64stream/data/"
```

**macOS:**
```bash
cmake --preset macos-universal
cmake --build build_macos

mkdir -p "$HOME/Library/Application Support/obs-studio/plugins/c64stream/bin"
mkdir -p "$HOME/Library/Application Support/obs-studio/plugins/c64stream/data"
cp -r build_macos/c64stream.plugin "$HOME/Library/Application Support/obs-studio/plugins/c64stream/bin/"
cp -r data/* "$HOME/Library/Application Support/obs-studio/plugins/c64stream/data/"
```

**Windows (WSL2 or Git Bash):**
See Windows section below for full details.

#### 2) Install Python dependencies

```bash
python3 -m pip install -r tests/e2e/requirements.txt
```

#### 3) Install 64tass assembler

**Linux (Debian/Ubuntu):**
```bash
sudo apt-get install 64tass
```

**macOS:**
```bash
brew install 64tass
```

**Windows:**
Download from https://sourceforge.net/projects/tass64/ and add to PATH.

### Running the test

**Default run (suite):**

```bash
cd tests/e2e
./run_avsync_suite.sh
```

**Custom host:** the device scenario defaults to hostname `c64u`. Override by editing the scenario overrides in
`tests/e2e/scenarios/ntsc_default_avsync_device/scenario.yaml` (e.g. `c64_host: 192.168.1.13`).

**Run just the device scenario:**

```bash
cd tests/e2e
./e2e.sh --scenario ntsc_default_avsync_device --duration 20 --verbose
```

### Understanding the output

After a successful run, you'll find in the session directory:

- `c64_recording.mp4` - OBS recording of the C64 output
- `obs.csv` - Frame-by-frame timing from OBS plugin
- `network.csv` - Packet-by-packet network timing
- `obs.log` - OBS debug log with A/V sync pop detections
- `av_pop_report.json` - Complete A/V sync analysis results
- `README.md` - Human-readable session summary

The report includes:
- **A/V offset statistics:** p50, p95, max deltas in milliseconds
- **Pop event counts:** Video and audio pops detected
- **CSV correlation:** Timing data from plugin instrumentation
- **MP4 analysis:** Independent verification from recorded video

### Running on Windows

#### Option 1: WSL2 (Recommended)

The shell script works seamlessly in WSL2 with WSLg for GUI support.

**Setup steps:**

1. Install WSL2 with Ubuntu:
   ```powershell
   wsl --install
   ```

2. Inside WSL, install dependencies:
   ```bash
   sudo apt-get update
   sudo apt-get install -y \
     build-essential cmake ninja-build pkg-config \
     python3 python3-pip curl obs-studio 64tass

   python3 -m pip install -r tests/e2e/requirements.txt
   ```

3. Build and install the plugin:
   ```bash
   cmake --preset ubuntu-x86_64
   cmake --build build_x86_64

   mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit"
   mkdir -p "$HOME/.config/obs-studio/plugins/c64stream/data"
   cp build_x86_64/c64stream.so "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit/"
   cp -r data/* "$HOME/.config/obs-studio/plugins/c64stream/data/"
   ```

4. Run the suite:
   ```bash
   cd tests/e2e
   ./run_avsync_suite.sh --duration 10 --verbose
   ```

**Note:** If `c64u` hostname doesn't resolve in WSL, use `--host <ip>` or add to `/etc/hosts`.

#### Option 2: Git Bash

The script also works in Git Bash on Windows, but requires:
- Git for Windows (includes bash, curl)
- OBS Studio installed
- Python 3 installed and in PATH
- 64tass assembler in PATH

**Run from Git Bash:**
```bash
cd tests/e2e
./run_avsync_suite.sh --duration 10 --verbose
```

### Running on macOS

The script works natively on macOS with minor adjustments:

1. Install dependencies via Homebrew:
   ```bash
   brew install cmake ninja pkg-config python3 64tass obs
   python3 -m pip install -r tests/e2e/requirements.txt
   ```

2. Build and install the plugin:
   ```bash
   cmake --preset macos-universal
   cmake --build build_macos

   mkdir -p "$HOME/Library/Application Support/obs-studio/plugins/c64stream/bin"
   mkdir -p "$HOME/Library/Application Support/obs-studio/plugins/c64stream/data"
   cp -r build_macos/c64stream.plugin "$HOME/Library/Application Support/obs-studio/plugins/c64stream/bin/"
   cp -r data/* "$HOME/Library/Application Support/obs-studio/plugins/c64stream/data/"
   ```

3. Run the suite:
   ```bash
   cd tests/e2e
   ./run_avsync_suite.sh --duration 10 --verbose
   ```

### Troubleshooting

**OBS WebSocket connection fails:**
- Ensure OBS WebSocket is enabled and reachable from the machine running the test.
- If OBS is already running, close it before running the harness (the harness starts OBS itself).

**OBS doesn't start:**
- Verify OBS is installed: `which obs` (Unix) or `where obs` (Windows)
- Check plugin is installed in the correct location
- Try starting OBS manually first to verify it works

**No video/audio received:**
- Check UDP ports are not blocked by firewall (default: 21000 video, 21001 audio)
- Verify C64U is streaming to the correct IP address
- Use `--verbose` flag to see detailed network activity

**No A/V pops detected:**
- Ensure Debug is enabled for the source (the scenario sets `debug_logging: true`).
- Verify the device is reachable and streaming to the host running OBS.
- Try longer duration: `--duration 30`.

**Hostname `c64u` not found:**
- Use IP address instead: `--host 192.168.1.13`
- Or add to `/etc/hosts` (Unix) or `C:\Windows\System32\drivers\etc\hosts` (Windows):
  ```
  192.168.1.13  c64u
  ```

## Artifacts and Output Files

Each run creates a session directory under the output base dir. For the suite runner:

- `tests/e2e/results/avsync_suite/01_mock_pre/session_YYYYmmdd_HHMMSS/`
- `tests/e2e/results/avsync_suite/02_device/session_YYYYmmdd_HHMMSS/`
- `tests/e2e/results/avsync_suite/03_mock_post/session_YYYYmmdd_HHMMSS/`

Typical contents:

- Recording: `*.mp4`
- `obs.csv` and `network.csv`
- `obs_log.txt`
- `validation_results.json`

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
