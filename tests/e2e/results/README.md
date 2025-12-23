# C64 Stream E2E Test Results

This directory contains reference recordings and test results for E2E testing.

## Directory Structure

```
results/
├── README.md           # This file
├── pal/                # PAL format results (50Hz)
│   ├── default/        # Default preset (no effects)
│   ├── classic_crt/    # Classic CRT preset
│   ├── amber_monitor/  # Amber tint preset
│   ├── green_monitor/  # Green phosphor preset
│   ├── sharp_pixels/   # Sharp pixels preset
│   ├── phosphor_glow/  # Phosphor afterglow preset
│   ├── vintage_tv/     # Vintage TV preset
│   └── arcade_cabinet/ # Arcade cabinet preset
└── ntsc/               # NTSC format results (60Hz)
    └── (same structure as pal/)
```

## Results

- [ntsc](./ntsc/README.md)
- [pal](./pal/README.md)

## Per-Preset Contents

Each preset folder contains:

- `c64_recording.mp4` - OBS recording of the test run
- `c64_recording_still.png` - Sample frame showing A/V pop
- `README.md` - Test report with validation results
- `validation_results.json` - Machine-readable validation data
- `network.csv` - Network packet reception log
- `obs.csv` - OBS event log
- `config_used/` - Copy of properties.ini used for the test

## Running Tests

### Single Preset

```bash
cd tests/e2e
python preset_runner.py --preset arcade_cabinet --format PAL --verbose
```

### All Presets

```bash
python preset_runner.py --all-presets --format PAL
```

### With Assertions

The preset runner automatically runs the assertion framework after each test.
To skip assertions:

```bash
python preset_runner.py --preset green_monitor --skip-assertions
```

## Assertion Framework

Each preset has specific assertions based on its enabled effects:

| Preset         | Video | Audio | Tint | Afterglow | Scanlines |
|----------------|-------|-------|------|-----------|-----------|
| Default        | ✅    | ✅    | -    | -         | -         |
| Classic CRT    | ✅    | ✅    | -    | ✅        | ✅        |
| Amber Monitor  | ✅    | ✅    | ✅   | ✅        | ✅        |
| Green Monitor  | ✅    | ✅    | ✅   | ✅        | ✅        |
| Sharp Pixels   | ✅    | ✅    | -    | -         | -         |
| Phosphor Glow  | ✅    | ✅    | -    | ✅        | ✅        |
| Vintage TV     | ✅    | ✅    | -    | ✅        | ✅        |
| Arcade Cabinet | ✅    | ✅    | -    | ✅        | ✅        |

## CI Integration

In CI, tests run via the GitHub Actions workflow with matrix builds:
- Each preset × format combination runs in parallel
- Results are uploaded as artifacts
- Failures are reported in the job summary
