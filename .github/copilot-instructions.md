# Copilot Instructions for c64stream

## Project Overview
OBS Studio plugin for streaming C64 Ultimate device video/audio over network. See `doc/c64-stream-spec.md` for protocol details.

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
- No markdown files in project root during development

## Linux Build (MANDATORY)

### For CI/GitHub Actions Builds

**See [.github/build-instructions.md](.github/build-instructions.md) for complete CI build instructions.**

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
```

**Checklist:**
- [ ] Linux build succeeds without warnings
- [ ] Code formatting passes
- [ ] CMake formatting passes
- [ ] Workflow validation passes (if .github/workflows/ modified)
- [ ] Cross-platform compatibility maintained
- [ ] Code committed with clear commit message

## Cross-Platform Notes
**Networking:** Windows uses WinSock2, POSIX uses BSD sockets. Use wrapper functions in `c64-network.h`.
**File paths:** Forward slashes work everywhere, handle Windows drive letters.
**Threading:** Prefer atomic functions and semaphores from util/threading.h, falling back to pthread APIs if the code would otherwise be too complicated (available on all platforms via OBS).

Windows compatibility validated via CI - local Windows testing optional.
