# Dockerized Build System Migration Guide

## Overview 🎯

The C64 Stream project now supports **Dockerized builds** that dramatically reduce build times by using pre-built containers with OBS Studio and Qt6 dependencies already installed.

## Performance Improvement 🚀

- **Traditional Build**: ~4.5 minutes (3.5min APT packages + 1min plugin build)
- **Dockerized Build**: ~1-2 minutes (0.5min container start + 1min plugin build)
- **Speed Improvement**: 60-75% faster builds

## What's Changed

### Simplified Workflow Structure

**Single Build Workflow**: `build-project.yaml` - Consolidated workflow with Docker-based Ubuntu builds and integrated E2E testing

**Entry Point Workflows**:

- **`push.yaml`** - Triggers builds on push to master/main/release branches
- **`dispatch.yaml`** - Manual build dispatch
- **`pr-pull.yaml`** - Pull request builds
- **`check-format.yaml`** - Code formatting validation

### Container Infrastructure

- **Pre-built Image**: `ghcr.io/chrisgleissner/c64stream/ubuntu-build:latest`
- **Monthly Updates**: Automatic rebuilds for security updates
- **Multi-platform**: Supports both containerized and traditional builds

## Migration Impact

### ✅ What Works Out of the Box

- **Faster CI Builds**: Immediate 60-75% speed improvement
- **Same Artifacts**: Produces identical build outputs
- **Backward Compatible**: Traditional builds still available
- **E2E Testing**: Both containerized and traditional E2E tests supported

### ⚠️ What to Watch For

- **Container Registry**: Requires GitHub Container Registry access
- **First Run**: Initial container pull takes ~30 seconds
- **E2E Tests**: Containerized E2E may have different display behavior

### 🔄 Fallback Strategy

If containerized builds fail, you can:

1. Set `use_containerized: false` in workflow inputs
2. Use traditional build workflows directly
3. Local builds continue to work unchanged

## Usage Examples

### Local Development

```bash
# Build using local Docker (fastest)
docker run --rm -v $(pwd):/workspace \
  ghcr.io/chrisgleissner/c64stream/ubuntu-build:latest \
  bash -c "cd /workspace && cmake --preset ubuntu-x86_64 && cmake --build build_x86_64"

# Traditional local build (unchanged)
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
```

### Workflow Configuration

```yaml
# Standard build with E2E testing
uses: ./.github/workflows/build-project.yaml
with:
  run_e2e: true
  e2e_format: 'PAL'
  e2e_frames: '250'

# Build without E2E testing
uses: ./.github/workflows/build-project.yaml
with:
  run_e2e: false
```

## Monitoring & Validation

### Build Time Tracking

Monitor these metrics to validate improvements:

- **Ubuntu Build Job**: Should be ~1-2 minutes (was ~4.5 minutes)
- **E2E Test Setup**: Should be ~30 seconds (was ~3.5 minutes)
- **Container Pull Time**: ~30 seconds on first run, cached afterwards

### Health Checks

The system includes automatic validation:

- Container image functionality tests
- Plugin build verification
- E2E test artifact collection
- Fallback to traditional builds on failure

## Troubleshooting

### Container Issues

```bash
# Test container manually
docker run --rm ghcr.io/chrisgleissner/c64stream/ubuntu-build:latest obs --version

# Check container registry access
docker login ghcr.io -u ${{ github.actor }} -p ${{ secrets.GITHUB_TOKEN }}
```

### Build Failures

1. Check container logs in GitHub Actions
2. Verify base image is up to date
3. Fall back to traditional builds
4. Check dependency compatibility

### E2E Issues

1. Review X11/display setup in containers
2. Check artifact upload/download
3. Compare with traditional E2E results
4. Verify plugin installation paths

## Future Improvements

- [ ] Windows containerized builds (requires Windows containers)
- [ ] macOS virtualized builds (requires different approach)
- [ ] Local development container setup
- [ ] Build cache optimization
- [ ] Multi-architecture container support
