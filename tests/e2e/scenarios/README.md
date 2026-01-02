# E2E Test Scenarios

E2E test scenarios for validating C64 Stream plugin effects and configurations.

## Quick Start

```bash
# List all available scenarios
./e2e.sh --list-scenarios

# Run a specific scenario
./e2e.sh --scenario ntsc_amber_monitor --verbose

# Run assertion verification against recording
python3 -m assertions \
    --mp4 test_output/c64_recording.mp4 \
    --scenario ntsc_amber_monitor \
    --verbose
```

## Scenario Structure (New Format)

Each scenario is a **concise YAML file** that references a preset from `effect_presets.ini`:

```
scenarios/{scenario_name}/
├── scenario.yaml          # Scenario definition (10-15 lines)
└── generated/             # Auto-generated at runtime
    └── basic/scenes/C64StreamTest.json
```

### scenario.yaml Format

```yaml
name: Human-readable scenario name
format: PAL or NTSC
preset: Preset name from effect_presets.ini

# Optional: Override specific effect settings
overrides:
  afterglow_duration_ms: 75  # Boost for E2E detection

# Assertions to run against recorded output
assertions:
  - video_quality
  - audio
  - tint
  - afterglow
  - scanlines
```

### Available Assertions

| Assertion          | Description                                                      |
| ------------------ | ---------------------------------------------------------------- |
| `video_quality`    | Basic video quality: duration, resolution, black frames          |
| `audio`            | Audio presence and quality validation                            |
| `tint`             | Color tint detection (amber/green monitor)                       |
| `afterglow`        | Phosphor persistence/decay verification                          |
| `scanlines`        | Scanline pattern uniformity (<1% variance)                       |
| `frame_progression`| Verify frames increment properly (frame counter marker)          |
| `palette_mapping`  | Verify palette colors match expected VPL (16-color watch region) |
| `palette_stability`| Verify palette colors don't drift over time                      |
| `sharp_pixels`     | Verify pixel sharpness when effects disabled                     |

## Available Scenarios

| Scenario              | Format | Preset         | Key Assertions                    |
| --------------------- | ------ | -------------- | --------------------------------- |
| ntsc_default          | NTSC   | Default        | video_quality, audio              |
| ntsc_classic_crt      | NTSC   | Classic CRT    | video_quality, audio, afterglow, scanlines |
| ntsc_amber_monitor    | NTSC   | Amber Monitor  | video_quality, audio, tint, afterglow, scanlines |
| ntsc_green_monitor    | NTSC   | Green Monitor  | video_quality, audio, tint, afterglow, scanlines |
| ntsc_sharp_pixels     | NTSC   | Sharp Pixels   | video_quality, audio              |
| ntsc_sharp_scan_lines | NTSC   | Default        | video_quality, audio, scanlines   |
| ntsc_phosphor_glow    | NTSC   | Phosphor Glow  | video_quality, audio, afterglow, scanlines |
| ntsc_vintage_tv       | NTSC   | Vintage TV     | video_quality, audio, afterglow, scanlines |
| ntsc_arcade_cabinet   | NTSC   | Arcade Cabinet | video_quality, audio, scanlines   |
| ntsc_palette_vibrant  | NTSC   | Default        | video_quality, audio, palette_mapping |
| ntsc_palette_muted    | NTSC   | Default        | video_quality, audio, palette_mapping |
| ntsc_delay_buffer500ms| NTSC   | Default        | video_quality, audio (buffer test)|
| pal_default           | PAL    | Default        | video_quality, audio              |

## Adding New Scenarios

1. Create directory: `scenarios/{format}_{preset_name}/`
2. Create `scenario.yaml` with:
   - `name`: Human-readable name
   - `format`: PAL or NTSC
   - `preset`: Preset from `data/effect_presets.ini`
   - `overrides`: Optional effect tweaks (for E2E detectability)
   - `assertions`: List of assertions to run

Example for a new "Cinema Mode" scenario:

```yaml
name: NTSC Cinema Mode
format: NTSC
preset: Vintage TV
overrides:
  blur_strength: 0.5
  bloom_strength: 0.6
assertions:
  - video_quality
  - audio
  - scanlines
```

## Base Template

The `base_template.json` contains the common OBS scene structure.
The `scenario_loader.py` merges preset settings and overrides into this template at runtime.

## Test Pattern Notes

The generated video pattern is designed to be both visually obvious and programmatically verifiable:

- **Top-left marker**: solid block with color `frame_num % 16` (frame progression check)
- **Top-right palette tile**: 4×4 grid of all 16 VIC colors (color stability verification)
- **Bottom-right A/V pop**: blinking white square with audio pop (A/V sync and afterglow tail)
