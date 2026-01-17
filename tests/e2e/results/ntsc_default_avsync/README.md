# C64 Stream E2E Test Report

## Scenario: NTSC Default A/V Sync

- Generated: 2026-01-17 17:02:28 UTC
- Git Branch: fix/improve-keyboard-mappings
- Git ID: 5886543
- Environment: local

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1022G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30778, Missing 25 (0.08%)
- ✅ Network Timing: span=8014.8ms, video_mean=366.3us, audio_mean=4004.4us
- ✅ Frame Processing: 479 frames processed
- ✅ Video Recording: 10.7 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.3s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 63.6% | 69.8% | 71.91% | 88.6% |
| RAM | 4792.38 MB | 4916.24 MB | 4922.28 MB | 5052.77 MB |
| GPU | 13% | 21% | 28.43% | 46% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8014.845 ms
- Total packets analyzed: 23879

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 23879 | 0.001 ms | 0.671 ms | 48.380 ms | 176.31% | 16.99% | 18.33% | 17.472 |
| Video | 21878 | 0.001 ms | 0.366 ms | 48.380 ms | 158.53% | 18.48% | 11.01% | 9.468 |
| Audio | 2001 | 0.002 ms | 4.004 ms | 11.175 ms | 23.67% | 2.30% | 0.20% | 1.673 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 21878 | 0.039 ms | 48.100 ms | 0 |
| Audio | 2001 | 0.301 ms | 7.178 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 20.1ms, max 23.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9946.0ms, video=9945.5ms (frame 595), diff=0.5ms
- 🟢 Pop #2 [R]: audio=10769.0ms, video=10747.8ms (frame 643), diff=21.2ms
- 🟢 Pop #3 [R]: audio=11574.0ms, video=11550.2ms (frame 691), diff=23.8ms
- 🟢 Pop #4 [L]: audio=12375.0ms, video=12352.5ms (frame 739), diff=22.5ms
- 🟢 Pop #5 [R]: audio=13176.0ms, video=13154.8ms (frame 787), diff=21.2ms
- 🟢 Pop #6 [R]: audio=13981.0ms, video=13957.1ms (frame 835), diff=23.9ms
- 🟢 Pop #7 [R]: audio=14782.0ms, video=14759.5ms (frame 883), diff=22.5ms
- 🟢 Pop #8 [L]: audio=15583.0ms, video=15561.8ms (frame 931), diff=21.2ms
- 🟢 Pop #9 [R]: audio=16388.0ms, video=16364.1ms (frame 979), diff=23.9ms

- Channels: LRRLRRRLR
- 🔁 Channel alternation: MISMATCH

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 595 at 00:09.9 of the 21.5 s video above.
