# Developer Guide

C64 Ultimate video/audio streaming plugin for OBS Studio.

## Prerequisites

### Properties Configuration System

The plugin uses a multi-tier configuration system to handle different environments:

1. **Production Settings** (`data/properties.ini`): Default settings for real C64 Ultimate devices
   - `c64_host=c64u`
   - `control_port=64`
   - `dns_server_ip=192.168.1.1`
   - Shipped to end users with the plugin

2. **E2E Testing Settings**:
   - `tests/e2e/properties_e2e_local.ini`: Local development E2E testing
   - `tests/e2e/properties_e2e_ci.ini`: CI environment E2E testing
   - Both use `localhost` and `control_port=6400` for the local streaming harness

### OBS Configuration Pollution Prevention

**Problem**: E2E tests temporarily copy test-specific properties to the plugin directory, but OBS also caches source settings in its scene collections. This means that even after restoring correct properties.ini, OBS still has cached test settings.

**Solution**: The `local-build.sh --install` command performs comprehensive cleanup:

1. **Properties File Reset**: Restores `data/properties.ini` with real C64 Ultimate settings
2. **OBS Scene Reset**: Clears all cached C64 Stream source settings in scene collections
3. **Profile Cleanup**: Removes E2E test profiles and scene collections

```bash
# Clear OBS scene collection settings (forces reload from properties.ini)
source['settings'] = {}  # Empty settings object
```

This ensures that after E2E tests, subsequent OBS launches load fresh settings from the correct properties.ini file.

### Development Workflow

**Clean Installation**:

```bash
./local-build.sh linux --install --clean
```

**E2E Testing**:

```bash
./local-build.sh linux --e2e --install
```

**Unit tests** (run by default in `local-build.sh`):

```bash
# Run only build + unit tests
./local-build.sh linux

# Explicitly run tests (same as default)
./local-build.sh linux --tests

# Skip tests (not recommended)
./local-build.sh linux --no-tests
```

**Post-E2E Cleanup** (automatic during install):

- Backs up any E2E properties files
- Restores production properties.ini
- Clears OBS cached settings
- Removes E2E profiles/scenes

## System Prerequisites

**Windows:**

- Visual Studio 2022 (with C++ workload)
- CMake 3.30+
- LLVM 21.1.1+ (for clang-format)

**Linux:**

```
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  gcc g++ \
  libobs-dev
```

**macOS:**

- Xcode 16.0+
- CMake 3.30+

## Quick Start

### Windows

**Option 1: Manual build**

**Configure:**

```powershell
cmake --preset windows-x64
```

**Build:**

```powershell
cmake --build build_x64 --config Debug
```

**Install to OBS:**

```powershell
New-Item -ItemType Directory -Path 'C:\ProgramData\obs-studio\plugins\c64stream\bin\64bit' -Force
Copy-Item 'build_x64\Debug\c64stream.dll' -Destination 'C:\ProgramData\obs-studio\plugins\c64stream\bin\64bit\' -Force
```

**Option 2: Convenience script (requires Git Bash)**

```cmd
local-build.bat windows --install
```

### Linux

**Configure:**

```bash
cmake --preset ubuntu-x86_64
```

**Build:**

```bash
cmake --build build_x86_64
```

**Install to OBS:**

```bash
mkdir -p ~/.config/obs-studio/plugins/c64stream/bin/64bit
cp build_x86_64/c64stream.so ~/.config/obs-studio/plugins/c64stream/bin/64bit/
```

**Convenience script with E2E testing:**

```bash
./local-build.sh linux --install          # Build and install
./local-build.sh linux --e2e --install    # Build, install, and run E2E tests
```

### macOS

**Configure:**

```bash
cmake --preset macos
```

**Build:**

```bash
cmake --build build_macos
```

**Install to OBS:**

```bash
mkdir -p "$HOME/Library/Application Support/obs-studio/plugins/c64stream/bin/64bit"
cp build_macos/c64stream.so "$HOME/Library/Application Support/obs-studio/plugins/c64stream/bin/64bit/"
```

## VS Code Development

**Quick build:** Press `Ctrl+Shift+B` (Windows) or use default build task (Linux/macOS)

**Debug with OBS:** Press `F5` - builds plugin, installs it, and launches OBS with debugger attached

**Run specific task:** `Ctrl+Shift+P` → "Tasks: Run Task"

## Code Formatting (Mandatory)

**All code must be formatted with clang-format 21.1.1+ before committing.** This ensures consistency across platforms and prevents build failures on Linux where formatting is automatically checked.

### Version Requirement

**clang-format 21.1.1 or later is required.** Latest versions (22.x, 23.x, etc.) are fully supported.

**Check your version:**

```bash
clang-format --version
# Should show: clang-format version 21.1.1 or higher
```

