# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-09 15:31:20 UTC

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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 30801/30803 packets (28798 video, 2003 audio, minor loss)
- ✅ Network Timing: span=8023.4ms, video_mean=287.1us, audio_mean=4004.8us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 10.9 MB
- ✅ Content Integrity: 22.0s duration

### Resource Usage

During the test's processing window (18.1s, 37 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 51.7% | 58.4% | 61.41% | 85.7% |
| RAM | 5904.2 MB | 5941.15 MB | 5976.41 MB | 6095.51 MB |
| GPU | 11.54% | 16.01% | 19.89% | 42.38% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8023.387 ms
- Total packets analyzed: 29953

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 29953 | 0.001 ms | 0.536 ms | 30.023 ms | 210.93% | 0.00% | 35.67% | 2342.500 |
| Video | 27951 | 0.001 ms | 0.287 ms | 30.023 ms | 206.50% | 0.00% | 31.09% | 1149.000 |
| Audio | 2002 | 0.003 ms | 4.005 ms | 28.266 ms | 28.34% | 1.75% | 0.25% | 1.565 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 27951 | 0.001 ms | 30.021 ms | 0 |
| Audio | 2002 | 0.519 ms | 24.024 ms | 0 |

Details: [network.json](network.json)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 652 at 00:10.9 of the 22.0 s video above.
