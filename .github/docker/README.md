# Docker Build System for C64 Stream Plugin 🐳

This directory contains Docker-based build configurations that dramatically speed up CI builds by pre-installing heavy dependencies.

## Performance Improvement

- **Traditional Build**: ~4.5 minutes (3.5min APT packages + 1min plugin build)
- **Docker Container Build**: ~1-2 minutes (0.5min container start + 1min plugin build)
- **Speed Improvement**: ~60-75% faster

## Supported Linux Distributions

The plugin is tested on all major Linux distributions supported by OBS Studio:

- **Ubuntu 24.04** - Primary development and testing platform
- **Debian 12 (Bookworm)** - Stable enterprise Linux
- **Fedora 40** - Latest Fedora release with cutting-edge packages
- **Arch Linux** - Rolling release with latest upstream

Each distribution has a dedicated Dockerfile that pre-installs OBS Studio and all build dependencies.

## Build Strategy

### Pre-built Containers

Each distribution uses a pre-built base image with OBS Studio and Qt6 already installed:

- `Dockerfile.ubuntu-build` - Ubuntu 24.04 with OBS PPA
- `Dockerfile.debian-build` - Debian 12 with backports
- `Dockerfile.fedora-build` - Fedora 40 with RPM Fusion
- `Dockerfile.arch-build` - Arch Linux rolling release

**Pros:**

- Fastest builds (~1-2 minutes)
- Consistent environment across distributions
- Cached dependency installation
- Automatic rebuild when Dockerfile changes

**Cons:**

- Requires maintaining custom images for each distribution
- Initial image build takes ~10 minutes per distribution
- Weekly rebuilds for security updates (automatic)

**Usage:**

```yaml
jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        distro: [ubuntu, debian, fedora, arch]
    container:
      image: ghcr.io/chrisgleissner/c64stream/${{ matrix.distro }}-build:latest
```

## Current Implementation

The Docker build system is integrated into the main `build-project.yaml` workflow:

- **Multi-Distribution Matrix**: Builds and tests on Ubuntu, Debian, Fedora, and Arch Linux
- **Automatic Image Management**: Checks if Docker images exist and are recent
- **Smart Rebuilding**: Rebuilds images when Dockerfiles change or age > 7 days
- **Transparent Integration**: Uses Docker containers seamlessly with matrix strategy
- **No Manual Steps**: Everything works automatically

## Local Development

### Build Plugin with Docker

```bash
# Ubuntu (default, matches primary CI environment)
docker run --rm -v $(pwd):/workspace \
  ghcr.io/chrisgleissner/c64stream/ubuntu-build:latest \
  bash -c "cd /workspace && cmake --preset ubuntu-x86_64 && cmake --build build_x86_64"

# Debian
docker run --rm -v $(pwd):/workspace \
  ghcr.io/chrisgleissner/c64stream/debian-build:latest \
  bash -c "cd /workspace && cmake --preset ubuntu-x86_64 && cmake --build build_x86_64"

# Fedora
docker run --rm -v $(pwd):/workspace \
  ghcr.io/chrisgleissner/c64stream/fedora-build:latest \
  bash -c "cd /workspace && cmake --preset ubuntu-x86_64 && cmake --build build_x86_64"

# Arch Linux
docker run --rm -v $(pwd):/workspace \
  ghcr.io/chrisgleissner/c64stream/arch-build:latest \
  bash -c "cd /workspace && cmake --preset ubuntu-x86_64 && cmake --build build_x86_64"
```

### Run E2E Tests

```bash
# Run E2E tests on specific distribution
docker run --rm -v $(pwd):/workspace \
  ghcr.io/chrisgleissner/c64stream/ubuntu-build:latest \
  bash -c "cd /workspace/tests/e2e && ./e2e.sh PAL 250"
```

### Update Base Images

```bash
# Build specific distribution image
docker build -f .github/docker/Dockerfile.ubuntu-build -t ubuntu-build .
docker build -f .github/docker/Dockerfile.debian-build -t debian-build .
docker build -f .github/docker/Dockerfile.fedora-build -t fedora-build .
docker build -f .github/docker/Dockerfile.arch-build -t arch-build .

# Test the images
docker run --rm ubuntu-build obs --version
docker run --rm debian-build obs --version
docker run --rm fedora-build obs --version
docker run --rm arch-build obs --version
```

## E2E Testing Across Distributions

The E2E test suite runs on all supported distributions to ensure compatibility:

- **Ubuntu 24.04**: Primary test platform with OBS PPA
- **Debian 12**: Tests stable/LTS compatibility
- **Fedora 40**: Tests latest packages and libraries
- **Arch Linux**: Tests rolling release and upstream compatibility

Each distribution runs the same E2E test suite but may have distribution-specific configurations in `tests/e2e/config/<distro>/`.

## Migration Strategy

1. **Phase 1 (Current)**: Multi-distribution support with Ubuntu as primary ✅
2. **Phase 2**: Expand E2E coverage to all distributions
3. **Phase 3**: Add distribution-specific packaging (DEB, RPM, PKG)

## Maintenance

- **Weekly**: Base image rebuilds for security updates (automated)
- **Monthly**: Dependency version reviews
- **As needed**: Update Dockerfiles when OBS Studio or dependencies change
- **Per release**: Test on all distributions before tagging
