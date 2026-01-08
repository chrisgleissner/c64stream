# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-02 16:51:09 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28800 video, 2003 audio)
- ✅ Network Timing: span=8023.3ms, video_mean=278.6us, audio_mean=4005.1us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 10.9 MB
- ✅ Content Integrity: 22.0s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 51.7% | 59.3% | 59.01% | 71.3% |
| RAM | 4157.16 MB | 4185.04 MB | 4187.94 MB | 4213.22 MB |
| GPU | 23.44% | 25.66% | 25.55% | 28.28% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8023.342 ms
- Total packets analyzed: 30800

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30800 | 0.001 ms | 0.521 ms | 8.675 ms | 209.16% | 0.29% | 33.48% | 1183.250 |
| Video | 28798 | 0.001 ms | 0.279 ms | 4.463 ms | 198.12% | 0.31% | 28.86% | 596.500 |
| Audio | 2002 | 0.006 ms | 4.005 ms | 8.675 ms | 23.53% | 1.45% | 0.05% | 1.521 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28798 | 0.002 ms | 4.459 ms | 0 |
| Audio | 2002 | 0.574 ms | 4.432 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 10.5ms, max 13.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10945.0ms, video=10931.7ms (frame 654), diff=13.3ms
- 🟢 Pop #2 [R]: audio=11746.0ms, video=11734.0ms (frame 702), diff=12.0ms
- 🟢 Pop #3 [L]: audio=12548.0ms, video=12536.4ms (frame 750), diff=11.6ms
- 🟢 Pop #4 [R]: audio=13350.0ms, video=13338.7ms (frame 798), diff=11.3ms
- 🟢 Pop #5 [L]: audio=14152.0ms, video=14141.0ms (frame 846), diff=11.0ms
- 🟢 Pop #6 [R]: audio=14953.0ms, video=14943.3ms (frame 894), diff=9.7ms
- 🟢 Pop #7 [L]: audio=15756.0ms, video=15745.7ms (frame 942), diff=10.3ms
- 🟢 Pop #8 [R]: audio=16556.0ms, video=16548.0ms (frame 990), diff=8.0ms
- 🟢 Pop #9 [L]: audio=17358.0ms, video=17350.3ms (frame 1038), diff=7.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 654 at 00:10.9 of the 22.0 s video above.
