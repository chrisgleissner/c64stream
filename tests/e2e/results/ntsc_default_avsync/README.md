# C64 Stream E2E Test Report

## Scenario: NTSC Default A/V Sync

- Generated: 2026-01-22 22:42:30 UTC
- Git Branch: cursor/c64-stream-effects-filter-4ac5
- Git ID: 912712e
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
- RAM: 31Gi total, 19Gi available
- Disk (/): 1.8T total, 964G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30792, Missing 11 (0.04%)
- ✅ Network Timing: span=8021.3ms, video_mean=376.8us, audio_mean=4005.0us
- ✅ Frame Processing: 479 frames processed
- ✅ Video Recording: 10.7 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.5s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 66% | 74.7% | 77.24% | 99.1% |
| RAM | 11716.56 MB | 11770.61 MB | 11768.12 MB | 11811.06 MB |
| GPU | 6% | 19% | 25.4% | 48% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8021.316 ms
- Total packets analyzed: 23292

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 23292 | 0.001 ms | 0.689 ms | 8.447 ms | 168.74% | 18.68% | 19.71% | 17.806 |
| Video | 21290 | 0.001 ms | 0.377 ms | 7.622 ms | 134.44% | 20.37% | 12.35% | 10.021 |
| Audio | 2002 | 0.002 ms | 4.005 ms | 8.447 ms | 24.29% | 2.30% | 0.20% | 1.720 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 21290 | 0.046 ms | 7.342 ms | 0 |
| Audio | 2002 | 0.331 ms | 4.449 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Acceptable synchronization (88.9%): avg offset 17.1ms, max 31.4ms

#### Sync Details

- 🟢 Pop #1 [R]: audio=10126.0ms, video=10112.7ms (frame 605), diff=13.3ms
- 🟢 Pop #2 [L]: audio=10927.0ms, video=10915.0ms (frame 653), diff=12.0ms
- 🟢 Pop #3 [R]: audio=11732.0ms, video=11717.3ms (frame 701), diff=14.7ms
- 🟢 Pop #4 [R]: audio=12533.0ms, video=12519.6ms (frame 749), diff=13.4ms
- 🟢 Pop #5 [R]: audio=13334.0ms, video=13305.3ms (frame 796), diff=28.7ms
- 🟢 Pop #6 [R]: audio=14139.0ms, video=14107.6ms (frame 844), diff=31.4ms
- 🟢 Pop #7 [R]: audio=14940.0ms, video=14926.6ms (frame 893), diff=13.4ms
- 🟢 Pop #8 [R]: audio=15741.0ms, video=15728.9ms (frame 941), diff=12.1ms
- 🟢 Pop #9 [R]: audio=16546.0ms, video=16531.3ms (frame 989), diff=14.7ms

- Channels: RLRRRRRRR
- 🔁 Channel alternation: MISMATCH

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.6 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 605 at 00:10.1 of the 21.6 s video above.
