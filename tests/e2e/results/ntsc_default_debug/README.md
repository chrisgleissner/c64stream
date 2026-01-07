# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-07 11:49:01 UTC

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

- ✅ UDP Packet Reception: 30803/30803 packets (28743 video, 1956 audio)
- ✅ Network Timing: span=8024.4ms, video_mean=278.5us, audio_mean=4001.5us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 10.9 MB
- ✅ Content Integrity: 22.1s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 63.7% | 70.95% | 72.26% | 82.2% |
| RAM | 6278.91 MB | 6387.81 MB | 6371.61 MB | 6395.64 MB |
| GPU | 23.58% | 46.2% | 42.74% | 53.74% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8024.449 ms
- Total packets analyzed: 30706

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30706 | 0.001 ms | 0.516 ms | 9.848 ms | 213.55% | 0.09% | 32.74% | 1210.250 |
| Video | 28742 | 0.001 ms | 0.278 ms | 7.071 ms | 208.93% | 0.10% | 28.15% | 603.500 |
| Audio | 1954 | 0.006 ms | 4.002 ms | 9.848 ms | 26.08% | 2.30% | 0.15% | 1.633 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28742 | 0.001 ms | 7.067 ms | 0 |
| Audio | 1954 | 0.648 ms | 5.607 ms | 0 |

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
- Taken from frame 681 at 00:11.4 of the 22.1 s video above.
