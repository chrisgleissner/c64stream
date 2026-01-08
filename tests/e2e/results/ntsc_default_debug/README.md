# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-08 09:35:55 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 19252/19252 packets (17961 video, 1226 audio)
- ✅ Network Timing: span=5014.3ms, video_mean=278.7us, audio_mean=3996.9us
- ✅ Frame Processing: 1550 frames processed
- ✅ Video Recording: 9.4 MB
- ✅ Content Integrity: 19.0s duration

### Resource Usage

During the test's processing window (4.6s, 10 of 43 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 52.3% | 56.95% | 60.59% | 92.4% |
| RAM | 5154.52 MB | 5222.55 MB | 5213.3 MB | 5243.44 MB |
| GPU | 28.32% | 32.92% | 33.34% | 37.39% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5014.319 ms
- Total packets analyzed: 19191

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 19191 | 0.001 ms | 0.516 ms | 8.085 ms | 208.47% | 0.30% | 33.83% | 1159.000 |
| Video | 17959 | 0.001 ms | 0.279 ms | 4.879 ms | 195.34% | 0.32% | 29.31% | 549.250 |
| Audio | 1224 | 0.006 ms | 3.997 ms | 8.085 ms | 23.16% | 2.04% | 0.00% | 1.565 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17959 | 0.001 ms | 4.875 ms | 0 |
| Audio | 1224 | 0.489 ms | 4.242 ms | 0 |

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
- Taken from frame 651 at 00:10.8 of the 19.0 s video above.
