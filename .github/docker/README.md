# Docker Build System for C64 Stream Plugin 🐳

This directory contains Docker-based build configurations that dramatically speed up CI builds by pre-installing heavy dependencies.

## Performance Improvement

- **Traditional Build**: ~4.5 minutes (3.5min APT packages + 1min plugin build)
- **Docker Container Build**: ~1-2 minutes (0.5min container start + 1min plugin build)
- **Speed Improvement**: ~60-75% faster

## Build Strategies

### 1. Pre-built Container (`Dockerfile.ubuntu-build`)

Uses a pre-built base image with OBS Studio and Qt6 already installed.

**Pros:**

- Fastest builds (~1-2 minutes)
- Consistent environment
- Cached dependency installation

**Cons:**

- Requires maintaining custom image
- Initial image build takes ~10 minutes
- Monthly rebuilds needed for security updates

**Usage:**

```yaml
jobs:
  build:
    runs-on: ubuntu-latest
    container:
      image: ghcr.io/chrisgleissner/c64stream/ubuntu-build:latest
```

### 2. Multi-stage Build (`Dockerfile.multi-stage`)

Builds dependencies and plugin in separate stages for optimal caching.

**Pros:**

- No custom base image needed
- Docker layer caching optimizes builds
- Self-contained build process

**Cons:**

- Slightly slower than pre-built (~2-3 minutes)
- More complex Dockerfile

**Usage:**

```bash
docker build -f .github/docker/Dockerfile.multi-stage --target plugin-build .
```

## Workflows

### Build Docker Images (`build-docker-images.yaml`)

- Builds and publishes the base Ubuntu build image
- Runs monthly to get security updates
- Triggered by changes to Docker files

### Containerized Build (`containerized-build.yaml`)

- Fast plugin builds using multi-stage Docker
- Alternative to traditional VM-based builds
- Produces same artifacts as main build

## Local Development

### Build Plugin with Docker

```bash
# Using pre-built image (fastest)
docker run --rm -v $(pwd):/workspace \
  ghcr.io/chrisgleissner/c64stream/ubuntu-build:latest \
  bash -c "cd /workspace && cmake --preset ubuntu-x86_64 && cmake --build build_x86_64"

# Using multi-stage build (self-contained)
docker build -f .github/docker/Dockerfile.multi-stage --target plugin-build -t c64stream-build .
docker run --rm -v $(pwd)/output:/output c64stream-build cp /workspace/build_x86_64/c64stream.so /output/
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