# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-07 14:42:14 UTC

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

- ✅ UDP Packet Reception: 30803/30803 packets (28745 video, 1975 audio)
- ✅ Network Timing: span=8023.8ms, video_mean=278.7us, audio_mean=4001.7us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 10.9 MB
- ✅ Content Integrity: 22.1s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 56.9% | 59.4% | 62.09% | 76.5% |
| RAM | 7958.1 MB | 8019.64 MB | 8006.46 MB | 8025.23 MB |
| GPU | 36.82% | 50.47% | 49.06% | 53.21% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8023.769 ms
- Total packets analyzed: 30725

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30725 | 0.001 ms | 0.518 ms | 13.853 ms | 211.78% | 0.12% | 32.79% | 1189.250 |
| Video | 28743 | 0.001 ms | 0.279 ms | 7.625 ms | 204.08% | 0.13% | 28.18% | 586.000 |
| Audio | 1974 | 0.003 ms | 4.002 ms | 13.853 ms | 25.65% | 2.33% | 0.15% | 1.610 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28743 | 0.001 ms | 7.621 ms | 0 |
| Audio | 1974 | 0.594 ms | 9.611 ms | 0 |

Details: [network.json](network.json)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 680 at 00:11.3 of the 22.1 s video above.
