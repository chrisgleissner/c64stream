# C64 Stream E2E Test Report

## Scenario: NTSC Default A/V Sync

- Generated: 2026-01-23 12:33:31 UTC
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

- ✅ UDP Packet Reception: Expected 30803, Received 30751, Missing 52 (0.17%)
- ✅ Network Timing: span=8008.8ms, video_mean=335.3us, audio_mean=4005.2us
- ✅ Frame Processing: 479 frames processed
- ✅ Video Recording: 10.7 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.4s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 47.2% | 49.9% | 50.53% | 63.2% |
| RAM | 4733.98 MB | 4826.54 MB | 4821.04 MB | 4853.86 MB |
| GPU | 0% | 0% | 11.6% | 47% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8008.796 ms
- Total packets analyzed: 25887

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25887 | 0.001 ms | 0.619 ms | 6.454 ms | 169.13% | 8.71% | 15.18% | 16.246 |
| Video | 23888 | 0.001 ms | 0.335 ms | 2.947 ms | 98.55% | 9.44% | 8.10% | 7.993 |
| Audio | 1999 | 1.619 ms | 4.005 ms | 6.454 ms | 16.66% | 0.55% | 0.00% | 1.492 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23888 | 0.013 ms | 2.668 ms | 1 (0.0%) |
| Audio | 1999 | 0.066 ms | 2.453 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 21.3ms, max 22.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9833.0ms, video=9811.8ms (frame 587), diff=21.2ms
- 🟢 Pop #2 [L]: audio=10634.0ms, video=10614.1ms (frame 635), diff=19.9ms
- 🟢 Pop #3 [L]: audio=11439.0ms, video=11416.4ms (frame 683), diff=22.6ms
- 🟢 Pop #4 [R]: audio=12241.0ms, video=12218.8ms (frame 731), diff=22.2ms
- 🟢 Pop #5 [L]: audio=13041.0ms, video=13021.1ms (frame 779), diff=19.9ms
- 🟢 Pop #6 [L]: audio=13846.0ms, video=13823.4ms (frame 827), diff=22.6ms
- 🟢 Pop #7 [L]: audio=14647.0ms, video=14625.7ms (frame 875), diff=21.3ms
- 🟢 Pop #8 [L]: audio=15448.0ms, video=15428.1ms (frame 923), diff=19.9ms
- 🟢 Pop #9 [L]: audio=16253.0ms, video=16230.4ms (frame 971), diff=22.6ms

- Channels: LLLRLLLLL
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
- Taken from frame 587 at 00:09.8 of the 21.5 s video above.
