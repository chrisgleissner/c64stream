# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-07 15:36:24 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.2

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28747 video, 1976 audio)
- ✅ Network Timing: span=8023.9ms, video_mean=278.5us, audio_mean=4006.5us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.2s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 58.1% | 59.4% | 61.98% | 80.0% |
| RAM | 7668.31 MB | 7744.78 MB | 7731.71 MB | 7762.75 MB |
| GPU | 46.0% | 54.38% | 53.18% | 57.41% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8023.882 ms
- Total packets analyzed: 30727

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30727 | 0.001 ms | 0.518 ms | 16.382 ms | 209.34% | 0.10% | 33.00% | 1155.000 |
| Video | 28745 | 0.001 ms | 0.279 ms | 16.382 ms | 198.63% | 0.11% | 28.39% | 558.500 |
| Audio | 1975 | 0.005 ms | 4.006 ms | 15.522 ms | 23.06% | 1.52% | 0.05% | 1.506 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28745 | 0.001 ms | 16.378 ms | 0 |
| Audio | 1975 | 0.479 ms | 11.279 ms | 0 |

Details: [network.json](network.json)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 686 at 00:11.4 of the 22.2 s video above.
