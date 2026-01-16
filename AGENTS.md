# Agent Guide (LLM / Copilot / Cursor)

This repository is an OBS Studio source plugin (`c64stream`) for streaming C64 Ultimate video/audio over the network.

## Quick orientation

1. Start with `README.md` for the overall goal and repository layout.
2. For feature context, read `implementation-plan.md` and `INVESTIGATION.md` (if present) before developing your solution.
3. For any multi-hour task, open `PLANS.md` before planning changes.
4. Technical protocol details live in `doc/c64u/c64u-stream-spec.md`; cross-check there as needed.
5. Build/test workflows are defined in `.github/build-instructions.md`.
6. Use `.github/copilot-instructions.md` for the latest rules and conventions (it supersedes anything below if there is a conflict).

## Source of truth

- **Primary rules & conventions**: see `.github/copilot-instructions.md`
- **Complex feature work**: see `PLANS.md` (read at start of substantial tasks)
- **Complex research or bug fix work**: see `INVESTIGATIONS.md` (read at start of substantial tasks)
- **Streaming protocol documentation**: `doc/c64u/c64u-stream-spec.md`
- **REST API documentation**: `doc/c64u/c64u-rest-api.md` and its formal OpenAPI specification  `doc/c64u/c64u-openapi.yaml`
- **Build details (CI)**: `.github/build-instructions.md`

When this file conflicts with `.github/copilot-instructions.md`, follow `.github/copilot-instructions.md`.

## MANDATORY: Error Investigation Rule (High Priority)

**Always investigate errors, warnings, and assertion failures.**

- Attribute every error, warning, or assertion failure to recent code changes.
- Treat every issue as actionable and resolve the root cause.
- If tests show warnings after changes, fix them before proceeding.
- Declare completion only when tests are free of warnings and failures.

For complex or multi-hour tasks, read [`PLANS.md`](PLANS.md) first and follow its structure for planning and tracking work.

## Output Wording Rules

- Use short-version wording that omits the word spelled c‑o‑n‑c‑i‑s‑e.
- Use wording that avoids the word spelled C‑R‑I‑T‑I‑C‑A‑L.
- Describe only the current state of documents when changing them.

## Fast path (what to do before you open a PR)

### One-time setup: Install git hooks

```bash
# Install pre-commit hook (enforces clang-format automatically)
ln -sf ../../build-aux/git-hooks/pre-commit .git/hooks/pre-commit
```

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

When the check fails, format the files and re-check:

```bash
./build-aux/run-clang-format path/to/file.c path/to/file.h
./build-aux/run-clang-format --check
```

**Important**: CI requires **clang-format 21.1.1+**. Distro packages are often too old.
The wrapper auto-detects common Homebrew installs; you can also override via `CLANG_FORMAT=/path/to/clang-format`.

### MANDATORY: Full test suite before commit

**Always run the full test suite before committing or declaring work complete**:

```bash
./local-build.sh linux --tests --script-tests
```

This runs:
- All C/C++ unit tests (CTest)
- All C64Script validation tests (all scripts in the repository)
- All Python unit tests (E2E harness)

Commit and push only after a green local build. No exceptions.

### Unit tests (individual)

If you need to run specific test categories:

```bash
# C/C++ tests (CMake/CTest)
ctest --test-dir build_x86_64 --output-on-failure

# C64Script validation tests (ALL .c64script files in repo)
ctest --test-dir build_x86_64 -R c64script_all_scripts --output-on-failure

# Python unit tests (E2E harness)
python3 -m unittest \
    tests/e2e/util/test_network_simulation.py \
    tests/e2e/util/test_network_timing_validation.py
```

### C64Script Trace Validation (MANDATORY for script changes)

All `.c64script` files are executed during tests. Scripts with `.expected-trace.yaml` files have their execution traces validated:

- Trace limit: **1000 steps max** (prevents huge traces in repo)
- Traces record: line number, line content, variable states
- Format: YAML for readability
- Location: `<script>.expected-trace.yaml` alongside script file

### Documentation review (MANDATORY before declaring completion)

Verify relevant docs match the code/behavior you changed (especially anything in `doc/` and test docs).

## E2E tests (LOCAL ONLY)

E2E tests drive a real OBS instance and validate the recorded output.

- Run E2E tests only on a local machine with a working graphical environment (X11/Wayland) and OBS installed.

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

### Running the full E2E scenario suite (MANDATORY before declaring completion)

```bash
cd tests/e2e
scenarios=$(./e2e.sh --list-scenarios | awk -F: '/^  [a-z0-9_]+:/{print $1}' | tr -d ' ')
for s in $scenarios; do
    ./e2e.sh --scenario "$s" --duration 8 || exit 1
done
```

### Assertion framework (for verifying recordings):

```bash
# Verify recording against scenario's OBS scene config
python3 -m assertions \
    --mp4 results/ntsc_amber_monitor/c64_recording.mp4 \
    --scene-json scenarios/ntsc_amber_monitor/overrides/basic/scenes/C64StreamTest.json \
    --verbose
```

## What to optimize for

- Low latency and robustness (network jitter, missing packets, reconnects).
- Deterministic render/effect behavior across OBS render paths (preview/program/recording).
- Tests that are stable, fast, and don’t depend on flaky external state (especially in CI).

## Modularization guardrails

- When a source file grows beyond 1000 lines or mixes distinct concerns, pause and plan a split into focused modules or
    headers before adding more features.
