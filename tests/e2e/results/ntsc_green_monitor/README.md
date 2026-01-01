# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2026-01-01 18:26:33 UTC

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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28800 video, 2003 audio)
- ⚠️ Network Timing: span=8023.4ms, video_mean=278.5us, audio_mean=4005.6us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.2s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 90.6% | 91.6% | 91.64% | 93.8% |
| RAM | 6614.16 MB | 6657.13 MB | 6654.56 MB | 6689.08 MB |
| GPU | 25.79% | 37.94% | 34.0% | 39.32% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8023.414 ms
- Total packets analyzed: 30800

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30800 | 0.001 ms | 0.521 ms | 29.069 ms | 251.60% | 0.01% | 29.46% | 1959.333 |
| Video | 28799 | 0.001 ms | 0.279 ms | 29.069 ms | 280.47% | 0.01% | 24.90% | 1065.000 |
| Audio | 2001 | 0.002 ms | 4.006 ms | 28.032 ms | 53.79% | 10.99% | 3.85% | 2.682 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28799 | 0.001 ms | 29.066 ms | 0 |
| Audio | 2001 | 0.985 ms | 23.820 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 25.3ms, max 38.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11108.0ms, video=11082.1ms (frame 663), diff=25.9ms
- 🟢 Pop #2 [R]: audio=11908.0ms, video=11884.5ms (frame 711), diff=23.5ms
- 🟢 Pop #3 [L]: audio=12708.0ms, video=12686.8ms (frame 759), diff=21.2ms
- 🟢 Pop #4 [R]: audio=13513.0ms, video=13505.8ms (frame 808), diff=7.2ms
- 🟢 Pop #5 [L]: audio=14313.0ms, video=14291.4ms (frame 855), diff=21.6ms
- 🟡 Pop #6 [R]: audio=15113.0ms, video=15077.1ms (frame 902), diff=35.9ms
- 🟡 Pop #7 [L]: audio=15918.0ms, video=15879.4ms (frame 950), diff=38.6ms
- 🟢 Pop #8 [R]: audio=16718.0ms, video=16698.4ms (frame 999), diff=19.6ms
- 🟢 Pop #9 [L]: audio=17518.0ms, video=17484.0ms (frame 1046), diff=34.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🔴 Too few valid marker samples

- Settling: 0s (pass/fail uses post-settling only)

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 663 at 00:11.0 of the 22.2 s video above.
