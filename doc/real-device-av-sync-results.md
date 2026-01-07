# Real-device A/V sync results (C64 Ultimate)

This documents the real-device A/V sync “pop” tests using OBS + the `c64stream` plugin against a C64 Ultimate device.

## How to reproduce these results

Prerequisites and full setup steps are documented in `doc/real-device-av-sync.md`.

Summary (Linux / WSL2):

1) Build and install the plugin into OBS.
2) Ensure the C64 Ultimate is reachable (REST + UDP).
3) Run the same duration ladder with the same thresholds.

Commands used for the ladder (PAL):

```bash
./tests/e2e/real-device-av-sync.sh --format PAL --duration 10 --verbose
./tests/e2e/real-device-av-sync.sh --format PAL --duration 60 --verbose
./tests/e2e/real-device-av-sync.sh --format PAL --duration 600 --verbose
```

10-minute run in NTSC mode (device + OBS configured NTSC):

```bash
./tests/e2e/real-device-av-sync.sh --format NTSC --duration 600 --verbose
```

References:

- Real-device runner usage and OS-specific setup: `doc/real-device-av-sync.md`
- E2E overview (mock scenarios vs real-device flow): `doc/e2e.md`

## Setup

- Runner: `tests/e2e/real-device-av-sync.sh`
- Device host: `c64u`
- Video standard used for OBS configuration: match the device (PAL or NTSC)
- A/V delay acceptance criteria (from analyzer):
  - `p50_delta_ms <= 20`
  - `p95_delta_ms <= 40`
  - `max_delta_ms <= 60`
- Authoritative timing source: `obs.csv` (as reported by analyzer)

### What `p50`, `p95`, and `max` mean

For each matched A/V “pop” event:

- Video pop time = timestamp of the detected full-white video frame.
- Audio pop time = timestamp of the detected audio “tone present” event.
- Per-pop A/V delta (ms) = $|t_{audio} - t_{video}|$.

The reported metrics summarize the distribution of these per-pop deltas over the whole run:

- `p50_delta_ms`: 50th percentile (median) of per-pop deltas.
- `p95_delta_ms`: 95th percentile of per-pop deltas.
- `max_delta_ms`: maximum per-pop delta observed.

## Versions used

Captured from the environment and the OBS log artifacts produced by the run.

- OS: Linux (Ubuntu 24.04.3 LTS), kernel `6.14.0-37-generic` (x86_64)
- OBS Studio: 32.0.2 (`obs --version`)
- Plugin (as logged by OBS during the 10-minute run):
  - Version string: `C64 Stream Plugin 1.0.2-7-gbc4c8b5 (bc4c8b5)`
  - Build info: `1.0.2-7-gbc4c8b5 (Git ID: bc4c8b5, Built: 2026-01-07 15:47:06 UTC)`
  - Source: `tests/e2e/results/real_c64u_av_sync/session_20260107_185808/obs_log.txt`
  - NTSC run log (same build): `tests/e2e/results/real_c64u_av_sync/session_20260107_175147/obs_log.txt`
- Repo revision (for traceability):
  - `git rev-parse HEAD`: `bc4c8b52cbc8815b10fb183fdf0c6623e62165b9`
  - `git describe --tags --always`: `1.0.2-7-gbc4c8b5-dirty`

## Commands

All runs below were executed with:

- `./tests/e2e/real-device-av-sync.sh --format <PAL|NTSC> --duration <seconds> --verbose`

## Run results

### 2026-01-07 — Linux (Ubuntu 24.04.3 LTS)

| Duration | Status | p50 (ms) | p95 (ms) | max (ms) | Pop count | Inferred standard | Results dir |
|---:|:---:|---:|---:|---:|---:|:---:|---|
| 10s | pass | 1.101 | 3.211 | 3.211 | 15 | PAL | `tests/e2e/results/real_c64u_av_sync/session_20260107_185615` |
| 60s | pass | 1.104 | 3.276 | 3.333 | 67 | PAL | `tests/e2e/results/real_c64u_av_sync/session_20260107_185641` |
| 600s | pass | 1.093 | 3.263 | 6.493 | 610 | PAL | `tests/e2e/results/real_c64u_av_sync/session_20260107_185808` |

### 2026-01-07 — Linux (Ubuntu 24.04.3 LTS) — NTSC

Note: For this run the analyzer reported `authoritative_source = network_csv` because `obs.csv` contained no pop events.

| Duration | Status | p50 (ms) | p95 (ms) | max (ms) | Pop count | Inferred standard | Results dir |
|---:|:---:|---:|---:|---:|---:|:---:|---|
| 600s | pass | 1.695 | 4.371 | 22.712 | 1360 | NTSC | `tests/e2e/results/real_c64u_av_sync/session_20260107_175147` |

## Notes

- All runs completed with device reset on exit (runner performs `PUT /v1/machine:reset`).
- The analyzer also reports pop cadence (`pop_period_p50_ms`) consistent with PAL (~977ms).
