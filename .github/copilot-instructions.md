# Copilot Instructions for c64stream

## Non-negotiables (READ FIRST)

1. **Formatting is required**: before **any push**, run:
   - `./build-aux/run-clang-format --check`
   - If it fails, run `./build-aux/run-clang-format <files...>` and re-run the check.
2. **E2E is LOCAL ONLY**: do **not** run E2E tests in cloud/CI environments (known instability). Only run E2E locally with a working GUI.
3. **Agent entrypoint**: also see `AGENTS.md` (references this file and standard workflows).
4. **Before declaring work "complete"**: review the documentation that your changes touch (`doc/`, `docs/`, README/AGENTS) and, for runtime-behavior changes, run the entire local E2E scenario suite (note spin-up trade-offs in your summary if you skip it).
5. **Run the prescribed local build and let CI verify**: execute `./local-build.sh linux --install --e2e-scenarios` locally until it succeeds, then push so GitHub Actions can run the CI build (do not rely on `act` or other shortcuts). Both runs must exit without errors before labeling the work as done.
6. **NEVER skip tests or ignore problems**: When facing test failures, performance issues, or other problems:
   - **Do NOT** add `skip`, `ci_skip`, `@pytest.mark.skip`, or similar markers to bypass tests
   - **Do NOT** comment out failing code or tests
   - **Do NOT** reduce test coverage or assertions to make tests pass
   - **ALWAYS** fix the root cause of the problem
   - If a fix requires significant work, document the issue and create a plan, but never ship with skipped tests
   - This applies to all tests, including unit tests, integration tests, and E2E tests, as well as any environment, both local and cloud/CI
## Quick discovery

- **High-level context**: `README.md`, `implementation-plan.md`, `INVESTIGATION.md`, and `IMPLEMENTATION_SUMMARY.md` (REST control snapshot) describe the product vision, planned work, and ongoing research.
- **Planning**: `PLANS.md` (multi-hour requests) and `AGENTS.md` (workflow rules) guide how you should work.
- **Documentation**: `doc/` hosts technical references (including `doc/c64u/c64u-stream-spec.md` for protocol details) and `docs/` is the website; add new docs there instead of scattering markdown elsewhere.
- **Tests and scripts**: `tests/` houses the validation suites and `build-aux/` provides helper scripts such as formatting and validation helpers.
- **CI context**: `.github/build-instructions.md` and the workflow YAML files describe the required CI behaviors.

## Project Overview
OBS Studio plugin for streaming C64 Ultimate device video/audio over network. See `doc/c64u/c64u-stream-spec.md` for protocol details.

## Key Files
**Core Implementation:**
- `src/c64-source.c/h` - OBS source (render, properties, lifecycle)
- `src/c64-network*.c/h` - UDP/TCP streaming client and buffering
- `src/c64-video.c/h` - Video format conversion
- `src/c64-audio.c/h` - Audio stream processing
- `src/c64-protocol.c/h` - C64 Ultimate protocol handling
- `src/c64-rest-client.c/h` - REST control client
- `src/c64-automation.c` - Automation/preset helpers
- `src/c64-script-*.c/h` - Script parser, bytecode/VM, runtime, executor
- `src/plugin-main.c` - OBS plugin entry point

**Build System:**
- `CMakePresets.json` - Platform build configurations
- `buildspec.json` - Dependencies and versions
- `build-aux/run-clang-format` - Code formatting tool
- `build-aux/run-gersemi` - CMake formatting tool
- `.github/scripts/build-ubuntu` - GitHub Actions/Copilot build entrypoint

## Code Guidelines

### Core Principles (MANDATORY)
1. **Performance** - Low latency, preallocated buffers, atomic operations over locks
2. **Robustness** - Handle network failures, validate inputs, proper error handling
3. **Simplicity** - Clear code, avoid overengineering, KISS principle
4. **Consistency** - Follow existing patterns, DRY principle
5. **Cross-Platform** - Ensure compatibility with Linux and Windows
6. **Maintainability** - Write clear, self-explanatory, and well-documented code whilst avoiding redundant comments.

### Code Formatting (MANDATORY)
- **ALWAYS run `./build-aux/run-clang-format` after ANY code changes**
- Must pass clang-format 21.1.1+ validation
- 4 spaces indentation, 120 char limit, files end with newline
- **Never commit code that fails clang-format**

