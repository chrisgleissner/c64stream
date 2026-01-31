# Testing Strategy (Current + Feasible Extensions)

This document summarizes the existing test strategy in this repository and proposes pragmatic extensions that are realistic for local development and CI. It does not introduce new requirements beyond what the repo already enforces.

## Current Test Layers

### 1) Build verification (CMake/Ninja)
- Primary build target: `cmake --build build_x86_64`
- CI build (per .github/build-instructions.md): `.github/scripts/build-ubuntu --target ubuntu-x86_64 --config RelWithDebInfo`
- Build artifacts are validated by existence (e.g., `build_x86_64/c64stream.so`).

### 2) C/C++ unit and integration tests (CTest)
Defined in [tests/CMakeLists.txt](../tests/CMakeLists.txt) and [tests/script/CMakeLists.txt](../tests/script/CMakeLists.txt):
- Parser tests: `test_c64script_parser`
- Compiler/VM tests: `test_c64script_compiler`
- Edge cases: `test_c64script_edge_cases`
- Debug helpers: `test_c64script_debug`
- Repo-wide script validation: `test_c64script_all_scripts`
- Per-script parser regression tests for `data/scripts/*.c64script`

Run locally:
- `ctest --test-dir build_x86_64 --output-on-failure`
- Focused script validation only: `ctest --test-dir build_x86_64 -R c64script_all_scripts --output-on-failure`

### 3) Python validation tests
From [tests/script/CMakeLists.txt](../tests/script/CMakeLists.txt):
- `tests/script/test_scripts.py` is executed via CTest if Python is available.

### 4) E2E harness (Python + UDP replay)
Located in [tests/e2e](../tests/e2e):
- UDP replay tool built during CMake config: `tests/e2e/util/udp_replay.c`
- Python orchestrator and assertions for playback verification
- Includes REST-control and keymap-related tests (e.g., `test_rest_client.py`, `test_keymap.py`)

Important: E2E tests are **LOCAL ONLY** (require GUI and OBS). Do not run in CI.

## Recommended Local Workflow (Aligned with Repo Rules)

1) **Build**
   - `cmake --build build_x86_64`

2) **Unit/validation tests**
   - `./build linux --tests --script-tests`

3) **E2E (local only)**
   - `cd tests/e2e`
   - `./e2e.sh --list-scenarios`
   - Run scenario(s) as needed (GUI required)

## CI Coverage (Current Reality)
- CI validates build and test targets defined by the build scripts.
- Python validation runs when Python is available.
- E2E is intentionally not run in CI due to instability and GUI requirements.

## Practical Extensions (Feasible, Low Risk)

These additions are realistic given the current structure and toolchain, and do not require major architectural changes:

1) **Focused CTest presets**
   - Add CMake/CTest preset aliases for `script-only` and `vm-only` to standardize local workflows. This is low effort and CI-neutral.

2) **Selective E2E smoke set**
   - Define a small “smoke” subset of scenarios (2–3) in `tests/e2e/run_all_scenarios.sh` for quick local checks. This keeps local feedback fast without enabling CI E2E.

3) **Script executor thread safety checks**
   - Add a dedicated unit test in `tests/script` that exercises script actions without an OBS UI thread, using the existing stubs. This validates defensive behavior without requiring OBS to run.

4) **Playlist/automation integration checks (headless)**
   - Add a small C test in `tests/script` that creates an automation instance with stubbed config and verifies playlist selection logic. This validates playlist indexing/selection without OBS or REST dependencies.

## Non-Goals (Explicitly Out of Scope)
- Running full E2E in CI (blocked by GUI requirements and known instability).
- Adding heavy integration tests that require real hardware (C64U) in CI.

## Notes
- All test and build commands above are already part of the documented workflows in this repository (see AGENTS.md and .github/copilot-instructions.md).
- Any new tests should use existing harnesses (CTest or the e2e framework) to stay consistent with the current tooling.
