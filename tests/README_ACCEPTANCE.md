# C64 Stream Acceptance Test

## Overview

This directory contains acceptance tests for the C64 Stream plugin. The tests verify that the plugin builds correctly, integrates with the mock server, and can handle typical streaming scenarios.

## Test Structure

### acceptance_test.sh (Smoke Test)

A lightweight smoke test that verifies:
- ✅ Plugin builds successfully
- ✅ Mock server can start and send UDP packets
- ✅ Plugin exports correct OBS symbols
- ✅ Plugin binary size is reasonable

**Usage:**
```bash
cd /home/runner/work/c64stream/c64stream
./tests/acceptance_test.sh
```

**Requirements:**
- Built plugin (`build_x86_64/c64stream.so`)
- Mock server capability (Linux only, uses pthread)
- Standard Unix tools (stat, nm, grep)

**Expected Output:**
```
=== C64 Stream Plugin - Smoke Test ===
[INFO] Setting up test environment...
[✓] Test environment created
[INFO] Building plugin...
[✓] Plugin built successfully
[INFO] Building mock server...
[✓] Mock server built successfully
[INFO] Starting mock server...
[✓] Mock server started
[INFO] Running plugin smoke test...
[✓] Smoke test period completed
===  Running Validation ===
[TEST] Checking plugin binary...
[✓] Plugin binary exists
[TEST] Checking mock server logs...
[✓] Mock server started successfully
[✓] All smoke tests passed! ✨
```

### Docker-Based Integration Test (TODO)

A full integration test that:
- Builds plugin in Docker
- Starts mock server
- Starts OBS in Docker with plugin loaded
- Enables all recording options (video, audio, CSV)
- Waits ~5 seconds while monitoring OBS logs
- Gracefully terminates
- Validates recorded files

**Planned Features:**
- OBS running in Docker with Xvfb
- Plugin loaded with all recording enabled
- Validation of:
  - BMP frame files
  - Network CSV logs
  - OBS logs for errors
  - File sizes and content

**Implementation Notes:**
- Need Dockerfile with OBS Studio + dependencies
- Mount plugin and recording directories as volumes
- Use `--network host` to connect to mock server
- Create pre-configured OBS profile/scene
- Parse OBS logs to detect plugin initialization

## Running Tests

### In CI Environment

The smoke test runs automatically during builds when `ENABLE_TESTS=ON` (default for local builds).

### Locally

```bash
# Run smoke test
cd /home/runner/work/c64stream/c64stream
./tests/acceptance_test.sh

# Run with custom build directory
BUILD_DIR=./my_build ./tests/acceptance_test.sh
```

### Manual Testing

For manual testing with the mock server:

```bash
# Terminal 1: Start mock server
cd build_x86_64
./c64_mock_server

# Terminal 2: Start OBS and add C64 Stream source
obs
# In OBS: Add C64 Stream source, configure to connect to localhost
```

## Test Output

All test artifacts are written to `test_output/`:
- `recordings/` - Would contain recorded frames/CSV (Docker test)
- `mock_server.log` - Mock server output
- `obs.log` - OBS output (Docker test)

## Extending Tests

To add new test scenarios:

1. Add test function to `acceptance_test.sh`
2. Follow naming: `test_<feature>()`
3. Use log helpers: `log_info`, `log_success`, `log_error`, `log_test`
4. Return 0 for pass, 1 for fail
5. Add to validation sequence

Example:
```bash
test_feature_x() {
    log_test "Checking feature X..."
    if [[ condition ]]; then
        log_success "Feature X works"
        return 0
    else
        log_error "Feature X failed"
        return 1
    fi
}
```

## Known Limitations

- Mock server requires Linux (pthread dependency)
- Full OBS integration test requires Docker
- No Windows testing in this script (would need WSL or native Windows test)
- Audio recording not yet validated (requires OBS integration)

## Future Enhancements

- [ ] Complete Docker-based OBS integration test
- [ ] Add audio validation
- [ ] Test different video formats (PAL/NTSC)
- [ ] Performance benchmarking
- [ ] Memory leak detection with valgrind
- [ ] Coverage reporting
- [ ] Windows test suite
