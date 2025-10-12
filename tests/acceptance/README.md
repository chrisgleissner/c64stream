# C64 Stream Acceptance Tests

Automated acceptance tests for the C64 Stream OBS plugin. These tests validate that the plugin correctly receives, processes, and renders C64 Ultimate video and audio streams according to the [C64 Ultimate Data Stream Specification](../../doc/c64-stream-spec.md).

## Overview

The acceptance test suite consists of:

1. **Packet Generator** (`generate_packets.py`) - Creates deterministic test packets
2. **UDP Replay Tool** (`udp_replay.c`) - High-performance packet transmission
3. **Test Orchestrator** (`run_acceptance_test.py`) - Coordinates the complete test
4. **Output Verifier** (`verify_output.py`) - Validates recorded output

## Architecture

```
┌─────────────────┐     UDP Packets     ┌──────────────────┐
│ Packet Generator│ ──────────────────> │   udp_replay     │
└─────────────────┘                     └──────────────────┘
                                                │
                                                │ UDP Stream
                                                ▼
                                        ┌──────────────────┐
                                        │  OBS + C64Plugin │
                                        └──────────────────┘
                                                │
                                                │ Recording
                                                ▼
                                        ┌──────────────────┐
                                        │ Output Verifier  │
                                        └──────────────────┘
```

## Test Methodology

### Deterministic Test Patterns

**Video Markers:**
- Top-left 32×32 pixel block contains frame marker
- Marker color = `frame_num % 16` (VIC color index)
- Allows frame-level verification and dropped frame detection

**Audio Markers:**
- Amplitude envelope modulation in first 10 samples
- Envelope amplitude = `0.5 + 0.5 × (frame_num % 16) / 16`
- 440Hz base tone for consistency
- Enables A/V sync verification

### Packet Format Compliance

All packets strictly follow the C64 Ultimate specification:

**Video Packets (780 bytes):**
```
Header (12 bytes):
  - Sequence number (16-bit LE)
  - Frame number (16-bit LE)
  - Line number (16-bit LE, bit 15 = last packet flag)
  - Pixels per line (16-bit LE) = 384
  - Lines per packet (8-bit) = 4
  - Bits per pixel (8-bit) = 4
  - Encoding type (16-bit) = 0

Payload (768 bytes):
  - 4 lines × 384 pixels
  - 4-bit VIC colors, little-endian
```

**Audio Packets (770 bytes):**
```
Header (2 bytes):
  - Sequence number (16-bit LE)

Payload (768 bytes):
  - 192 stereo samples
  - 16-bit signed, little-endian
  - Interleaved L/R channels
```

## Building

The acceptance tests are built as part of the main CMake build:

```bash
cd build_x86_64
cmake --build . --target udp_replay
```

Or build standalone:

```bash
cd tests/acceptance
gcc -O2 -o udp_replay udp_replay.c
```

## Usage

### 1. Generate Test Packets

```bash
cd tests/acceptance
./generate_packets.py --frames 30 --output test_packets
```

This creates:
```
test_packets/
├── video/
│   ├── PAL/     (68 packets × 30 frames = 2040 files)
│   └── NTSC/    (60 packets × 30 frames = 1800 files)
└── audio/
    ├── PAL/     (30 packets)
    └── NTSC/    (30 packets)
```

### 2. Run Acceptance Test

```bash
./run_acceptance_test.py --format PAL --verbose
```

Options:
- `--format PAL|NTSC` - Video format to test
- `--frames N` - Number of frames
- `--video-port N` - Video UDP port (default: 11000)
- `--audio-port N` - Audio UDP port (default: 11001)
- `--verbose` - Detailed logging

### 3. Verify Output (Manual)

If you have a recording file from a manual test:

```bash
./verify_output.py recording_PAL.mkv --format PAL --frames 30 --verbose
```

## Running in CI

The acceptance tests can run in GitHub Actions with Xvfb for headless operation:

```yaml
- name: Install Test Dependencies
  run: |
    sudo apt-get update
    sudo apt-get install -y xvfb obs-studio ffmpeg python3-numpy

- name: Build UDP Replay Tool
  run: |
    cd tests/acceptance
    gcc -O2 -o udp_replay udp_replay.c

- name: Generate Test Packets
  run: |
    cd tests/acceptance
    ./generate_packets.py --frames 30

- name: Run Acceptance Test
  run: |
    cd tests/acceptance
    ./run_acceptance_test.py --format PAL --verbose
```

## Performance Considerations

The C64 Ultimate protocol has high bandwidth requirements:

- **PAL Video**: 21.3 Mbps (3,408 packets/sec)
- **NTSC Video**: 22.4 Mbps (3,590 packets/sec)
- **Audio**: 1.54 Mbps (250 packets/sec)

The `udp_replay` tool is written in C for maximum performance:
- Minimal overhead for packet transmission
- Configurable inter-packet delay (microsecond precision)
- Sorted packet order to maintain frame integrity

### Resource-Constrained Environments

For CI runners with limited resources, the test can be adapted:

1. **Reduce frame count**: Test with 10-15 frames instead of 30
2. **Single format**: Test only PAL or NTSC, not both
3. **Increase packet delay**: Reduce bandwidth to avoid drops

Example for constrained environment:
```bash
./generate_packets.py --frames 10 --format PAL
./run_acceptance_test.py --format PAL --frames 10
```

## Validation Checks

The output verifier performs the following checks:

- ✅ Recording file exists and is readable
- ✅ Video dimensions match format (384×272 PAL or 384×240 NTSC)
- ✅ Frame count within expected range (±20% tolerance)
- ⚠️  Frame marker pattern verification (requires image processing)
- ⚠️  Audio/Video synchronization (requires audio analysis)

Checks marked with ⚠️ are aspirational and require additional implementation.

## Troubleshooting

### UDP Packet Loss

If you see dropped packets in the plugin logs:

1. **Increase system UDP buffer size:**
   ```bash
   sudo sysctl -w net.core.rmem_max=8388608
   sudo sysctl -w net.core.rmem_default=8388608
   ```

2. **Add packet delay:**
   ```bash
   # Increase inter-packet delay in run_acceptance_test.py
   '--delay', '500',  # 500 microseconds instead of 300
   ```

### OBS Recording Issues

1. **Check OBS logs:**
   ```bash
   tail -f ~/.config/obs-studio/logs/*.txt
   ```

2. **Verify Xvfb is running:**
   ```bash
   ps aux | grep Xvfb
   echo $DISPLAY  # Should show :99 or similar
   ```

3. **Test OBS manually:**
   ```bash
   DISPLAY=:99 obs --verbose
   ```

## Future Enhancements

1. **Full OBS Integration**
   - Use obs-websocket API for programmatic control
   - Automated scene setup with C64 Stream source
   - Recording start/stop automation

2. **Advanced Verification**
   - Pixel-level frame marker analysis (OpenCV/PIL)
   - Audio waveform analysis for sync verification
   - Automated pass/fail criteria

3. **Performance Testing**
   - Measure packet drop rate under load
   - Benchmark frame processing latency
   - Stress test with extended recordings

4. **Multi-Format Testing**
   - Test both PAL and NTSC in single run
   - Validate format auto-detection
   - Test format switching mid-stream

## References

- [C64 Ultimate Data Stream Specification](../../doc/c64-stream-spec.md)
- [OBS Studio Documentation](https://obsproject.com/docs/)
- [OBS WebSocket Protocol](https://github.com/obsproject/obs-websocket)
