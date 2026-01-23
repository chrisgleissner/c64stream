# C64 Stream E2E Test Report

## Scenario: NTSC Default A/V Sync (Device)

- Generated: 2026-01-23 12:34:09 UTC
- Git Branch: doc/improvements
- Git ID: bd15461
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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 962G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Received 65622
- ✅ Network Timing: span=17943.8ms, video_mean=340.0us, audio_mean=4001.5us
- ✅ Frame Processing: 1019 frames processed
- ✅ Video Recording: 6.7 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (14.5s, 28 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 7.8% | 50.5% | 45.97% | 59.3% |
| RAM | 4359.82 MB | 4702.49 MB | 4653.68 MB | 4792.97 MB |
| GPU | 0% | 0% | 0% | 0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ℹ️ Packet Generation: Skipped (device packet source)
- ✅ UDP Capture: Device stream
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 17943.825 ms
- Total packets analyzed: 57238

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 57238 | 0.001 ms | 0.625 ms | 6.845 ms | 172.83% | 5.16% | 14.12% | 16.568 |
| Video | 52782 | 0.001 ms | 0.340 ms | 5.668 ms | 128.85% | 5.50% | 6.92% | 11.059 |
| Audio | 4456 | 1.102 ms | 4.002 ms | 6.845 ms | 14.85% | 0.65% | 0.00% | 1.504 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 52782 | 0.027 ms | 5.413 ms | 1 (0.0%) |
| Audio | 4456 | 0.051 ms | 2.897 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ❌ Poor synchronization (0.0%): avg offset 784.8ms, max 3974.3ms

#### Sync Details

- • Pop #1 [L]: audio=54.0ms, video=4028.3ms (frame 241), diff=3974.3ms
- • Pop #2 [L]: audio=1021.0ms, video=4028.3ms (frame 241), diff=3007.3ms
- • Pop #3 [R]: audio=1986.0ms, video=4028.3ms (frame 241), diff=2042.3ms
- • Pop #4 [R]: audio=2965.0ms, video=4028.3ms (frame 241), diff=1063.3ms
- 🔴 Pop #5 [L]: audio=3937.0ms, video=4028.3ms (frame 241), diff=91.3ms
- 🔴 Pop #6 [R]: audio=4920.0ms, video=5014.5ms (frame 300), diff=94.5ms
- 🔴 Pop #7 [R]: audio=5896.0ms, video=5984.0ms (frame 358), diff=88.0ms
- 🔴 Pop #8 [L]: audio=6884.0ms, video=6970.2ms (frame 417), diff=86.2ms
- 🔴 Pop #9 [L]: audio=7874.0ms, video=7939.7ms (frame 475), diff=65.7ms
- • Pop #10 [R]: audio=8825.0ms, video=8925.9ms (frame 534), diff=100.9ms
- 🔴 Pop #11 [R]: audio=9809.0ms, video=9895.4ms (frame 592), diff=86.4ms
- • Pop #12 [L]: audio=10781.0ms, video=10881.6ms (frame 651), diff=100.6ms
- 🔴 Pop #13 [R]: audio=11762.0ms, video=11851.0ms (frame 709), diff=89.0ms
- 🔴 Pop #14 [R]: audio=12740.0ms, video=12837.2ms (frame 768), diff=97.2ms

- Channels: LLRRLRRLLRRLRR
- 🔁 Channel alternation: MISMATCH

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 13.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 242 at 00:04.0 of the 13.5 s video above.
