# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

- Generated: 2026-01-22 19:40:35 UTC
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

- ✅ UDP Packet Reception: Expected 30803, Received 30767, Missing 36 (0.12%)
- ✅ Network Timing: span=8013.2ms, video_mean=315.8us, audio_mean=4005.0us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.1s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 39.3% | 46.4% | 50.36% | 72.4% |
| RAM | 11570.49 MB | 11824.47 MB | 11907.54 MB | 12551.41 MB |
| GPU | 0% | 0% | 0% | 0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8013.236 ms
- Total packets analyzed: 27373

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 27373 | 0.001 ms | 0.585 ms | 12.210 ms | 171.71% | 7.83% | 13.14% | 15.267 |
| Video | 25373 | 0.001 ms | 0.316 ms | 8.962 ms | 83.47% | 8.43% | 6.38% | 5.323 |
| Audio | 2000 | 0.002 ms | 4.005 ms | 12.210 ms | 14.33% | 0.70% | 0.10% | 1.382 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 25373 | 0.017 ms | 8.683 ms | 0 |
| Audio | 2000 | 0.048 ms | 8.209 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 21.1ms, max 24.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9817.0ms, video=9795.1ms (frame 586), diff=21.9ms
- 🟢 Pop #2 [R]: audio=10617.0ms, video=10597.4ms (frame 634), diff=19.6ms
- 🟢 Pop #3 [L]: audio=11422.0ms, video=11399.7ms (frame 682), diff=22.3ms
- 🟢 Pop #4 [R]: audio=12223.0ms, video=12202.1ms (frame 730), diff=20.9ms
- 🟢 Pop #5 [L]: audio=13023.0ms, video=13004.4ms (frame 778), diff=18.6ms
- 🟢 Pop #6 [R]: audio=13828.0ms, video=13806.7ms (frame 826), diff=21.3ms
- 🟢 Pop #7 [L]: audio=14630.0ms, video=14609.0ms (frame 874), diff=21.0ms
- 🟢 Pop #8 [R]: audio=15431.0ms, video=15411.4ms (frame 922), diff=19.6ms
- 🟢 Pop #9 [L]: audio=16238.0ms, video=16213.7ms (frame 970), diff=24.3ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 30/2/2/2 | 30/1/1/1 | 0 | 0 |
| After settling | 9/2/2/2 | 18/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.411–20.409).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 42 | 11.551 | 1.150 | 4.112 | 9.561–13.673 |
| 2 | 13 | 16.790 | 0.460 | 1.471 | 15.896–17.367 |
| 3 | 3 | 14.871 | 0.343 | 0.836 | 14.475–15.311 |

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
- Taken from frame 586 at 00:09.8 of the 21.2 s video above.
