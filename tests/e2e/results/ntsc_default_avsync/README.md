# C64 Stream E2E Test Report

## Scenario: NTSC Default A/V Sync

- Generated: 2026-01-18 15:35:18 UTC
- Git Branch: feat/c64script-extension
- Git ID: 3ad75ae
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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1018G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30767, Missing 36 (0.12%)
- ✅ Network Timing: span=8013.5ms, video_mean=338.8us, audio_mean=4005.0us
- ✅ Frame Processing: 479 frames processed
- ✅ Video Recording: 10.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.1s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 51.3% | 55.3% | 56.24% | 74.7% |
| RAM | 6977.2 MB | 7029.23 MB | 7034.13 MB | 7101.53 MB |
| GPU | 0% | 0% | 0% | 0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8013.508 ms
- Total packets analyzed: 25654

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25654 | 0.001 ms | 0.625 ms | 6.734 ms | 169.42% | 10.03% | 15.66% | 16.651 |
| Video | 23654 | 0.001 ms | 0.339 ms | 3.637 ms | 103.71% | 10.87% | 8.56% | 8.115 |
| Audio | 2000 | 1.750 ms | 4.005 ms | 6.734 ms | 17.83% | 0.65% | 0.00% | 1.514 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23654 | 0.018 ms | 3.358 ms | 0 |
| Audio | 2000 | 0.106 ms | 2.734 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 13.8ms, max 15.2ms

#### Sync Details

- 🟢 Pop #1 [R]: audio=9742.0ms, video=9728.2ms (frame 582), diff=13.8ms
- 🟢 Pop #2 [R]: audio=10543.0ms, video=10530.5ms (frame 630), diff=12.5ms
- 🟢 Pop #3 [R]: audio=11348.0ms, video=11332.9ms (frame 678), diff=15.1ms
- 🟢 Pop #4 [L]: audio=12149.0ms, video=12135.2ms (frame 726), diff=13.8ms
- 🟢 Pop #5 [R]: audio=12950.0ms, video=12937.5ms (frame 774), diff=12.5ms
- 🟢 Pop #6 [R]: audio=13755.0ms, video=13739.8ms (frame 822), diff=15.2ms
- 🟢 Pop #7 [R]: audio=14556.0ms, video=14542.2ms (frame 870), diff=13.8ms
- 🟢 Pop #8 [L]: audio=15357.0ms, video=15344.5ms (frame 918), diff=12.5ms
- 🟢 Pop #9 [R]: audio=16162.0ms, video=16146.8ms (frame 966), diff=15.2ms

- Channels: RRRLRRRLR
- 🔁 Channel alternation: MISMATCH

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 582 at 00:09.7 of the 21.2 s video above.
