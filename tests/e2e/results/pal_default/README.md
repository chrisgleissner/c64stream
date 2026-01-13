# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2026-01-12 23:57:21 UTC

## Test configuration

- Format: PAL
- Frames: 400
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
- RAM: 31Gi total, 27Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 29134 packets (27143 video, 1991 audio)
- ✅ Network Timing: span=7963.0ms, video_mean=337.6us, audio_mean=4001.4us
- ✅ Frame Processing: Accuracy: 100.0%
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 of 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 41.5% | 47.7% | 47.47% | 57.3% |
| RAM | 3934.19 MB | 4013.68 MB | 4002.58 MB | 4040.57 MB |
| GPU | 30.4% | 30.4% | 45.48% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 29134 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 7963.027 ms
- Total packets analyzed: 25580

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 25580 | 0.001 ms | 0.623 ms | 6.735 ms | 165.78% | 7.51% | 13.82% | 14.843 |
| Video | 23590 | 0.001 ms | 0.338 ms | 3.589 ms | 86.20% | 8.13% | 6.60% | 6.583 |
| Audio | 1990 | 1.491 ms | 4.001 ms | 6.735 ms | 13.94% | 0.45% | 0.00% | 1.447 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23590 | 0.017 ms | 3.299 ms | 0 |
| Audio | 1990 | 0.044 ms | 2.736 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 0.0ms, max 0.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9895.0ms, video=9875.3ms (frame 495), diff=19.7ms
- 🟢 Pop #2 [R]: audio=10852.0ms, video=10832.9ms (frame 543), diff=19.1ms
- 🟢 Pop #3 [L]: audio=11808.0ms, video=11790.5ms (frame 591), diff=17.5ms
- 🟢 Pop #4 [R]: audio=12767.0ms, video=12748.1ms (frame 639), diff=18.9ms
- 🟢 Pop #5 [L]: audio=13726.0ms, video=13705.7ms (frame 687), diff=20.3ms
- 🟢 Pop #6 [R]: audio=14681.0ms, video=14663.3ms (frame 735), diff=17.7ms
- 🟢 Pop #7 [L]: audio=15642.0ms, video=15620.9ms (frame 783), diff=21.1ms

### Frame Progression

- 🟢 Frame sequence verified (397.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 3.0/2.0/2.0/2.0 | 3.0/1.0/1.0/1.0 | 0.0 | 0.0 |
| After settling | 0.0/0.0/0.0/0.0 | 2.0/1.0/1.0/1.0 | 0.0 | 0.0 |

See [playback.csv](playback.csv) for details.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 9.520 | 0.000 | 0.000 | 9.520–9.520 |
| 2 | 1 | 10.700 | 0.000 | 0.000 | 10.700–10.700 |
| 3 | 1 | 11.980 | 0.000 | 0.000 | 11.980–11.980 |
| 4 | 1 | 14.540 | 0.000 | 0.000 | 14.540–14.540 |
| 5 | 1 | 16.320 | 0.000 | 0.000 | 16.320–16.320 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.3 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)