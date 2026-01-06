# Copilot Build Dependencies

This document describes the dependencies and setup for Copilot agent builds.

## Overview

Copilot agent builds use a minimal dependency set to ensure fast session startup. Unlike CI builds that run in a full OBS environment, Copilot builds only install what's absolutely necessary, with OBS and Qt6 dependencies downloaded automatically by the build system.

## Core Dependencies

### Essential Build Tools
- **build-essential**: GCC compiler and core development tools
- **cmake** (>= 3.28): Build system generator
- **ninja-build**: Fast build tool
- **pkg-config**: Package configuration helper
- **git**: Version control
- **ccache**: Build caching for faster rebuilds

### Required Runtime Libraries  
- **curl**: Command-line download tool (used by build scripts)
- **libcurl4-openssl-dev**: libcurl development headers (required for REST client)
- **zsh**: Z shell (required by `build-aux/run-clang-format` and `build-aux/run-gersemi`)

### Code Quality Tools
- **clang-format 21.1.1+**: Code formatting (installed via Homebrew or LLVM APT)
- **gersemi**: CMake formatting (installed via pip)

### Python Environment
- **python3**: Python interpreter
- **python3-pip**: Python package installer
- **pytest**: Test framework (optional, for Python tests)

### Optional Optimizations
- **libsimde-dev**: SIMD optimizations (improves performance)

## Installation Methods

### Automated Installation (Recommended for Copilot)

Use the provided script for automated setup:

```bash
./.github/scripts/install-copilot-deps.sh
```

This script:
- Installs minimal dependencies via APT
- Installs clang-format 21 from official LLVM APT repository
- Sets up update-alternatives for clang-format
- Installs gersemi via pip

### Manual Installation

If you prefer manual installation or need to troubleshoot:

#### 1. Core packages via APT
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake ninja-build pkg-config git \
    curl wget libcurl4-openssl-dev zsh ccache \
    python3 python3-pip libsimde-dev \
    ca-certificates gnupg lsb-release software-properties-common
```

#### 2. clang-format 21 from LLVM APT
```bash
# Install from official LLVM repository
curl -sSL https://apt.llvm.org/llvm.sh | sudo bash -s -- 21

# Set as default
sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-21 2100 --force
sudo update-alternatives --set clang-format /usr/bin/clang-format-21

# Verify
clang-format --version
```

#### 3. gersemi via pip
```bash
python3 -m pip install --user --break-system-packages gersemi
```

### Using local-build.sh

The `local-build.sh` script can auto-install dependencies:

```bash
./local-build.sh linux --install-deps
```

This installs core packages + libobs-dev. For E2E testing dependencies (OBS, xvfb, etc.):

```bash
./local-build.sh linux --install-e2e-deps
```

## Troubleshooting

### clang-format not found or wrong version

```bash
# Check current version
clang-format --version

# If < 21.1.1, install from LLVM APT:
curl -sSL https://apt.llvm.org/llvm.sh | sudo bash -s -- 21
sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-21 2100 --force
```

### zsh not found

```bash
sudo apt-get install -y zsh
```

### libcurl not found

```bash
sudo apt-get install -y curl libcurl4-openssl-dev
```

## Docker Images

### Copilot-Optimized Image

`.github/docker/Dockerfile.copilot-build` provides a minimal image:
- Only essential build tools
- clang-format 21 from official LLVM APT
- No OBS, no GUI libraries
- ~500MB vs ~2GB for full CI image

### Full CI Image  

`.github/docker/Dockerfile.ubuntu-build` provides the complete CI environment:
- All build tools
- OBS Studio from PPA
- Qt6 development libraries
- X11, audio, and E2E testing dependencies
- Used for full CI builds and E2E tests

## Dependency Comparison

| Dependency | Copilot Build | Full CI Build | E2E Testing |
|------------|---------------|---------------|-------------|
| build-essential | ✅ | ✅ | ✅ |
| cmake, ninja | ✅ | ✅ | ✅ |
| curl, libcurl-dev | ✅ | ✅ | ✅ |
| zsh | ✅ | ✅ | ✅ |
| clang-format 21 | ✅ (LLVM APT) | ✅ | ✅ |
| libobs-dev | ❌ (auto-downloaded) | ✅ | ✅ |
| obs-studio | ❌ | ✅ | ✅ |
| Qt6 | ❌ (auto-downloaded) | ✅ | ✅ |
| xvfb, X11 | ❌ | ❌ | ✅ |
| Python scipy, opencv | ❌ | ❌ | ✅ |

## Performance

### Installation Times

- **APT packages only**: ~30-60 seconds
- **With LLVM APT repository**: ~2-3 minutes (first time)
- **Subsequent runs**: ~30-60 seconds (APT cache)

### Image Sizes

- **Copilot minimal**: ~500MB
- **Full CI**: ~2GB
- **E2E testing**: ~2.5GB

## See Also

- `.github/scripts/install-copilot-deps.sh` - Automated installation script
- `.github/workflows/copilot-setup-steps.yml` - Copilot CI setup
- `.github/docker/Dockerfile.copilot-build` - Minimal Docker image
- `.github/docker/Dockerfile.ubuntu-build` - Full CI Docker image
- `local-build.sh` - Local build script with dependency management