**Install latest clang-format:**

- **Windows:** Download latest LLVM from <https://llvm.org/builds/> (includes clang-format)
- **Linux:** `brew install clang-format` (via Homebrew)
- **macOS:** `brew install clang-format`

### Automatic Formatting

The `local-build.bat` / `local-build.sh` scripts **automatically format code** before every build:

```bash
# Linux/macOS
./local-build.sh linux

# Windows
local-build.bat windows
```

### Manual Formatting

**Format all source files:**

**Windows:**

```powershell
& "C:\Program Files\LLVM\bin\clang-format.exe" -style=file -fallback-style=none -i src/*.c src/*.h tests/*.c
```

**Linux/macOS:**

```bash
./build-aux/run-clang-format
```

**Check formatting without modifying files:**

```bash
./build-aux/run-clang-format --check    # Exits with error if formatting needed
```

### When to Format

- **Before every commit** (mandatory)
- **After editing on Windows** (Windows doesn't auto-format on save by default)
- **When switching between platforms** (to catch any platform-specific formatting drift)

**Note:** The Linux build will fail if code is not properly formatted. Always run clang-format before committing changes made on Windows.

## Testing Strategy

The c64stream plugin employs a multi-layered testing approach to ensure correctness, cross-platform compatibility, and robustness. Testing is conducted at multiple levels: unit tests, integration tests, and end-to-end validation.

### Test Layers

#### 1. Unit Tests (C)

**Purpose:** Validate individual components and data structures in isolation.

**Location:** `tests/test_*.c`

**Examples:**
- `tests/test_properties_ini_defaults.c` - Validates properties configuration system
- Tests for protocol parsing, packet handling, color conversion

**Running unit tests:**

```bash
# Build and run all unit tests
ctest --test-dir build_x86_64 --output-on-failure

# Run specific test
./build_x86_64/tests/test_properties_ini_defaults
```

**Coverage:**
- Properties system configuration and defaults
- Data structure initialization and cleanup
- Error handling and edge cases

#### 2. Python Unit Tests

**Purpose:** Validate Python-based test infrastructure and script execution framework.

**Location:** `tests/e2e/test_*.py`

**Examples:**
- `test_script_executor.py` - Tests C64 script parser and executor (mock-based)
- `test_network_simulation.py` - Network packet replay validation
- `test_network_timing_validation.py` - UDP timing accuracy tests

**Running Python tests:**

```bash
cd tests/e2e
python3 -m pytest test_*.py -v --tb=short
```

**Coverage:**
- Script command parsing and validation
- Executor state machine behavior
- REST API call sequencing
- Network timing and packet generation
- Mock-based testing of C64U interactions

#### 3. E2E Script Tests

**Purpose:** Real-world script execution scenarios for automation and REST control features.

**Location:** `tests/e2e/scripts/*.c64script`

**Test scripts:**
- `basic_automation.c64script` - Simple command sequence
- `palette_cycling.c64script` - Palette changes during execution
- `effect_showcase.c64script` - Effect transitions
- `recording_workflow.c64script` - Record start/stop automation
- `error_recovery.c64script` - Error handling scenarios
- `complex_sequence.c64script` - Multi-step automation workflow

**Running script tests:**

```bash
# Run all script tests through OBS (requires OBS with plugin installed)
cd tests/e2e
for script in scripts/*.c64script; do
    echo "Testing: $script"
    # Manual execution through OBS UI or automated harness
done
```

**Coverage:**
- Script command execution in real OBS environment
- REST API integration with C64U device
- Keyboard injection and automation
- Effect and palette changes
- Recording lifecycle management

#### 4. Integration Tests

**Purpose:** Validate interaction between components (network → protocol → rendering).

**Approach:**
- Packet replay with deterministic inputs
- Validation of rendering output against expected baselines
- Frame progression marker detection

**Tools:**
- `udp_replay` - Precise packet replay tool (built from `udp_replay/udp_replay.c`)
- `generate_packets.py` - Creates deterministic test packets with visual markers

**E2E Testing**

**Complete plugin validation:**

```bash
cd tests/e2e
./e2e.sh              # Full end-to-end test
./e2e.sh --verbose     # With detailed logging
```

**Integrated build + E2E testing (Linux only):**

```bash
./local-build.sh linux --e2e --install    # Build, install, and run E2E tests
```

See [`doc/e2e.md`](e2e.md) for comprehensive E2E testing documentation.

### End-to-end tests (in-depth)

The E2E harness validates the full path: deterministic packet generation → UDP replay → OBS + plugin processing → recording → result validation.

- Orchestrators:
  - `tests/e2e/e2e.sh` — shell wrapper for deps/build/run/report (generates `tests/e2e/test_output/README.md`)
  - `tests/e2e/e2e.py` — launches Xvfb/OBS, starts packet replay, validates results, writes `validation_results.json`
- Generators/Tools:
  - `tests/e2e/generate_packets.py` — PAL/NTSC packets with visual and audio pop markers
  - `build_x86_64/tests/e2e/udp_replay` — precise UDP timing sender

Key behaviors and correctness guards:

- FPS configuration:
  - PAL: FPSType=Common, `FPSCommon="50 PAL"`, `FPSInt=30`, `FPSNum=30` (label is critical)
  - NTSC: `FPSCommon="60"`
- CFR enforcement during compression: final MP4 is normalized to constant frame rate (50/60) to avoid 30 fps artifacts from VFR containers.
- Validation artifacts:
  - `validation_results.json` — statuses for UDP reception, frame processing, recording, duration/integrity, and `av_sync_details`
  - Recording file (mkv/mp4) — linked from the generated `README.md`
  - CSVs: `network.csv`, `obs.csv` when recorded
- A/V sync analysis (Pop synchronization):
  - Detects video and audio pops, pairs closest matches, and assigns a traffic light (green/yellow/red)
  - Summary includes sync accuracy %, average and max offset, and a channel alternation verdict
  - All timings displayed with 0.1 ms precision

Output locations:

- Human-friendly report: `tests/e2e/test_output/README.md`
- Machine-readable: `tests/e2e/test_output/validation_results.json`
- Artifacts: recording file(s), optional `network.csv` and `obs.csv`

Running locally (Linux):

```bash
./local-build.sh linux --e2e --install
```

More details and CI usage in [`doc/e2e.md`](e2e.md).

## Build Configurations

**Debug** - Full debug symbols, no optimization

```bash
cmake --preset windows-x64
cmake --build build_x64 --config Debug
```

**RelWithDebInfo** - Optimized with debug symbols (default for development)

```bash
cmake --build build_x64 --config RelWithDebInfo
```

**Release** - Full optimization, no debug symbols

```bash
cmake --build build_x64 --config Release
```

## Cross-Platform Development

### Platform Detection Patterns

**Networking (c64-network.h):**

```c
#ifdef _WIN32
    #include <winsock2.h>
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    #include <sys/socket.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE -1
#endif
```

**Data types (c64-types.h):**

```c
#ifdef _WIN32
    #ifndef __MINGW32__
        typedef long long ssize_t;
    #endif
    #define SSIZE_T_FORMAT "%lld"
#else
    #define SSIZE_T_FORMAT "%zd"
#endif
```

### Best Practices

1. Use `#ifdef _WIN32` for Windows-specific code
2. Use `socket_t` typedef instead of `SOCKET` or `int`
3. Use `SSIZE_T_FORMAT` macro for `ssize_t` printf formatting
4. Use `c64_get_socket_error()` wrapper for error codes
5. Test on multiple platforms before committing

## Build Verification

Before committing code changes:

**Linux (required):**

```bash
rm -rf build_x86_64
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
ls build_x86_64/c64stream.so  # Verify exists
```

**Windows (validated via CI):**

```powershell
Remove-Item "build_x64" -Recurse -Force -ErrorAction SilentlyContinue
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo
dir build_x64\RelWithDebInfo\c64stream.dll  # Verify exists
```

## Common Development Tasks

**Clean build:**

```bash
rm -rf build_x86_64        # Linux
rm -rf build_x64           # Windows
rm -rf build_macos         # macOS
```

**Rebuild after CMake changes:**

```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
```

**Check build errors:**

```bash
cmake --build build_x86_64 2>&1 | grep error
```

**Install dependencies (Linux):**

```bash
./install-ubuntu-deps.sh
```

## Performance Analysis

**Profile UDP receiver:**

```bash
# Build with profiling enabled
cmake --preset ubuntu-x86_64 -DENABLE_PROFILING=ON
cmake --build build_x86_64

# Run with profiler
perf record -g ./obs --profile c64stream
perf report
```

**Memory leak detection:**

```bash
valgrind --leak-check=full --show-leak-kinds=all obs --profile c64stream
```

## CI/CD Integration

The project uses GitHub Actions for:

- Multi-platform builds (Ubuntu, macOS, Windows)
- Code formatting validation
- Python unit tests (pytest)
- Package generation
- Code signing (macOS)

**CI runs:**

- CI is triggered by **committing and pushing** to GitHub.

### CI Test Coverage

**What CI validates:**
1. **Build integrity** - All platforms compile without errors/warnings
2. **Code formatting** - clang-format 21+ compliance on all source files
3. **Python unit tests** - Mock-based testing of script executor and network simulation
4. **Cross-platform compatibility** - Windows (MSVC), Linux (GCC/Clang), macOS (Clang)

**What CI does NOT validate (local testing only):**
- E2E tests with real OBS (requires GUI/X11, unstable in cloud environments)
- Manual testing scenarios (keyboard capture, script execution)
- Performance testing and profiling

**Before pushing:**
```bash
# Run formatting check
./build-aux/run-clang-format --check

# Run Python unit tests
cd tests/e2e && python3 -m pytest test_*.py -v

# Run local E2E (if making plugin behavior changes)
./local-build.sh linux --e2e --install
```

### Test Coverage Summary

| Test Type | Location | CI | Local | Purpose |
|-----------|----------|-----|-------|---------|
| C Unit Tests | `tests/test_*.c` | ✅ | ✅ | Component validation |
| Python Unit Tests | `tests/e2e/test_*.py` | ✅ | ✅ | Script executor, network simulation |
| E2E Script Tests | `tests/e2e/scripts/*.c64script` | ❌ | ✅ | Real-world automation scenarios |
| E2E Full Validation | `tests/e2e/e2e.sh` | ❌ | ✅ | End-to-end recording validation |
| Code Formatting | All `.c`/`.h` files | ✅ | ✅ | Style consistency |
| Cross-platform Builds | CI matrix | ✅ | ⚠️ | Platform compatibility |

**Coverage philosophy:**
- **CI:** Fast, deterministic tests that validate core logic and cross-platform builds
- **Local:** Full validation including GUI, rendering, and real-world scenarios
- **Both required:** CI must pass before merge, local E2E required for behavior changes

## C64Script Development

### Language Architecture

C64Script is a BASIC-inspired scripting language with modern control flow. The implementation follows a classic compiler pipeline:

1. **Lexer** (`c64-script-token.c`) - Tokenizes source text into tokens
2. **Parser** (`c64-script-parser.c`) - Builds Abstract Syntax Tree (AST) from tokens
3. **Compiler** (`c64-script-bytecode.c`) - Generates bytecode from AST
4. **Runtime** (`c64-script-runtime.c`) - Manages variables, stacks, and execution state
5. **VM** (`c64-script-vm.c`) - Executes bytecode instructions
6. **Executor** (`c64-script-executor.c`) - Manages script lifecycle in OBS context

### Script Debugging Features

The debugging system provides source-level debugging without exposing VM internals:

**Controls** (in `c64-properties.c`):
- Start/Stop - Unified button with state-aware labels
- Pause/Resume - Pauses at source-line boundaries
- Step - Executes one source line when paused
- Log variables - Dumps all variables to OBS log

**Line Tracking** (in `c64-script-vm.c`):
- `last_executed_line` - Updated after each instruction completes
- `next_line_to_execute` - Set before executing next instruction
- `source_line` field in bytecode tracks original line numbers

**Pause Implementation**:
```c
// VM checks for pause at source-line boundaries
if (runtime->should_pause && current_line != runtime->last_executed_line) {
    runtime->is_paused = true;
    while (runtime->is_paused && !runtime->should_stop) {
        os_sleep_ms(10);  // Don't busy-wait
        if (runtime->step_mode) {
            runtime->step_mode = false;
            break;  // Execute one line
        }
    }
}
```

**Wait Command Handling**:
- `WAIT` and `WAIT UNTIL` commands check `step_mode` and `is_paused`
- In debug mode, waits return immediately to avoid blocking
- Normal wait behavior resumes when script continues running

### Testing Scripts

Unit tests in `tests/test_c64script_debug.c` validate:
- Pause/resume state transitions
- Step mode execution
- Line tracking accuracy
- Variable logging
- Wait command behavior in debug mode

Example test:
```c
TEST(pause_and_resume) {
    c64script_runtime_t *runtime = c64script_runtime_create();
    runtime->should_pause = true;  // Request pause
    // VM will set is_paused when it encounters a new line
    runtime->is_paused = false;    // Resume
    c64script_runtime_destroy(runtime);
}
```

### Language Reference

- **Full Spec:** [`doc/c64script-spec.md`](c64script-spec.md) - Complete language reference
- **Debugging:** [`doc/c64script-debugging.md`](c64script-debugging.md) - Debug workflows and tips
- **Examples:** [`data/scripts/`](../data/scripts/) - Demo scripts and hello world

## Contributing

1. Fork repository
2. Create feature branch
3. Make changes
4. Format code: `./build-aux/run-clang-format`
5. Verify builds: Linux (required), Windows (CI validates)
6. Run tests: `ctest -V`
7. Submit pull request

## Resources

- **C64U Streaming Specification:** [`doc/c64-stream-spec.md`](c64-stream-spec.md)
- **E2E Testing:** [`doc/e2e.md`](e2e.md)
- **OBS Plugin Guide:** [OBS Studio Documentation](https://obsproject.com/wiki/Plugin-Development)
