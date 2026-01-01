# Palette configuration system

## Non-interactive execution (must finish end-to-end)

- Do not ask the user questions or request decisions; resolve ambiguities by choosing the most conservative, repo-consistent option and ensure the docs reflect the resulting behavior.
- Do not stop at partial work: implement *all* Deliverables (code + shipped palettes + persistence + UI + LUT integration + docs) before declaring completion.
- Never ignore errors/warnings/assertion failures. Before declaring completion, run `cmake --preset ubuntu-x86_64`, `cmake --build build_x86_64`, `./build-aux/run-clang-format --check`, and `ctest --test-dir build_x86_64 --output-on-failure` with zero failures.
- Before declaring completion, ensure the E2E scenarios `ntsc_palette_vibrant` and `ntsc_palette_muted` pass with zero `palette_mapping` assertion errors and the results are present in `tests/e2e/results/<scenario>/README.md` (run locally via `cd tests/e2e && ./e2e.sh --scenario ntsc_palette_vibrant --verbose` and `./e2e.sh --scenario ntsc_palette_muted --verbose`).

## Repo pointers (read first)

- **Default palette + LUT hot path**: `src/c64-color.c` (`vic_colors[16]` is the current hard-coded palette; `color_pair_lut[256]` maps one source byte (two 4-bit pixels: low nibble then high nibble) to two consecutive 32-bit pixels packed into one `uint64_t`; `c64_convert_pixels_optimized()` is LUT lookup + 64-bit stores in the hot loop)
- **Where pixel conversion is used**: `src/c64-video.c` (`c64_assemble_frame_with_interpolation()` calls `c64_convert_pixels_optimized()` per line)
- **Output pixel format**: `src/c64-video.c` (frames are output with `obs_frame.format = VIDEO_FORMAT_RGBA`; the `uint32_t` pixels produced by the LUT must match that format)
- **OBS properties UI + file-dialog pattern**: `src/c64-properties.c` (`c64_create_properties()`, `obs_properties_add_group()`, `obs_properties_add_path()` + `obs_properties_add_button()` pattern in Import/Export)
- **Defaults + shipped config**: `src/c64-properties.c` (`c64_set_property_defaults()`) and `data/properties.ini` (loaded via `obs_module_file("properties.ini")`)
- **Settings apply/live update**: `src/c64-source.c` (`c64_update()`) and source creation init: `src/c64-source.c` (`c64_create()` calls `c64_init_color_conversion_lut()` once)
- **Shipped plugin data**: runtime “plugin-data/” is accessed via `obs_module_file(...)`; in this repo that maps to the source tree `data/` directory (installed recursively by CMake)
- **Filesystem helpers**: `src/c64-file.c/h` (`c64_create_directory_recursive()`, `c64_get_user_documents_path()`)
- **Shipped presets example (INI → dropdown)**: `src/c64-presets.c` (`c64_presets_init()`, `parse_presets_file()`, `c64_presets_populate_list()`), with shipped `data/effect_presets.ini`
- **Localization for new UI strings**: `data/locale/en.ini` (plus other `data/locale/*.ini`)
- **E2E regression sensitivity**: `tests/e2e/assertions/palette_stability.py` checks that the on-screen palette tile doesn’t drift over time (it is not an absolute “default palette values” check)
- **E2E palette tile geometry (C64-coordinate reference; don’t hardcode output pixels)**: `tests/e2e/generate_packets.py` (top-right “VIC-II palette reference” is a 4×4 grid of colors 0–15, row-major; geometry is defined in C64 coordinates and will be rescaled/filtered/encoded by OBS, so assertions must map it via detected content bounds and sample conservatively)
- **E2E scenarios to add**: `tests/e2e/scenarios/ntsc_palette_vibrant/scenario.yaml` and `tests/e2e/scenarios/ntsc_palette_muted/scenario.yaml` (must use `preset: Default`, only override palette selection, and must run `palette_mapping`)
- **E2E report integration**: `tests/e2e/e2e.py` (writes `validation_results.json`) and `tests/e2e/e2e.sh` (`generate_report()` writes `tests/e2e/results/<scenario>/README.md`)
- **Robust assertion design reference**: `doc/frame-progression-marker-detection.md` (adaptive thresholds + content-bounds mapping for resilience across scaling/filters/encoders)

