# C64 Development Tools

This directory contains tools for building and running C64 assembly programs.

## Unified Build Script

The main tool is `c64-build.sh` - a unified script that replaces the individual build scripts that were previously in each program directory.

### Usage

```bash
# Basic build
./c64-build.sh av-sync.asm
./c64-build.sh av-sync-auto.asm
./c64-build.sh digit-cycle.asm

# Build and run in VICE emulator  
./c64-build.sh av-sync.asm --run
./c64-build.sh av-sync-auto.asm --run

# Build with verbose output
./c64-build.sh digit-cycle.asm --verbose

# Build to custom directory
./c64-build.sh av-sync.asm --output /tmp

# Install dependencies automatically
./c64-build.sh --install-deps

# Show help
./c64-build.sh --help
```

### Features

- **Automatic cleanup**: Removes any existing .prg files before building to avoid conflicts
- **Spurious file cleanup**: Automatically removes any temporary files created by build tools
- **Cross-platform**: Works on any system with 64tass installed
- **VICE integration**: Automatically detects and launches VICE emulator (x64sc, x64, or xvice)
- **Dependency management**: Can automatically install required tools (64tass, VICE)
- **Flexible output**: Build to any directory, not just alongside the .asm file
- **Clear feedback**: Color-coded output with progress information

### Dependencies

- **64tass**: C64 cross-assembler (`sudo apt install 64tass`)
- **VICE**: C64 emulator (`sudo apt install vice`) - optional, only needed for `--run`

### Available Programs

- `av-sync.asm` - Audio/Video synchronization test program
- `av-sync-auto.asm` - Automatic A/V sync pop generator (one-frame pops every 48 frames)
- `digit-cycle.asm` - Digit cycling display program

### Directory Structure

```
tools/c64/
├── c64-build.sh           # Unified build script
├── install-vice.sh        # VICE installer  
├── README.md             # Documentation
├── av-sync.asm           # Audio/Video sync test program
├── av-sync-auto.asm      # Automatic A/V sync pop generator
├── av-sync.prg           # Built program (created by build script)
├── av-sync-auto.prg      # Built program (created by build script)
├── digit-cycle.asm       # Digit cycling display program
└── digit-cycle.prg       # Built program (created by build script)
```

### Previous Build Scripts

The individual `build.sh` and `build-and-run.sh` scripts in each program directory have been removed and replaced by this unified tool. The functionality is equivalent or enhanced compared to the old scripts.
