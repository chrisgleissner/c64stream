# Docker Build System for C64 Stream Plugin 🐳

This directory contains Docker-based build configurations that dramatically speed up CI builds by pre-installing heavy dependencies.

## Performance Improvement

- **Traditional Build**: ~4.5 minutes (3.5min APT packages + 1min plugin build)
- **Docker Container Build**: ~1-2 minutes (0.5min container start + 1min plugin build)
- **Speed Improvement**: ~60-75% faster

## Build Strategy

### Pre-built Container (`Dockerfile.ubuntu-build`)

Uses a pre-built base image with OBS Studio and Qt6 already installed.

**Pros:**

- Fastest builds (~1-2 minutes)
- Consistent environment
- Cached dependency installation
- Automatic rebuild when Dockerfile changes

**Cons:**

- Requires maintaining custom image
- Initial image build takes ~10 minutes
- Weekly rebuilds for security updates (automatic)

**Usage:**

```yaml
jobs:
  build:
    runs-on: ubuntu-latest
    container:
      image: ghcr.io/chrisgleissner/c64stream/ubuntu-build:latest
```

## Current Implementation

The Docker build system is integrated into the main `build-project.yaml` workflow:

- **Automatic Image Management**: Checks if Docker image exists and is recent
- **Smart Rebuilding**: Rebuilds image when Dockerfile changes or age > 7 days  
- **Transparent Integration**: Uses Docker container for Ubuntu builds seamlessly
- **No Manual Steps**: Everything works automatically

## Local Development

### Build Plugin with Docker

```bash
# Using pre-built image (matches CI environment)
docker run --rm -v $(pwd):/workspace \
  ghcr.io/chrisgleissner/c64stream/ubuntu-build:latest \
  bash -c "cd /workspace && cmake --preset ubuntu-x86_64 && cmake --build build_x86_64"
```

### Update Base Image

```bash
# Build new base image
docker build -f .github/docker/Dockerfile.ubuntu-build -t ubuntu-build .

# Test the image
docker run --rm ubuntu-build obs --version
```

## Migration Strategy

1. **Phase 1**: Keep existing VM builds, add containerized builds as optional
2. **Phase 2**: Once stable, switch main builds to containers
3. **Phase 3**: Remove VM builds, use containers exclusively

## Maintenance

- **Monthly**: Base image rebuilds (automated)
- **As needed**: Update Dockerfiles when dependencies change
- **Security**: Regular base image updates via scheduled builds