## OBS properties reference (quick notes)

From `https://docs.obsproject.com/reference-properties`:

- **Groups/sections**: `obs_properties_add_group(...)` + `obs_property_group_content(...)`
- **Dropdowns**: `obs_properties_add_list(..., OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING)` + `obs_property_list_add_string(...)`
- **Buttons**: `obs_properties_add_button(...)` / `obs_properties_add_button2(...)`; the callback returns `true` to refresh the properties UI when needed
- **Modified callbacks**: `obs_property_set_modified_callback(...)` / `obs_property_set_modified_callback2(...)`; return `true` to refresh the properties UI when layout/values should update
- **File/directory pickers**: `obs_properties_add_path(..., OBS_PATH_FILE|OBS_PATH_FILE_SAVE|OBS_PATH_DIRECTORY, filter, default_path)`
- **Color pickers**: `obs_properties_add_color(...)` / `obs_properties_add_color_alpha(...)` (values retrieved via `obs_data_get_int()`)

---

You are implementing a **Palette configuration system** for an OBS **source plugin** (C/C++), used to render video data streamed from a **Commodore 64 Ultimate** device.

The goal is to allow users to select, load, edit, and persist **C64 colour palettes**, while preserving an existing **high-performance lookup-table (LUT) based pixel-mapping optimisation**.
This is a functional and UX enhancement only. Do NOT regress performance, latency, determinism, or rendering correctness.

---

## Core invariants (must not be violated)

- Rendering output for the **Default** palette must be bit-identical to the current hard-coded mapping. *(Repo note: `vic_colors[16]` in `src/c64-color.c` is the canonical mapping used to build `color_pair_lut[256]`, and the resulting pixels are output as `VIDEO_FORMAT_RGBA` in `src/c64-video.c`.)*
- Packet processing must remain LUT-only in the hot path. *(Repo note: `c64_convert_pixels_optimized()` in `src/c64-color.c` is called from `c64_assemble_frame_with_interpolation()` in `src/c64-video.c`.)*
- Palette changes must affect subsequent packets immediately.
- Persisted palette data lives only in palette files. The INI is an index, not a data store.

---

## Palette concept

- A palette defines **exactly 16 colours**, indices 0–15.
- Indices map directly to Commodore 64 colour codes.
- Palette data is stored in `.vpl` files only.
- A palette INI file stores **references to palette files**, not RGB values.

---

## Shipped palettes (mandatory)

The plugin MUST ship with preinstalled palettes located in:

plugin-data/palettes/ *(Repo note: shipped plugin data is `data/` in this repo; implement as `data/palettes/` and load via `obs_module_file("palettes/<file>.vpl")`.)*

These palettes MUST be auto-discovered and appear in the Palette dropdown.

### Required shipped palettes

1. **Default**
   - Matches the current hard-coded palette exactly.
   - Used when no palette is explicitly selected.
   - Baseline for correctness and regression testing.

2. **Vibrant**
   - Higher saturation and contrast.
   - Tuned for modern LCD/OLED displays and video pipelines.
   - Still recognisably C64, not stylised.

3. **Muted**
   - Reduced saturation and softer contrast.
   - Evokes aged CRTs and home TVs.

4. **Warm**
   - Subtle warm white-point shift.
   - Represents tube and calibration variance.
   - No aggressive hue skew.

5. **Cool**
   - Subtle cool white-point shift.
   - Sibling to Warm.
   - Calibration-style variation, not an effect.

Each shipped palette:
- MUST be a valid `.vpl` file.
- MUST follow the VPL specification below.
- SHOULD contain a meaningful first comment line used as its display name.

