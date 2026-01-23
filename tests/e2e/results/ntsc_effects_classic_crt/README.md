# C64 Stream E2E Test Report

## Scenario: NTSC Effects Classic CRT

- Generated: 2026-01-23 13:41:08 UTC
- Git Branch: main
- Git ID: 47ccc5a
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
- Disk (/): 1.8T total, 962G available

## Test results

### Validation Summary

- ❓ UDP Packet Reception: Media source (no UDP)
- ❓ Network Timing: Media source (no UDP)
- ✅ Frame Processing: 477 frames processed
- ✅ Video Recording: 9.4 MB
- ❓ Content Integrity: Media source (no UDP)

### Resource Usage

During the test's processing window (15.9s, 30 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 42% | 93.05% | 80.08% | 95.1% |
| RAM | 5169.03 MB | 5222.73 MB | 5226.5 MB | 5275.29 MB |
| GPU | 14% | 39% | 38.13% | 48% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 7979.790 ms
- Total packets analyzed: 26186

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 26186 | 0.001 ms | 0.609 ms | 6.398 ms | 167.48% | 9.91% | 12.86% | 14.276 |
| Video | 24193 | 0.001 ms | 0.330 ms | 3.147 ms | 86.07% | 10.65% | 5.79% | 5.906 |
| Audio | 1993 | 1.999 ms | 4.001 ms | 6.398 ms | 12.47% | 0.05% | 0.00% | 1.391 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 24193 | 0.032 ms | 2.848 ms | 0 |
| Audio | 1993 | 0.051 ms | 2.399 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 530.9ms, max 1605.5ms

#### Sync Details

- • Pop #1 [L]: audio=4311.0ms, video=5098.1ms (frame 305), diff=787.1ms
- 🟢 Pop #2 [R]: audio=5111.0ms, video=5114.8ms (frame 306), diff=3.8ms
- • Pop #3 [L]: audio=5916.0ms, video=6702.8ms (frame 401), diff=786.8ms
- 🟢 Pop #4 [R]: audio=6718.0ms, video=6719.5ms (frame 402), diff=1.5ms
- • Pop #5 [L]: audio=7516.0ms, video=8307.4ms (frame 497), diff=791.4ms
- 🟢 Pop #6 [R]: audio=8323.0ms, video=8324.1ms (frame 498), diff=1.1ms
- 🟢 Pop #7 [L]: audio=9124.0ms, video=9126.5ms (frame 546), diff=2.5ms
- • Pop #8 [R]: audio=9925.0ms, video=9126.5ms (frame 546), diff=798.5ms
- • Pop #9 [L]: audio=10732.0ms, video=9126.5ms (frame 546), diff=1605.5ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=103, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 98/2/2/4 | 87/1/1/4 | 0 | 0 |
| After settling | 0/0/0/0 | 89/1/1/3 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 3.928–11.918).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 202 | 7.699 | 2.232 | 7.856 | 4.012–11.868 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 306 at 00:05.1 of the 19.1 s video above.
