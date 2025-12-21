# Scenarios

Place scenario folders here. Each scenario is a subdirectory with:

- scenario.yaml: minimal YAML with keys:
  - name: Human-readable name
  - format: PAL or NTSC
  - overrides_dir: relative path of directory with files to overlay into ~/.config/obs-studio after baseline copy
- overrides/: directory with files mirroring OBS config structure (optional), e.g.:
  - basic/profiles/C64StreamTest/basic.ini
  - basic/scenes/C64StreamTest.json
  - plugins/c64stream/data/properties.ini

During a run with local-build.sh --e2e-scenarios, each scenario is executed, outputs are copied to tests/e2e/results/SCENARIO and a top-level results/README.md is generated.

## Test pattern notes (what to look for in recordings)

The generated video pattern is designed to be both visually obvious and programmatically verifiable:

- **Top-left marker**: a solid block whose color is `frame_num % 16` (quick frame progression sanity check).
- **Top-right palette tile**: a stable **4×4 tile of all 16 VIC colors** (used to verify color stability and detect drift).
- **Bottom-right A/V pop**: a blinking white-in-black square synchronized with an audio “pop” (used for A/V sync and afterglow tail verification).
