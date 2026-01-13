# Copilot Instructions for c64stream

## Non-negotiables (READ FIRST)

1. **Formatting is required**: before **any push**, run:
   - `./build-aux/run-clang-format --check`
   - If it fails, run `./build-aux/run-clang-format <files...>` and re-run the check.
2. **E2E is LOCAL ONLY**: do **not** run E2E tests in cloud/CI environments (known instability). Only run E2E locally with a working GUI.
3. **Agent entrypoint**: also see `AGENTS.md` (references this file and standard workflows).
4. **Before declaring work "complete"**: do a documentation review (docs match code/behavior) and run a full local E2E scenario suite (all scenarios).
5. **Run BOTH local AND CI builds**: before any change is considered done, run:
    - Local build: `./local-build.sh linux --install --e2e-scenarios` (runs full build, unit tests, and all E2E scenarios)
    - CI build: trigger via push (do not use `act`)
   - Both must pass with zero errors before declaring completion
6. **NEVER skip tests or ignore problems**: When facing test failures, performance issues, or other problems:
   - **Do NOT** add `skip`, `ci_skip`, `@pytest.mark.skip`, or similar markers to bypass tests
   - **Do NOT** comment out failing code or tests
   - **Do NOT** reduce test coverage or assertions to make tests pass
   - **ALWAYS** fix the root cause of the problem
   - If a fix requires significant work, document the issue and create a plan, but never ship with skipped tests
   - This applies to all tests, including unit tests, integration tests, and E2E tests, as well as any environment, both local and cloud/CI
## Project Overview
OBS Studio plugin for streaming C64 Ultimate device video/audio over network. See `doc/c64u-stream-spec.md` for protocol details.

## Key Files
**Core Implementation:**
- `src/c64-source.c/h` - Main OBS source plugin
- `src/c64-network.c/h` - UDP/TCP streaming client
- `src/c64-video.c/h` - Video format conversion
- `src/c64-audio.c/h` - Audio stream processing
- `src/c64-protocol.c/h` - C64 Ultimate protocol handling
- `src/plugin-main.c` - OBS plugin entry point

**Build System:**
- `CMakePresets.json` - Platform build configurations
- `buildspec.json` - Dependencies and versions
- `build-aux/run-clang-format` - Code formatting tool

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

#### Installing clang-format 21 on Linux (recommended)

CI uses clang-format 21.x. Many Linux distros ship older versions; if `./build-aux/run-clang-format --check` reports an old/missing formatter, install clang-format 21 via Homebrew LLVM and add it to `PATH`:

```bash
# one-time Homebrew install to ~/.linuxbrew (no sudo)
git clone --depth=1 https://github.com/Homebrew/brew ~/.linuxbrew/Homebrew
mkdir -p ~/.linuxbrew/bin
ln -sf ../Homebrew/bin/brew ~/.linuxbrew/bin/brew
eval "$(~/.linuxbrew/bin/brew shellenv)"

# clang-format 21.x
brew install llvm
export PATH="$(brew --prefix llvm)/bin:$PATH"
clang-format --version
```

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
- All markdown files go in `doc/` folder (except `README.md`)
- Use kebab-case naming
- Avoid ad-hoc markdown files in the project root during development (exceptions: `README.md`, `AGENTS.md`)

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

# E2E (MANDATORY for plugin behavior changes, but LOCAL ONLY. Do NOT run in cloud/CI environments.)
# Requires a working graphical environment (X11/Wayland) and OBS installed.
./local-build.sh linux --install --e2e

# Unit tests (run by default in local-build.sh; use --no-tests to skip)
./local-build.sh linux --tests

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
- [ ] Documentation reviewed (docs reflect current behavior)
- [ ] Full local E2E scenario suite passes (LOCAL ONLY)
- [ ] Cross-platform compatibility maintained
- [ ] Code committed with clear commit message

## Cross-Platform Notes
**Networking:** Windows uses WinSock2, POSIX uses BSD sockets. Use wrapper functions in `c64-network.h`.
**File paths:** Forward slashes work everywhere, handle Windows drive letters.
**Threading:** Prefer atomic functions and semaphores from util/threading.h, falling back to pthread APIs if the code would otherwise be too complicated (available on all platforms via OBS).

Windows compatibility validated via CI - local Windows testing optional.
