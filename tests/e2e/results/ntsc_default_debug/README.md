# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-08 12:48:16 UTC

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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 111290/19252 packets (103537 video, 7412 audio, minor loss)
- ⚠️ Network Timing: span=25158.3ms, video_mean=242.3us, audio_mean=3331.8us
- ✅ Frame Processing: 9082 frames processed
- ✅ Video Recording: 9.4 MB
- ✅ Content Integrity: 18.9s duration

### Resource Usage

During the test's processing window (21.1s, 43 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 48.6% | 55.8% | 56.64% | 91.2% |
| RAM | 4139.04 MB | 4605.88 MB | 4514.67 MB | 4658.09 MB |
| GPU | 28.99% | 35.94% | 36.03% | 42.41% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 25158.327 ms
- Total packets analyzed: 110971

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 110971 | 0.001 ms | 0.449 ms | 33.292 ms | 228.40% | 7.77% | 31.58% | 735.500 |
| Video | 103535 | 0.001 ms | 0.242 ms | 33.292 ms | 239.34% | 8.33% | 26.97% | 443.167 |
| Audio | 7411 | 0.002 ms | 3.332 ms | 30.393 ms | 43.75% | 14.75% | 0.20% | 1.722 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 103535 | 0.003 ms | 33.286 ms | 12578 (12.2%) |
| Audio | 7411 | 0.759 ms | 26.848 ms | 1185 (16.0%) |

Details: [network.json](network.json)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 18.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from 00:09.5 of the 18.9 s video above.
