# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-09 18:05:46 UTC

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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 30754/30803 packets (28754 video, 2000 audio, minor loss)
- ✅ Network Timing: span=8012.1ms, video_mean=347.3us, audio_mean=4005.0us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.1s duration

### Resource Usage

During the test's processing window (18.6s, 38 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 53.1% | 56.8% | 58.68% | 74.7% |
| RAM | 6807.79 MB | 6832.4 MB | 6843.28 MB | 6920.08 MB |
| GPU | 11.26% | 14.25% | 15.23% | 28.25% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8012.088 ms
- Total packets analyzed: 25067

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25067 | 0.001 ms | 0.639 ms | 6.839 ms | 168.55% | 10.85% | 16.39% | 16.616 |
| Video | 23068 | 0.001 ms | 0.347 ms | 4.116 ms | 109.95% | 11.76% | 9.18% | 8.448 |
| Audio | 1999 | 1.110 ms | 4.005 ms | 6.839 ms | 18.55% | 0.90% | 0.00% | 1.549 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23068 | 0.021 ms | 3.837 ms | 0 |
| Audio | 1999 | 0.140 ms | 2.892 ms | 0 |

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
- Taken from frame 658 at 00:11.0 of the 22.1 s video above.
