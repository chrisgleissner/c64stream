# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

- Generated: 2026-01-22 19:55:22 UTC
- Git Branch: cursor/c64-stream-effects-filter-4ac5
- Git ID: c325f69
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
- Disk (/): 1.8T total, 972G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30758, Missing 45 (0.15%)
- ✅ Network Timing: span=8009.6ms, video_mean=466.2us, audio_mean=4004.4us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.4s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 91.9% | 93.3% | 93.43% | 96.3% |
| RAM | 11670.85 MB | 11769.57 MB | 11761.94 MB | 11803.71 MB |
| GPU | 0% | 0% | 0% | 0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8009.608 ms
- Total packets analyzed: 19179

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 19179 | 0.001 ms | 0.835 ms | 17.208 ms | 188.46% | 26.20% | 23.56% | 25.223 |
| Video | 17179 | 0.001 ms | 0.466 ms | 14.713 ms | 201.45% | 28.34% | 15.68% | 17.879 |
| Audio | 2000 | 0.001 ms | 4.004 ms | 17.208 ms | 55.66% | 13.85% | 4.75% | 3.139 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17179 | 0.092 ms | 14.433 ms | 0 |
| Audio | 2000 | 0.771 ms | 13.211 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 99.3ms, max 770.0ms

#### Sync Details

- • Pop #1 [L]: audio=9911.0ms, video=10681.0ms (frame 639), diff=770.0ms
- 🟢 Pop #2 [R]: audio=10711.0ms, video=10697.7ms (frame 640), diff=13.3ms
- 🟢 Pop #3 [L]: audio=11516.0ms, video=11500.0ms (frame 688), diff=16.0ms
- 🟢 Pop #4 [R]: audio=12318.0ms, video=12302.3ms (frame 736), diff=15.7ms
- 🟢 Pop #5 [L]: audio=13119.0ms, video=13104.7ms (frame 784), diff=14.3ms
- 🟢 Pop #6 [R]: audio=13924.0ms, video=13907.0ms (frame 832), diff=17.0ms
- 🟢 Pop #7 [L]: audio=14726.0ms, video=14709.3ms (frame 880), diff=16.7ms
- 🟢 Pop #8 [R]: audio=15526.0ms, video=15511.7ms (frame 928), diff=14.3ms
- 🟢 Pop #9 [L]: audio=16330.0ms, video=16314.0ms (frame 976), diff=16.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/4/6/434 | 6/1/5/5 | 1 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

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
- Taken from frame 640 at 00:10.7 of the 21.5 s video above.