No speculative, “inspired”, or non-C64 system palettes are to be shipped.

---

## Palette file format (VPL – authoritative)

The implementation MUST follow the Ultimate-64 / VICE VPL specification exactly.

### Parsing rules

- Read line by line, supporting both Windows and Linux/MacOS line breaks.
- **Anything after `#` on any line is ignored** (inline comments allowed).
- Lines that are empty after stripping comments are ignored.
- Lines starting with `#` are ignored.
- A valid colour line contains:
  - Three hexadecimal byte values: RR GG BB
  - Values separated by one or more spaces
  - An optional fourth value (dither) may exist and MUST be ignored
- Hex values are case-insensitive.

### Structural rules

- After ignoring comments and empty lines, the file MUST contain **exactly 16 valid colour lines**.
- Line order is fixed and corresponds to C64 colour codes:
  0 Black
  1 White
  2 Red
  3 Cyan
  4 Purple
  5 Green
  6 Blue
  7 Yellow
  8 Orange
  9 Brown
  10 Pink
  11 Dark Grey
  12 Medium Grey
  13 Light Green
  14 Light Blue
  15 Light Grey

### Validation

- Any deviation (wrong count, malformed hex) invalidates the file.
- On validation failure:
  - Active palette remains unchanged.
  - LUTs MUST NOT be rebuilt.

---

## OBS Properties UI

Add a new **Palette** section, placed immediately **before the existing Effects section**. *(Repo note: Effects group is created in `src/c64-properties.c` as `effects_group`; insert the new group before that in `c64_create_properties()`.)*

### Controls (top to bottom)

