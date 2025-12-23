# Agent Guide (LLM / Copilot / Cursor)

This repository is an OBS Studio source plugin (`c64stream`) for streaming C64 Ultimate video/audio over the network.

## Source of truth

- **Primary rules & conventions**: see `.github/copilot-instructions.md`
- **Protocol documentation**: `doc/c64-stream-spec.md`
- **Build details (CI)**: `.github/build-instructions.md`

If anything in this file conflicts with `.github/copilot-instructions.md`, follow `.github/copilot-instructions.md`.

## Fast path (what to do before you open a PR)

### Build (Linux)

```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
```

### Formatting (MANDATORY)

Run clang-format using the repo wrapper (this is what CI runs):

```bash
./build-aux/run-clang-format --check
```

If it fails, format the files and re-check:

```bash
./build-aux/run-clang-format path/to/file.c path/to/file.h
./build-aux/run-clang-format --check
```

**Important**: CI requires **clang-format 21.1.1+**. Distro packages are often too old.
The wrapper auto-detects common Homebrew installs; you can also override via `CLANG_FORMAT=/path/to/clang-format`.

### Unit tests

```bash
ctest --test-dir build_x86_64 --output-on-failure
```

## E2E tests (LOCAL ONLY)

E2E tests drive a real OBS instance and validate the recorded output.

- **Do not run E2E tests in cloud/CI environments** (known instability/issues).
- Run E2E **only** on a local machine with a working graphical environment (X11/Wayland) and OBS installed.

### Basic E2E entrypoint:

```bash
cd tests/e2e
./e2e.sh --format PAL --frames 180 --verbose
```

### Running named scenarios:

```bash
# List available scenarios
./e2e.sh --list-scenarios

# Run a specific scenario (auto-sets format from scenario.yaml)
./e2e.sh --scenario ntsc_amber_monitor --verbose
```

### Assertion framework (for verifying recordings):

```bash
# Verify recording against scenario's OBS scene config
python3 assertion_framework.py \
    --mp4 results/ntsc_amber_monitor/c64_recording.mp4 \
    --scene-json scenarios/ntsc_amber_monitor/overrides/basic/scenes/C64StreamTest.json \
    --verbose
```

## What to optimize for

- Low latency and robustness (network jitter, missing packets, reconnects).
- Deterministic render/effect behavior across OBS render paths (preview/program/recording).
- Tests that are stable, fast, and don’t depend on flaky external state (especially in CI).
