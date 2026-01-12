# C64 Stream E2E Test Report

## Scenario: ntsc_default_avsync

Generated: 2026-01-12 16:41:57 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Version: 1.0.2

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 19221 packets (17971 video, 0 audio)
- ✅ Network Timing: span=5006.7ms, video_mean=352.7us, audio_mean=4006.9us
- ✅ Frame Processing: 299 frames processed
- ✅ Video Recording: 8.1 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (15.1s, 31 of 31 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 49.9% | 58.0% | 59.99% | 85.5% |
| RAM | 6567.25 MB | 6665.91 MB | 6677.27 MB | 6738.87 MB |
| GPU | 30.4% | 89.8% | 81.15% | 97.1% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 19221 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5006.725 ms
- Total packets analyzed: 15443

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 15443 | 0.001 ms | 0.648 ms | 18.418 ms | 173.85% | 12.98% | 16.76% | 17.285 |
| Video | 14194 | 0.001 ms | 0.353 ms | 9.576 ms | 125.27% | 14.01% | 9.56% | 8.864 |
| Audio | 1249 | 0.002 ms | 4.007 ms | 18.418 ms | 27.48% | 2.72% | 0.56% | 1.766 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 14194 | 0.029 ms | 9.296 ms | 0 |
| Audio | 1249 | 0.222 ms | 14.418 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 0.0ms, max 0.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9727.0ms, video=9711.5ms (frame 581), diff=15.5ms
- 🟢 Pop #2 [R]: audio=10528.0ms, video=10513.8ms (frame 629), diff=14.2ms
- 🟢 Pop #3 [L]: audio=11333.0ms, video=11316.2ms (frame 677), diff=16.8ms
- 🟢 Pop #4 [R]: audio=12134.0ms, video=12118.5ms (frame 725), diff=15.5ms
- 🟢 Pop #5 [L]: audio=12935.0ms, video=12920.8ms (frame 773), diff=14.2ms

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 16.3 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)