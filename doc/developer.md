# Developer Guide

C64 Ultimate video/audio streaming plugin for OBS Studio.

## Prerequisites

**Windows:**

- Visual Studio 2022 (with C++ workload)
- CMake 3.30+
- LLVM 21.1.1+ (for clang-format)

**Linux:**

- build-essential
- cmake 3.28+
- zsh
- clang-format 21.1.1+

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

## Unit Testing

**Run all tests:**

```bash
cd build_x86_64  # or build_x64 on Windows, build_macos on macOS
ctest -V
```

**Run specific test:**

```bash
./test_vic_colors           # Color conversion tests
./test_hostname_resolution  # DNS resolution tests
./test_enhanced_dns         # Enhanced DNS tests
```

**Mock C64 Ultimate server:**

```bash
./c64_mock_server --port 11000
```

## E2E Testing

**Complete plugin validation:**

```bash
cd tests/e2e
./e2e.sh              # Full end-to-end test
./e2e.sh --verbose     # With detailed logging
```

See [`doc/e2e.md`](e2e.md) for comprehensive E2E testing documentation.

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
- Package generation
- Code signing (macOS)

**Local CI simulation:**

```bash
# Requires 'act' tool: https://github.com/nektos/act
act -j ubuntu-build
```

## Contributing

1. Fork repository
2. Create feature branch
3. Make changes
4. Format code: `./build-aux/run-clang-format`
5. Verify builds: Linux (required), Windows (CI validates)
6. Run tests: `ctest -V`
7. Submit pull request

## Resources

- **Specification:** [`doc/c64-stream-spec.md`](c64-stream-spec.md)
- **E2E Testing:** [`doc/e2e.md`](e2e.md)
- **C64 Ultimate Docs:** [Data Streams](https://1541u-documentation.readthedocs.io/en/latest/data_streams.html)
- **OBS Plugin Guide:** [OBS Studio Documentation](https://obsproject.com/wiki/Plugin-Development)
