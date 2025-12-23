# Scenarios

E2E test scenarios for validating C64 Stream plugin effects and configurations.

## Quick Start

```bash
# List all available scenarios
./e2e.sh --list-scenarios

# Run a specific scenario
./e2e.sh --scenario ntsc_amber_monitor --verbose

# Run with assertion verification
./e2e.sh --scenario ntsc_amber_monitor --verbose
python3 assertion_framework.py \
    --mp4 test_output/c64_recording.mp4 \
    --scene-json scenarios/ntsc_amber_monitor/overrides/basic/scenes/C64StreamTest.json
```

## Scenario Structure

Each scenario is a subdirectory with:

```
scenarios/{scenario_name}/
├── scenario.yaml                    # Scenario metadata
└── overrides/                       # OBS config overlay
    └── basic/
        └── scenes/
            └── C64StreamTest.json   # Scene with effect settings
```

### scenario.yaml Format

```yaml
name: Human-readable name
format: PAL or NTSC
overrides_dir: overrides
```

### Effect Settings

Effect settings are embedded directly in the OBS scene JSON under `sources[].settings`:

```json
{
  "sources": [{
    "id": "c64_source",
    "settings": {
      "crt_preset": "Amber Monitor",
      "scan_line_distance": 0.5,
      "tint_mode": 1,
      "tint_strength": 1.0
    }
  }]
}
```

## Available Scenarios

| Scenario             | Format | Preset           | Key Effect            |
| -------------------- | ------ | ---------------- | --------------------- |
| ntsc_default         | NTSC   | Default          | No effects            |
| ntsc_classic_crt     | NTSC   | Classic CRT      | Scanlines + blur      |
| ntsc_amber_monitor   | NTSC   | Amber Monitor    | Amber tint            |
| ntsc_green_monitor   | NTSC   | Green Monitor    | Green tint            |
| ntsc_sharp_pixels    | NTSC   | Sharp Pixels     | No scaling artifacts  |
| ntsc_phosphor_glow   | NTSC   | Phosphor Glow    | Afterglow + bloom     |
| ntsc_vintage_tv      | NTSC   | Vintage TV       | Scanlines + blur      |
| ntsc_arcade_cabinet  | NTSC   | Arcade Cabinet   | Strong scanlines      |
| pal_sharp_pixels     | PAL    | Sharp Pixels     | PAL timing            |
| ntsc_delay_500ms     | NTSC   | Default          | High buffer delay     |
| scanlines            | PAL    | Custom           | Scanline uniformity   |

## Test Pattern Notes

The generated video pattern is designed to be both visually obvious and programmatically verifiable:

- **Top-left marker**: a solid block whose color is `frame_num % 16` (quick frame progression sanity check).
- **Top-right palette tile**: a stable **4×4 tile of all 16 VIC colors** (used to verify color stability and detect drift).
- **Bottom-right A/V pop**: a blinking white-in-black square synchronized with an audio "pop" (used for A/V sync and afterglow tail verification).
