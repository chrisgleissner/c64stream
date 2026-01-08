# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-08 23:51:40 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.2

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28737 video, 1962 audio)
- ✅ Network Timing: span=8021.6ms, video_mean=278.9us, audio_mean=4003.9us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.2s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 45.8% | 59.25% | 61.01% | 92.6% |
| RAM | 6313.59 MB | 6344.82 MB | 6340.37 MB | 6357.8 MB |
| GPU | 2.4% | 5.12% | 8.52% | 31.64% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8021.592 ms
- Total packets analyzed: 30705

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30705 | 0.001 ms | 0.517 ms | 11.819 ms | 208.86% | 0.32% | 32.18% | 1156.500 |
| Video | 28736 | 0.001 ms | 0.279 ms | 8.752 ms | 195.85% | 0.34% | 27.53% | 568.250 |
| Audio | 1960 | 0.010 ms | 4.004 ms | 11.819 ms | 23.53% | 1.73% | 0.05% | 1.538 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28736 | 0.001 ms | 8.748 ms | 0 |
| Audio | 1960 | 0.494 ms | 7.574 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 20.6ms, max 22.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11105.0ms, video=11082.1ms (frame 663), diff=22.9ms
- 🟢 Pop #2 [R]: audio=11905.0ms, video=11884.5ms (frame 711), diff=20.5ms
- 🟢 Pop #3 [L]: audio=12705.0ms, video=12686.8ms (frame 759), diff=18.2ms
- 🟢 Pop #4 [R]: audio=13510.0ms, video=13489.1ms (frame 807), diff=20.9ms
- 🟢 Pop #5 [L]: audio=14313.0ms, video=14291.4ms (frame 855), diff=21.6ms
- 🟢 Pop #6 [R]: audio=15113.0ms, video=15093.8ms (frame 903), diff=19.2ms
- 🟢 Pop #7 [L]: audio=15918.0ms, video=15896.1ms (frame 951), diff=21.9ms
- 🟢 Pop #8 [R]: audio=16718.0ms, video=16698.4ms (frame 999), diff=19.6ms
- 🟢 Pop #9 [L]: audio=17521.0ms, video=17500.8ms (frame 1047), diff=20.2ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 1/3/3/3 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.714–21.696).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 10.748 | 0.000 | 0.000 | 10.748–10.748 |

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