#### Installing clang-format 21 on Linux (for local development)

**Note:** Copilot agent builds do NOT install clang-format. Formatting is automatically skipped when clang-format is unavailable. Code formatting is validated in CI only.

For local development with formatting:

```bash
# Install clang-format 21 from official LLVM repository
curl -sSL https://apt.llvm.org/llvm.sh | sudo bash -s -- 21
sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-21 2100
```

Alternatively, the automated Copilot setup installs minimal dependencies only:

```bash
# Minimal install (no clang-format)
./.github/scripts/install-copilot-deps.sh
```

For manual installation or troubleshooting, see `.github/COPILOT_DEPENDENCIES.md`.

### License Header (Required)
```c
/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
```

### Documentation
- Keep documentation Markdown inside `doc/` (technical references) or `docs/` (site content). Root-level Markdown should be limited to `README.md`, `AGENTS.md`, `implementation-plan.md`, and other high-level summaries such as `INVESTIGATION.md` or `IMPLEMENTATION_SUMMARY.md` (when relevant).
- Prefer kebab-case filenames for any new documentation to stay consistent with existing styles.

## Linux Build (MANDATORY)

### For CI/GitHub Actions Builds

**See [./build-instructions.md](./build-instructions.md) for complete CI build instructions.**

When working in a Copilot session with GitHub Actions runner:
1. Follow the build instructions in `.github/build-instructions.md`
2. Use the build script: `.github/scripts/build-ubuntu --target ubuntu-x86_64 --config RelWithDebInfo`
3. Run in a **"build → verify logs → fix issues"** loop
4. **Only announce completion after build passes with zero errors**
5. Never terminate a Copilot session with a failing build

### For Local Development

#### Prerequisites
- CMake 3.28.3+, build-essential, ninja-build, pkg-config
- clang-format 21.1.1+ for code formatting
- zsh for build scripts

#### Quick Build
```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
```

#### Dependencies
Auto-downloaded from `buildspec.json`: OBS Studio SDK 31.1.1, obs-deps, Qt6 (optional).
Cached in `.deps/` directory.

### Validation (MANDATORY before completion)
```bash
# Clean build test
rm -rf build_x86_64
cmake --preset ubuntu-x86_64
cmake --build build_x86_64

# Verify success
if [ -f "build_x86_64/c64stream.so" ]; then
    echo "✅ Build successful"
else
    echo "❌ Build failed - DO NOT proceed"
fi

# Format validation
./build-aux/run-clang-format --check
./build-aux/run-gersemi --check

# Workflow validation (MANDATORY for any .github/workflows/ changes)
./build-aux/validate-workflows

# MANDATORY: Full test suite before commit
./local-build.sh linux --tests --script-tests

# E2E (MANDATORY for plugin behavior changes, but LOCAL ONLY. Do NOT run in cloud/CI environments.)
# Requires a working graphical environment (X11/Wayland) and OBS installed.
./local-build.sh linux --install --e2e

# Full E2E scenario suite (LOCAL ONLY) - run all scenarios
cd tests/e2e
scenarios=$(./e2e.sh --list-scenarios | awk -F: '/^  [a-z0-9_]+:/{print $1}' | tr -d ' ')
for s in $scenarios; do
    ./e2e.sh --scenario "$s" --duration 8 || exit 1
done
```

**Checklist:**
- [ ] Linux build succeeds without warnings
- [ ] Code formatting passes
- [ ] CMake formatting passes
- [ ] Workflow validation passes (if .github/workflows/ modified)
- [ ] **Full test suite passes: `./local-build.sh linux --tests --script-tests`**
- [ ] Documentation reviewed (docs reflect current behavior)
- [ ] Full local E2E scenario suite passes if behavior changed (LOCAL ONLY)
- [ ] Cross-platform compatibility maintained
- [ ] Code committed with clear commit message

## Cross-Platform Notes
**Networking:** Windows uses WinSock2, POSIX uses BSD sockets. Use wrapper functions in `c64-network.h`.
**File paths:** Forward slashes work everywhere, handle Windows drive letters.
**Threading:** Prefer atomic functions and semaphores from util/threading.h, falling back to pthread APIs if the code would otherwise be too complicated (available on all platforms via OBS).

Windows compatibility validated via CI - local Windows testing optional.
