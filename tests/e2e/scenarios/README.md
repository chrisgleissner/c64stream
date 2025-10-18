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
