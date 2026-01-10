# C64 Stream E2E Test Report

## Scenario: NTSC Default Debug

Generated: 2026-01-10 00:20:27 UTC

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

- ⚠️ UDP Packet Reception: 30735/30803 packets (28736 video, 1999 audio, minor loss)
- ✅ Network Timing: span=8005.1ms, video_mean=340.0us, audio_mean=4005.0us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.1s duration

### Resource Usage

During the test's processing window (18.6s, 38 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.3% | 56.0% | 57.64% | 92.3% |
| RAM | 6540.61 MB | 6680.24 MB | 6699.84 MB | 6900.3 MB |
| GPU | 4.52% | 13.69% | 12.42% | 52.18% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8005.142 ms
- Total packets analyzed: 25542

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25542 | 0.001 ms | 0.627 ms | 6.879 ms | 169.20% | 10.24% | 15.78% | 16.488 |
| Video | 23544 | 0.001 ms | 0.340 ms | 4.003 ms | 105.28% | 11.10% | 8.65% | 8.111 |
| Audio | 1998 | 1.473 ms | 4.005 ms | 6.879 ms | 17.34% | 0.50% | 0.00% | 1.499 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23544 | 0.019 ms | 3.723 ms | 0 |
| Audio | 1998 | 0.140 ms | 2.879 ms | 0 |

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
- Taken from frame 659 at 00:11.0 of the 22.1 s video above.
