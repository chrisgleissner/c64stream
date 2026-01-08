# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-08 07:35:25 UTC

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

- ✅ UDP Packet Reception: 19252/19252 packets (17960 video, 1232 audio)
- ✅ Network Timing: span=5014.4ms, video_mean=277.3us, audio_mean=4017.8us
- ✅ Frame Processing: 1550 frames processed
- ✅ Video Recording: 9.3 MB
- ✅ Content Integrity: 18.9s duration

### Resource Usage

During the test's processing window (4.6s, 10 of 43 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 53.3% | 58.3% | 61.68% | 86.1% |
| RAM | 4698.12 MB | 4740.37 MB | 4736.3 MB | 4755.52 MB |
| GPU | 43.54% | 45.65% | 46.48% | 49.43% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5014.364 ms
- Total packets analyzed: 19198

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 19198 | 0.001 ms | 0.517 ms | 25.014 ms | 214.89% | 0.15% | 34.21% | 1171.250 |
| Video | 17958 | 0.001 ms | 0.277 ms | 15.177 ms | 206.91% | 0.16% | 29.69% | 594.500 |
| Audio | 1231 | 0.004 ms | 4.018 ms | 25.014 ms | 29.26% | 2.52% | 0.32% | 1.594 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17958 | 0.001 ms | 15.173 ms | 0 |
| Audio | 1231 | 0.509 ms | 20.766 ms | 0 |

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
- Taken from frame 644 at 00:10.7 of the 18.9 s video above.
