# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-09 15:10:16 UTC

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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 30738/30803 packets (28738 video, 2000 audio, minor loss)
- ✅ Network Timing: span=8006.1ms, video_mean=286.3us, audio_mean=4004.6us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 10.9 MB
- ✅ Content Integrity: 22.0s duration

### Resource Usage

During the test's processing window (18.1s, 37 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 52.2% | 61.4% | 63.17% | 78.6% |
| RAM | 6658.55 MB | 6797.22 MB | 6812.55 MB | 6934.26 MB |
| GPU | 7.63% | 14.78% | 14.33% | 17.68% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8006.062 ms
- Total packets analyzed: 29963

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 29963 | 0.001 ms | 0.534 ms | 10.622 ms | 209.46% | 0.00% | 35.00% | 2419.000 |
| Video | 27964 | 0.001 ms | 0.286 ms | 9.463 ms | 202.54% | 0.00% | 30.35% | 1243.000 |
| Audio | 1999 | 0.005 ms | 4.005 ms | 10.622 ms | 27.03% | 2.75% | 0.15% | 1.655 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 27964 | 0.001 ms | 9.461 ms | 0 |
| Audio | 1999 | 0.599 ms | 6.383 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 7.0ms, max 10.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10938.0ms, video=10931.7ms (frame 654), diff=6.3ms
- 🟢 Pop #2 [R]: audio=11738.0ms, video=11734.0ms (frame 702), diff=4.0ms
- 🟢 Pop #3 [L]: audio=12545.0ms, video=12536.4ms (frame 750), diff=8.6ms
- 🟢 Pop #4 [R]: audio=13345.0ms, video=13338.7ms (frame 798), diff=6.3ms
- 🟢 Pop #5 [L]: audio=14147.0ms, video=14141.0ms (frame 846), diff=6.0ms
- 🟢 Pop #6 [R]: audio=14951.0ms, video=14943.3ms (frame 894), diff=7.7ms
- 🟢 Pop #7 [L]: audio=15753.0ms, video=15745.7ms (frame 942), diff=7.3ms
- 🟢 Pop #8 [R]: audio=16554.0ms, video=16548.0ms (frame 990), diff=6.0ms
- 🟢 Pop #9 [L]: audio=17361.0ms, video=17350.3ms (frame 1038), diff=10.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 2/2/2/2 | 2/1/1/1 | 0 | 0 |
| After settling | 14/2/2/3 | 20/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.547–21.529).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 30 | 16.827 | 0.754 | 2.441 | 15.762–18.203 |
| 2 | 4 | 13.105 | 0.043 | 0.100 | 13.055–13.155 |

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
