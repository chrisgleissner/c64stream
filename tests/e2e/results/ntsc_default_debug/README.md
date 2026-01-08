# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-08 20:51:57 UTC

## Test configuration

- Format: NTSC
- Frames: 60
- Duration: 1.0 seconds
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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 11259/3850 packets (10477 video, 741 audio, minor loss)
- ⚠️ Network Timing: span=3079.5ms, video_mean=293.7us, audio_mean=4007.6us
- ✅ Frame Processing: 613 frames processed
- ✅ Video Recording: 15.1 MB
- ✅ Content Integrity: 30.5s duration

### Resource Usage

During the test's processing window (1.6s, 4 of 45 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 57.3% | 60.75% | 61.45% | 67.0% |
| RAM | 4516.8 MB | 4522.59 MB | 4524.23 MB | 4534.96 MB |
| GPU | 0.0% | 0.29% | 7.15% | 28.02% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 3600 video, 250 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 3079.516 ms
- Total packets analyzed: 11218

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 11218 | 0.001 ms | 0.539 ms | 9.426 ms | 207.39% | 0.29% | 34.68% | 1132.750 |
| Video | 10475 | 0.001 ms | 0.294 ms | 4.864 ms | 209.55% | 0.31% | 30.04% | 787.750 |
| Audio | 740 | 0.026 ms | 4.008 ms | 9.426 ms | 20.64% | 0.95% | 0.14% | 1.536 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 10475 | 0.002 ms | 4.860 ms | 0 |
| Audio | 740 | 0.373 ms | 5.184 ms | 0 |

Details: [network.json](network.json)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 30.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from 00:15.2 of the 30.5 s video above.
