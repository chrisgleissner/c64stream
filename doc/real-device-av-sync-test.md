# Real C64U A/V Sync Test Plan

Plan for a local-only test that drives a real C64 Ultimate (C64U) via REST, uses the documented C64U control socket,
records 10s (default, configurable) in OBS, and verifies A/V pop delta is minimal. This also defines an offline log
analyzer that can consume `obs.csv`, `network.csv`, or `obs.log`
from another user's report.

## Goals

- Provide a user-triggered, fully automated flow to run `tools/c64/av-sync-auto.asm` on a real C64U via REST.
- Start OBS, record for 10s, and compute A/V pop delta from debug artifacts.
- Offer a standalone analyzer to process logs/CSVs provided by other users.
- Reuse existing E2E infrastructure wherever possible.

## Non-goals

- CI execution (local only; requires real hardware and OBS).
- Replacing existing mock E2E scenarios.
- Changing the C64U control protocol documented in `doc/c64-stream-spec.md`.

## Key Reuse

- `tools/c64/c64-build.sh`: build `av-sync-auto.asm` -> `.prg`.
- `tests/e2e/e2e.py`: OBS orchestration, recording, artifact capture, and cleanup patterns.
- `tests/e2e/assertions/av_pop_delta.py`: CSV-based A/V pop delta checks (already handles `obs.csv` + `network.csv`).
- Debug A/V pop logging already produced by the plugin (see `doc/av-sync.md` for log format).
- C64U control commands over TCP port 64 (see `doc/c64-stream-spec.md`).

## Entry Points

### 1) Full device-driven run (script)

Shell entrypoint with clear help:

- Path: `tests/e2e/real-device-av-sync.sh`
- Responsibilities:
  - Build or reuse `tools/c64/av-sync-auto.prg`.
  - Upload + execute the PRG on C64U via REST.
  - Start OBS via a dedicated test profile/scene and record for 10s.
  - Collect artifacts (`obs.csv`, `network.csv`, `obs.log`, recording).
  - Run the analyzer and print a concise summary + JSON report.

Example help text:

```bash
./tests/e2e/real-device-av-sync.sh --help

Usage:
  real-device-av-sync.sh --host <c64u-host> [options]

Required:
  --host <name|ip>           C64 Ultimate hostname or IP (e.g. 192.168.1.13)

Options:
  --rest-scheme <scheme>     REST scheme (default: http)
  --run-prg-endpoint <path>  REST endpoint for PRG upload+run (default: /v1/runners:run_prg)
  --rest-token <token>       Optional REST auth token
  --rest-token-header <hdr>  Header name for REST token (default: X-Password)
  --duration <sec>           Recording duration (default: 10)
  --output-dir <dir>         Output dir (default: tests/e2e/results/real_c64u_av_sync)
  --max-delta-ms <ms>        Max allowed A/V delta (default: 30)
  --min-pop-events <n>       Minimum pop events required (default: 2)
  --no-build                Skip PRG build (use existing .prg)
  --analyze-only <path>      Run analyzer only (see below)
  --obs-csv <path>           Analyze obs.csv directly (no OBS run)
  --network-csv <path>       Analyze network.csv directly (no OBS run)
  --obs-log <path>           Analyze obs.log directly (no OBS run)
  --verbose                  Verbose logs
```

Note: each run writes to `session_YYYYmmdd_HHMMSS` under `--output-dir`.

### 2) Offline analysis (script / python)

Python analyzer entrypoint:

- Path: `tests/e2e/av_pop_analyzer.py`
- Modes:
  - `--obs-csv <path>` (parse debug columns)
  - `--network-csv <path>` (parse debug columns)
  - `--obs-log <path>` (parse `[c64stream] ... A/V pop ...` lines)
- Output:
  - Human summary (counts, avg/max delta, optional drift line)
  - JSON with raw deltas and pass/fail verdict

The shell entrypoint can forward its own artifacts to the analyzer, while external users can run the analyzer directly:

```bash
python3 tests/e2e/av_pop_analyzer.py --obs-log /path/to/obs.log --max-delta-ms 30
python3 tests/e2e/av_pop_analyzer.py --obs-csv /path/to/obs.csv --network-csv /path/to/network.csv
```

## C64U Control Endpoints (from `doc/c64-stream-spec.md`)

- Control channel: TCP socket on port 64.
- Start stream: `FF2n` (n = stream ID).
  - Parameters: duration (0 = infinite, unit = 5ms ticks), optional destination string.
- Stop stream: `FF3n` (n = stream ID).
- Example commands (as documented):
  - Enable stream 0 (1 second): `20 FF 02 00 00 C8`
  - Enable stream 0 (infinite): `20 FF 0F 00 00 00 [IP as ASCII]`
  - Disable stream 0: `30 FF 00 00`

## Device-Driven Flow (Detailed)

1) **Build PRG**
   - Use `tools/c64/c64-build.sh av-sync-auto.asm` unless `--no-build` is set.
   - Use the resulting `tools/c64/av-sync-auto.prg`.

2) **Launch via REST**
   - Upload the `.prg` to C64U and start it on every run (no caching).
   - REST details are vendor-specific; the script should accept:
     - Host (`--host`, used to build the base URL)
     - Optional auth token (`--rest-token`, if required)
     - Optional endpoint overrides (if firmware variants differ)
   - The REST request should be retried with a short timeout and clear error if unreachable.

3) **OBS Configuration**
   - Ensure the C64 Stream source has Debug enabled (pop detection logs + CSV columns).
   - Ensure `c64_host` is set to the device host (not `0.0.0.0`).
   - Use a dedicated test profile/scene copied from `tests/e2e/config/obs-studio` and restore user config afterward.

4) **Recording (10s)**
   - Reuse `tests/e2e/e2e.py` to start OBS, wait for readiness, record for 10s, then stop.
   - No UDP replay; real device drives the stream.
   - Capture `obs.csv`, `network.csv`, and a copy of `obs.log`.

5) **A/V Delta Analysis**
   - Prefer CSV-based analysis if any CSVs are present (authoritative).
   - If only `obs.csv` is present, compute delta from OBS timestamps.
   - If only `obs.log` is present, compute delta from log lines (fallback).
   - Fail if debug markers are missing or insufficient pops exist.

## Analyzer Details

### Inputs

- `obs.csv`: uses `is_all_white` + `has_signal` columns (Debug mode required).
- `network.csv`: uses `is_all_white` + `has_signal` columns (Debug mode required).
- `obs.log`: parse log lines like:
  - `VIDEO: A/V pop video #N: frame=... ts=... ns, audio_delta_ms=...`
  - `AUDIO: A/V pop audio #N: ts=... ns, audio_delta_ms=...`

### Output Metrics

- Pop count (audio/video)
- Max/avg delta (ms)
- Optional drift estimate (simple linear regression on delta vs time)
- Pass/fail based on `--max-delta-ms` (default 30ms, configurable)

### Error Handling

- If no pops found, report a failure with guidance:
  - Debug may be off
  - Device did not run `av-sync-auto`
  - Recording too short

## Where This Fits in Tests

- Add a new local-only test target that is triggered by the shell script.
- Do not add to CI. Document in `doc/e2e.md` as a local-only, real-hardware test.
- Add a small README section in the script help explaining required hardware and Debug mode.

## Open Questions / Decisions

- C64U REST endpoint path/headers for PRG upload+start (confirm firmware details, especially auth).
- Whether different firmware versions require alternate endpoint overrides.
