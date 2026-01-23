# C64 Stream E2E Test Report

## Scenario: NTSC Default with Recording

- Generated: 2026-01-23 12:35:23 UTC
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

- ✅ UDP Packet Reception: Expected 30803, Received 30756, Missing 47 (0.15%)
- ✅ Network Timing: span=8012.2ms, video_mean=336.9us, audio_mean=4005.0us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.4s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 48.2% | 52.6% | 52.4% | 64.1% |
| RAM | 4684.08 MB | 4813.8 MB | 4807.18 MB | 4853.2 MB |
| GPU | 0% | 0% | 7.6% | 55% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8012.170 ms
- Total packets analyzed: 25783

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25783 | 0.001 ms | 0.621 ms | 6.909 ms | 169.21% | 9.08% | 15.43% | 16.263 |
| Video | 23784 | 0.001 ms | 0.337 ms | 3.957 ms | 100.91% | 9.81% | 8.36% | 8.039 |
| Audio | 1999 | 1.787 ms | 4.005 ms | 6.909 ms | 17.05% | 0.45% | 0.00% | 1.493 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23784 | 0.015 ms | 3.678 ms | 0 |
| Audio | 1999 | 0.077 ms | 2.909 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 13.0ms, max 14.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9825.0ms, video=9811.8ms (frame 587), diff=13.2ms
- 🟢 Pop #2 [R]: audio=10625.0ms, video=10614.1ms (frame 635), diff=10.9ms
- 🟢 Pop #3 [L]: audio=11431.0ms, video=11416.4ms (frame 683), diff=14.6ms
- 🟢 Pop #4 [R]: audio=12231.0ms, video=12218.8ms (frame 731), diff=12.2ms
- 🟢 Pop #5 [L]: audio=13033.0ms, video=13021.1ms (frame 779), diff=11.9ms
- 🟢 Pop #6 [R]: audio=13838.0ms, video=13823.4ms (frame 827), diff=14.6ms
- 🟢 Pop #7 [L]: audio=14639.0ms, video=14625.7ms (frame 875), diff=13.3ms
- 🟢 Pop #8 [R]: audio=15441.0ms, video=15428.1ms (frame 923), diff=12.9ms
- 🟢 Pop #9 [L]: audio=16244.0ms, video=16230.4ms (frame 971), diff=13.6ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

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