1. **Palette dropdown**
   - Populated from:
     - Shipped palettes in plugin-data/palettes/*.vpl
     - User palettes referenced in the palette INI
   - Sorted alphabetically, with **Default always first**.
   - Display name resolution:
     1. First comment line in the `.vpl` file (after stripping `#`), if present.
     2. Otherwise, filename without extension.

2. **Buttons**
   - Load from file…
   - Save
   - Save as…
   - Revert

3. **Visual colour editor**
   - Always visible.
   - Shows the palette currently selected in the Palette dropdown, and refreshes immediately when the dropdown selection changes.
   - 4×4 grid of colour pickers. *(Implementation note: OBS properties are form-based; represent the 4×4 as four rows (0–3, 4–7, 8–11, 12–15) via grouping and clear labels.)*
   - One picker per colour index (0–15), clearly labelled.

---

## UX and behaviour rules

### Palette selection
- Selecting a palette:
  - Loads the palette file.
  - Updates the colour picker grid.
  - Applies immediately to rendering.
  - Clears any in-memory edits.

### Visual editing
- Editing any colour picker:
  - Immediately updates rendering.
  - Operates on an in-memory working copy.
  - Does NOT write to disk automatically.

### Save
- If active palette is a **user palette**:
  - Overwrite its referenced `.vpl` file.
- If active palette is a **shipped palette**:
  - Do NOT overwrite.
  - Apply copy-on-write and internally redirect to Save as….

### Save as…
- Prompt for a new palette name. *(Implementation note: use an OBS save-file selector seeded to the user palette directory and treat the chosen filename (without extension) as the palette name.)*
- Write a new `.vpl` file to the user palette location.
- Add a new entry to the palette INI referencing that file.
- Select the new palette immediately.

### Revert
- Reload the palette file referenced by the current INI entry.
- Discard in-memory edits.
- Rebuild LUTs and update rendering.

### Load from file…
- Open a file selector filtered to `.vpl`. *(Repo note: see the existing Import/Export UI pattern in `src/c64-properties.c` which pairs `obs_properties_add_path()` with a button callback because buttons alone don’t open file dialogs in libobs.)*
- Validate strictly against the VPL spec.
- If valid:
  - Add it to the palette INI.
  - Make it selectable like any other palette.
  - Select and apply immediately.
- If invalid:
  - Show an error.
  - Leave the current palette unchanged.

---

## Persistence model

- Palette INI:
  - Stores palette entries as references to `.vpl` files.
  - Does NOT store RGB data.
- Palette files:
  - Single source of truth for colour data.
- Storage locations (must be user-writable; do not ask the user):
  - Shipped palettes are read-only and live under plugin data: `obs_module_file("palettes/<name>.vpl")`.
  - User palettes and the palette index INI must be stored in a per-user writable location (prefer OBS module config paths such as `obs_module_config_path(...)`; if unavailable, fall back to a subfolder under the user’s Documents directory via `c64_get_user_documents_path()`).
  - OBS source settings must persist only palette *selection* (an ID/name/path reference), not palette RGB data.

---

## Rendering and performance requirements

### Existing optimisation
- The plugin uses a 256-entry **pixel-pair LUT**: one source byte (two 4-bit pixels) → two 32-bit output pixels (packed into one `uint64_t` store).
- This optimisation MUST be preserved. *(Repo note: see `color_pair_lut[256]` + `c64_convert_pixels_optimized()` in `src/c64-color.c`.)*

### LUT lifecycle
- LUTs are fully derived from the active palette.
- *(Repo note: the current LUT builder `c64_init_color_conversion_lut()` is one-time/never-rebuild; the palette system must introduce rebuildable LUT generation from a 16-color palette and swap the active LUT safely so the hot path stays “LUT lookup only”.)*
- LUTs MUST be rebuilt eagerly when:
  - The active palette changes
  - Any colour is edited
  - Any pixel-packing or format parameter affecting LUT structure changes
- LUT rebuilds MUST NOT occur in the hot packet-processing path.

### Runtime behaviour
- Packet processing MUST:
  - Use LUTs only
  - Contain no palette logic or branching
- Palette changes MUST affect subsequent packets immediately.

### Thread safety
- If rendering or decoding is multi-threaded:
  - Build new LUTs in a temporary buffer
  - Swap LUT pointers atomically or under a mutex
- Old LUTs may be freed after swap.

---

## E2E requirements (mandatory)

### Assertion robustness (mandatory)

- All E2E assertions (existing and new) must be resilient to **rescaling**, **different canvas resolutions**, **scan lines**, **blur/bloom**, and typical **codec artifacts**.
- Never rely on “pixel-perfect” geometry in the encoded output. Always:
  - Detect the content bounds first (exclude letterboxing).
  - Map C64-coordinate regions of interest into output pixels via scale factors.
  - Sample from conservative inner ROIs and use robust statistics (median/percentiles) rather than single pixels.
- **Tint is the only allowed exception**: it is acceptable for color-identity checks (like `palette_mapping`) to skip or not be used when tint is enabled, because tint intentionally destroys per-color identity.

### New scenarios: `ntsc_palette_vibrant` and `ntsc_palette_muted`

- Do **not** change existing scenarios like `ntsc_default_720p`.
- Add two new scenarios:
  - `tests/e2e/scenarios/ntsc_palette_vibrant/scenario.yaml`
  - `tests/e2e/scenarios/ntsc_palette_muted/scenario.yaml`
- Each must be identical to `tests/e2e/scenarios/ntsc_default/scenario.yaml` **except**:
  - It explicitly selects the target palette (**Vibrant** / **Muted**) via `overrides:` on the new palette selection setting key you introduce for the plugin.
  - It includes the new assertion `palette_mapping` in `assertions:`.

### New assertion: `palette_mapping.py` (must be strict and must fail on mismatch)

Add a new E2E assertion module `tests/e2e/assertions/palette_mapping.py` that verifies **precise** palette mapping using the **top-right 16-color watch**:

- Locate the top-right palette tile region in the decoded recording robustly (use content-bounds detection like other assertions; never hardcode absolute pixel coordinates).
- Use the authoritative tile layout from `tests/e2e/generate_packets.py` **as C64-coordinate reference** (not as output-pixel truth):
  - In C64 coordinates, the top-right corner widget is **88×56 outer** with **72×40 inner**.
  - Inner contains a **4×4 grid** of colors **0–15**, row-major (row0=0–3, row1=4–7, row2=8–11, row3=12–15).
  - In the C64 inner: **2px padding**, then 4×4 cells of **17×9**, where the solid swatch is **15×7** and the remaining **2px** on right/bottom is a black gap.
  - In the recorded output, these sizes will be scaled and may not map to exact integers; compute regions proportionally and sample conservatively.
- The algorithm MUST be resilient to rescaling + scanlines + blur/bloom (except tint):
  - For each swatch, sample a **shrunk inner ROI** (e.g., center 40–60% of the swatch) to avoid grid gaps and edge bleed.
  - Use robust color estimation per swatch (e.g., per-channel median, or median of the brightest luma quantile) so scanlines and black gaps don’t bias the estimate.
  - Use multi-frame aggregation (e.g., median across several frames after settling) to reduce codec noise.
  - If tint is enabled in the active scene/settings, the assertion should **SKIP** (and tint scenarios should not list `palette_mapping`).
- Compare against the **expected palette RGB** loaded from the active `.vpl` file for the scenario.
- The assertion must **FAIL** if any swatch exceeds the allowed per-channel delta tolerance (use a tight tolerance that still passes reliably under OBS’s NV12/H.264 pipeline; surface the tolerance in the assertion’s metrics/details). Prefer an **adaptive** tolerance derived from observed signal range, but keep it strict enough to catch real mapping errors.
- The assertion must report failures concisely (e.g., max delta + list of failing indices + a few worst offenders with expected vs observed).

### Wiring and visibility (mandatory)

- Ensure `palette_mapping` can be referenced from scenario YAML:
  - Add it to `tests/e2e/assertions/runner.py` `assertion_map`.
  - Export it in `tests/e2e/assertions/__init__.py`.
  - Document it in `tests/e2e/scenarios/README.md` under “Available Assertions”.
- Ensure any `palette_mapping` failure causes the E2E scenario to fail:
  - `python3 -m assertions --scenario ntsc_palette_vibrant ...` and `python3 -m assertions --scenario ntsc_palette_muted ...` must exit non-zero if `palette_mapping` fails.
- Ensure the `palette_mapping` result is visible in `tests/e2e/results/<scenario>/README.md` in a concise way:
  - Add a `palette_mapping` block to `validation_results.json` (via `tests/e2e/e2e.py`) and extend `tests/e2e/e2e.sh` `generate_report()` to render a short verdict line for it.

These E2Es must pass with **zero** `palette_mapping` assertion errors.

---

## Documentation updates (mandatory)

- Update any existing documentation in the repository that describes:
  - OBS properties
  - configuration files
  - shipped presets
- Extend the **top-level README** section that documents **all properties** to include:
  - The new Palette section and its controls
  - Meaning and behaviour of each control (dropdown, buttons, colour grid)
  - Where shipped palettes live (plugin-data/palettes/)
  - How to add user palettes (Load from file…, Save as…)
  - The VPL format rules (including inline `#` comment behaviour and 16-line requirement)
  - The shipped palette names: Default, Vibrant, Muted, Warm, Cool
- Ensure the README stays accurate, concise, and consistent with existing formatting and terminology.

---

## Constraints / non-goals

- No modal dialogs inside the OBS properties pane.
- No reliance on UI enabled or disabled state.
- No lazy or on-demand LUT construction.
- No changes to packet format or rendering semantics.

---

## Deliverables

Implement:
- Strict VPL parsing and validation
- Shipped palette set as defined above
- Palette INI indexing
- OBS properties UI changes
- Copy-on-write palette editing
- Immediate palette activation
- Eager LUT rebuild integration
- Documentation updates as specified

Preserve:
- Existing rendering correctness
- Existing performance characteristics

Do not introduce regressions.
