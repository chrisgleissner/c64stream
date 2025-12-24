# C64 Stream E2E Test Results

This directory contains reference recordings and test results for E2E testing.

Important: Most subfolders here are generated locally and are intentionally not committed.
Only a small curated subset is tracked in git to avoid bloating the repository.

## Directory Structure

```
tests/e2e/results/
├── README.md
├── ntsc_default/        # committed (NTSC, Default preset)
├── ntsc_green_monitor/  # committed (NTSC, Green Monitor preset)
└── pal_default/         # committed (PAL, Default preset)
```

Any other folders under `tests/e2e/results/` are expected to be local-only outputs
from running scenarios (e.g. `ntsc_classic_crt/`, `scanlines/`, etc.).

## Results

- [NTSC Default](./ntsc_default/README.md)
- [NTSC Green Monitor](./ntsc_green_monitor/README.md)
- [PAL Default](./pal_default/README.md)

## Per-Preset Contents

Each scenario folder contains:

- `c64_recording.mp4` - OBS recording of the test run
- `c64_recording_still.png` - Sample frame showing A/V pop
- `README.md` - Test report with validation results
- `validation_results.json` - Machine-readable validation data
- `network.csv` - Network packet reception log
- `obs.csv` - OBS event log
- `config_used/` - Copy of properties.ini used for the test

## Running Tests

### Single Scenario

```bash
cd tests/e2e
./e2e.sh --scenario ntsc_default --verbose
```

### All Scenarios

```bash
./run_all_scenarios.sh
```

## CI Integration

In CI, tests run via the GitHub Actions workflow with matrix builds:
- Each scenario runs in parallel
- Results are uploaded as artifacts
- Failures are reported in the job summary
