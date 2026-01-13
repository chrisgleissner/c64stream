# C64 Stream E2E Test Report

## Scenario: NTSC Default A/V Sync

Generated: 2026-01-13 15:01:56 UTC

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
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30789 packets (28786 video, 2003 audio)
- ✅ Network Timing: span=8019.1ms, video_mean=371.7us, audio_mean=4005.0us
- ✅ Frame Processing: 479 frames processed
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 66% | 70.1% | 71.45% | 82.3% |
| RAM | 5681.46 MB | 5763.77 MB | 5763.4 MB | 5797.06 MB |
| GPU | 30.43% | 92.78% | 78.53% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8019.076 ms
- Total packets analyzed: 23578

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 23578 | 0.001 ms | 0.680 ms | 9.577 ms | 167.80% | 15.46% | 18.73% | 17.525 |
| Video | 21576 | 0.001 ms | 0.372 ms | 9.577 ms | 129.36% | 16.87% | 11.28% | 9.391 |
| Audio | 2002 | 0.002 ms | 4.005 ms | 8.139 ms | 21.90% | 1.20% | 0.05% | 1.610 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 21576 | 0.038 ms | 9.296 ms | 0 |
| Audio | 2002 | 0.304 ms | 4.141 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 17.1ms, max 18.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9762.0ms, video=9744.9ms (frame 583), diff=17.1ms
- 🟢 Pop #2 [R]: audio=10563.0ms, video=10547.3ms (frame 631), diff=15.7ms
- 🟢 Pop #3 [L]: audio=11368.0ms, video=11349.6ms (frame 679), diff=18.4ms
- 🟢 Pop #4 [L]: audio=12169.0ms, video=12151.9ms (frame 727), diff=17.1ms
- 🟢 Pop #5 [R]: audio=12970.0ms, video=12954.2ms (frame 775), diff=15.8ms
- 🟢 Pop #6 [R]: audio=13775.0ms, video=13756.6ms (frame 823), diff=18.4ms
- 🟢 Pop #7 [R]: audio=14576.0ms, video=14558.9ms (frame 871), diff=17.1ms
- 🟢 Pop #8 [L]: audio=15377.0ms, video=15361.2ms (frame 919), diff=15.8ms
- 🟢 Pop #9 [L]: audio=16182.0ms, video=16163.5ms (frame 967), diff=18.5ms

- Channels: LRLLRRRLL
- 🔁 Channel alternation: MISMATCH

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 583 at 00:09.7 of the 19.2 s video above.
