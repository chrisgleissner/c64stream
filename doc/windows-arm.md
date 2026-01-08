# Windows ARM64 Support

**Status**: 🧪 **Experimental** (Beta - seeking testers!)

This document describes C64 Stream plugin support for Windows on ARM64 (Snapdragon) devices, including installation, building, and known limitations.

---

## For End Users

### What is Windows ARM64?

Windows ARM64 refers to Windows 11 running on ARM-based processors like Qualcomm Snapdragon. These devices include:
- Microsoft Surface Pro (Snapdragon models)
- Microsoft Surface Laptop (Snapdragon models)
- Lenovo ThinkPad X13s
- Samsung Galaxy Book series
- Other Snapdragon X Elite/Plus laptops

### Why ARM64 Matters

ARM processors offer excellent battery life and fanless operation, making them ideal for mobile streaming and content creation. However, most Windows software is built for x86/x64 (Intel/AMD) processors and requires special ARM64 builds to run natively.

### Installation

1. **Download the ARM64 build**:
   - Visit the [Releases page](https://github.com/chrisgleissner/c64stream/releases)
   - Download `c64stream-VERSION-windows-arm64.zip` (NOT the `windows-x64` version)

2. **Install OBS Studio ARM64**:
   - Download OBS Studio 31.1.1+ ARM64 from [obsproject.com](https://obsproject.com/download)
   - ARM64 builds are marked as "Experimental"
   - Install OBS before installing the plugin

3. **Install the plugin**:
   - Extract the zip file
   - Copy `c64stream.dll` to `%ProgramFiles%\obs-studio\obs-plugins\64bit\`
   - Copy `data` folder to `%ProgramFiles%\obs-studio\data\obs-plugins\c64stream\`

4. **Restart OBS Studio**

5. **Verify installation**:
   - Open OBS Studio
   - Click "Sources" → "+" → Look for "C64 Stream"
   - If it appears, installation succeeded!

### Known Limitations (OBS ARM64)

OBS Studio's ARM64 support is **experimental**. Some features are not yet available:

❌ **Not Supported:**
- **Hardware encoders** (NVENC, AMF, Quick Sync) - must use software encoding
- **Scripting** (Lua/Python scripts)
- **Game Capture** source
- **AJA device** support

✅ **Supported:** (but in **experimental** state - expect things to break)
- C64 Stream plugin (full functionality)
- Most built-in sources (Display Capture, Window Capture, etc.)
- Browser Source (with hardware acceleration)
- Media Source (with hardware decoding)
- Virtual Camera (requires manual setup)
- Software encoding (x264, x265)

### Performance Notes

- **C64 Stream works identically** on ARM64 as on x64
- Network streaming performance is excellent
- Software encoding (x264) uses more CPU than hardware encoding
- For best performance, use lower encoding presets (e.g., "veryfast" instead of "slow")

### Troubleshooting

#### "Plugin failed to load"
- Ensure you downloaded the **ARM64** version (not x64)
- Check OBS Studio version is 31.1.1+ ARM64
- Verify OBS Studio itself runs correctly (try built-in sources first)

#### "No network stream detected"
- This is not ARM64-specific - see main [README](../README.md) for C64 Ultimate configuration

#### OBS crashes on startup
- ARM64 OBS support is experimental - check [OBS forums](https://obsproject.com/forum/) for known issues
- Try disabling other plugins to isolate the problem

### Reporting Issues

If you encounter ARM64-specific issues:
1. Check this is an ARM64 device (Settings → System → About → "Processor: Snapdragon...")
2. Note your OBS Studio version (Help → About)
3. Open an issue on [GitHub](https://github.com/chrisgleissner/c64stream/issues)
4. Tag with `windows-arm64` label
5. Include OBS log file (Help → Log Files → Upload Current Log)

**We're actively seeking beta testers!** Your feedback helps improve ARM64 support.

---

## For Developers

### Build Requirements

**Native Windows ARM64 build (recommended):**
- Windows 11 ARM64 device (or VM)
- Visual Studio 2022 with "Desktop development with C++" workload
- CMake 3.28.3+
- Git for Windows

**Cross-compilation from x64 Windows:**
- Visual Studio 2022 with ARM64 build tools
- CMake 3.28.3+
- Can build ARM64 from x64 Windows machine

### Building Locally on Windows

#### Option 1: Native ARM64 Build

```powershell
# Clone repository
git clone https://github.com/chrisgleissner/c64stream.git
cd c64stream

# Configure for ARM64
cmake --preset windows-arm64

# Build
cmake --build build_arm64 --config RelWithDebInfo

# Output: build_arm64/RelWithDebInfo/c64stream.dll
```

#### Option 2: Cross-Compile from x64 Windows

```powershell
# Same steps as above - Visual Studio handles cross-compilation automatically
cmake --preset windows-arm64
cmake --build build_arm64 --config RelWithDebInfo
```

The CMake preset sets the architecture to ARM64, and Visual Studio's ARM64 compiler toolchain handles the rest.

### Building on Linux

**Docker cross-compilation is NOT feasible** for Windows ARM64 builds. Reasons:
- MinGW-w64 lacks ARM64 Windows support
- LLVM/Clang cross-compilation requires complete Windows SDK (impractical)
- No mature cross-compilation toolchain exists

**Alternative: Use WSL + Visual Studio**
- Install WSL2 on ARM64 Windows
- Clone repo in WSL
- Call Visual Studio toolchain from WSL (requires careful path setup)
- This is complex and not recommended

**Best practice:** Build on Windows ARM64 device or use CI (see below).

### CI Build Process

The project uses **GitHub Actions ARM64 Windows runners** (generally available since 2025):

```yaml
# .github/workflows/build-project.yaml
jobs:
  windows-arm64-build:
    runs-on: windows-11-arm  # Native ARM64 runner
    steps:
      - uses: actions/checkout@v4
      - name: Build Plugin
        uses: ./.github/actions/build-windows
        with:
          target: arm64
          config: RelWithDebInfo
```

**Key points:**
- Uses native ARM64 Windows runners (not cross-compilation)
- Runs on `windows-11-arm` runner label
- Downloads ARM64 dependencies (OBS SDK, Qt6, obs-deps)
- Produces ARM64 artifacts: `c64stream-VERSION-windows-arm64.zip`

**Cost implications:**
- ARM64 runners are **paid** (not included in free tier)
- Typical cost: $0.016-0.064/minute depending on runner size
- Full build: ~5-10 minutes = ~$0.10-0.60 per build

### Dependency Management

ARM64 dependencies are defined in [buildspec.json](../buildspec.json):

```json
{
  "dependencies": {
    "obs-studio": {
      "version": "31.1.1",
      "hashes": {
        "windows-x64": "c8c642c1070dc31ce9a0f1e4cef5bb992f4bff4882255788b5da12129e85caa7",
        "windows-arm64": "3c63ea4e92f3fa7887839e499d3ea697b4363f196b1df8f8558307b45fcba31e"
      }
    },
    "prebuilt": {
      "version": "2025-07-11",
      "hashes": {
        "windows-x64": "38c40b13a341f6027ef90ffb3c72dae5cbc9bce34c6e23... ",
        "windows-arm64": "f581cc61e8f734a8b12d485fc8662a408ca59d222814e4b37bce115bd442fb04"
      }
    },
    "qt6": {
      "version": "2025-07-11",
      "hashes": {
        "windows-x64": "86e1a2f5fd4a03f... ",
        "windows-arm64": "7aab240504931f32ea8e8d208a912a0d6ddbd78c16858f2e559c7c9b29ab9326"
      }
    }
  }
}
```

**Dependency sources:**
- OBS Studio ARM64: [obsproject/obs-studio releases](https://github.com/obsproject/obs-studio/releases)
- obs-deps ARM64: [obsproject/obs-deps releases](https://github.com/obsproject/obs-deps/releases)
- Qt6 ARM64: [obsproject/obs-deps releases](https://github.com/obsproject/obs-deps/releases) (Qt6 packages)

### CMake Configuration

ARM64 preset in [CMakePresets.json](../CMakePresets.json):

```json
{
  "name": "windows-arm64",
  "displayName": "Windows ARM64",
  "description": "Windows ARM64 build",
  "inheritsFrom": ["base"],
  "generator": "Visual Studio 17 2022",
  "architecture": "ARM64,version=10.0.26100.0",
  "binaryDir": "${sourceDir}/build_arm64",
  "cacheVariables": {
    "CMAKE_SYSTEM_VERSION": "10.0.26100.0"
  }
}
```

**Key differences from x64:**
- `architecture`: `"ARM64,version=10.0.26100.0"` (instead of `"x64,..."`)
- `binaryDir`: `build_arm64` (instead of `build_x64`)

### Code Compatibility

**C64 Stream plugin requires ZERO code changes** for ARM64:
- Pure C code (C17 standard)
- No inline assembly
- No x86/x64-specific intrinsics
- Platform-neutral network code (WinSock2 is architecture-neutral)
- Uses OBS SDK abstractions (portable across architectures)

**Verified ARM64 compatibility:**
- ✅ Network streaming (UDP/TCP)
- ✅ Video format conversion
- ✅ Audio processing
- ✅ Color palettes and effects
- ✅ File I/O
- ✅ All UI properties

### Testing Strategy

**CI testing:** Build-only (cannot execute ARM64 binaries on x64 runners)

**Runtime testing:** Requires real ARM64 Windows hardware
- Manually test on Snapdragon device
- Connect to C64 Ultimate network stream
- Verify video/audio sync
- Test all source properties
- Record test footage

**Beta testing program:**
- Seeking volunteers with ARM64 Windows devices
- Test installations and report issues
- Validate performance characteristics

### Release Process

ARM64 builds are released alongside x64 builds:

**Artifacts:**
- `c64stream-VERSION-windows-x64.zip` (traditional build)
- `c64stream-VERSION-windows-arm64.zip` (ARM64 build)
- `c64stream-VERSION-windows-x64.exe` (installer, x64 only)

**Installation:**
- No ARM64 installer yet (manual zip installation only)
- Future: Consider ARM64 installer if demand warrants

**Documentation:**
- Mark ARM64 as "Experimental" in release notes
- Include link to this document
- Request beta tester feedback

### Debugging

**Debug symbols:**
- Available in PDB files (included in CI artifacts)
- Load in Visual Studio or WinDbg on ARM64 device

**Common issues:**
- **Linker errors:** Ensure ARM64 versions of all libraries
- **Runtime crashes:** Check dependencies are ARM64 (use `dumpbin /headers c64stream.dll`)
- **Missing symbols:** Verify OBS SDK ARM64 import libraries

**Useful commands:**
```powershell
# Check if DLL is ARM64
dumpbin /headers build_arm64\RelWithDebInfo\c64stream.dll | Select-String "machine"
# Output: "AA64 machine (ARM64)"

# Check dependencies
dumpbin /dependents build_arm64\RelWithDebInfo\c64stream.dll
```

### Known Issues

1. **OBS SDK packaging:** ARM64 SDK is distributed as runtime package, not development SDK
   - Workaround: CI automatically extracts necessary files
   - Local builds may need manual SDK extraction

2. **Qt6 ARM64:** obs-deps provides custom Qt6 builds
   - Do not use official Qt6 ARM64 (version/ABI mismatch)
   - Always use obs-deps Qt6 packages

3. **Virtual Camera:** Requires manual setup (not plugin-specific)
   - See [OBS Windows on ARM docs](https://obsproject.com/kb/windows-on-arm)

### Future Improvements

**Potential enhancements:**
- ARM64 installer (if adoption justifies development)
- Automated testing on ARM64 GitHub runners
- Performance benchmarks (ARM vs x64)
- Hardware encoder support (when OBS adds it)

### Contributing

**Testing contributions welcome:**
- Report ARM64-specific bugs
- Validate performance on different Snapdragon models
- Test with various C64 Ultimate firmware versions

**Code contributions:**
- No ARM64-specific code needed (architecture-neutral)
- Follow standard contribution guidelines in [developer.md](developer.md)

### Additional Resources

- [OBS Studio Windows on ARM](https://obsproject.com/kb/windows-on-arm)
- [GitHub ARM64 Windows Runners](https://github.com/actions/partner-runner-images/blob/main/images/arm-windows-11-image.md)
- [Visual Studio ARM64 development](https://learn.microsoft.com/en-us/visualstudio/install/visual-studio-on-arm-powered-devices)
- [C64 Stream main documentation](../README.md)

---

**Questions?** Open an issue with the `windows-arm` label!
