# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-10 18:58:32 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.3

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 19186/19252 packets (17938 video, 1248 audio, minor loss)
- ✅ Network Timing: span=4999.9ms, video_mean=354.2us, audio_mean=4005.8us
- ✅ Frame Processing: 1550 frames processed
- ✅ Video Recording: 9.4 MB
- ✅ Content Integrity: 19.0s duration

### Resource Usage

During the test's processing window (15.1s, 31 of 43 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 57.5% | 66.7% | 67.49% | 94.2% |
| RAM | 7511.37 MB | 7526.34 MB | 7531.4 MB | 7570.08 MB |
| GPU | 14.01% | 20.2% | 20.3% | 37.11% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 4999.869 ms
- Total packets analyzed: 15355

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 15355 | 0.001 ms | 0.651 ms | 9.743 ms | 171.46% | 14.02% | 17.06% | 17.053 |
| Video | 14108 | 0.001 ms | 0.354 ms | 7.437 ms | 126.33% | 15.20% | 9.79% | 8.904 |
| Audio | 1247 | 0.002 ms | 4.006 ms | 9.743 ms | 22.55% | 2.09% | 0.16% | 1.675 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 14108 | 0.035 ms | 7.156 ms | 0 |
| Audio | 1247 | 0.183 ms | 5.744 ms | 0 |

Details: [network.json](network.json)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 650 at 00:10.8 of the 19.0 s video above.
