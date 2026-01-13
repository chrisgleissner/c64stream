# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2026-01-13 16:34:49 UTC
Git Branch: test/modularize-e2e
Git ID: f4f3cd8
Environment: local

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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 29194, Received 29156, Missing 38 (0.13%)
- ✅ Network Timing: span=7968.6ms, video_mean=344.8us, audio_mean=4001.1us
- ✅ Frame Processing: 397 frames processed
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.8% | 55.7% | 56.46% | 68.7% |
| RAM | 5669.89 MB | 5751.57 MB | 5747.61 MB | 5792.27 MB |
| GPU | 30.43% | 89.83% | 73.79% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 7968.591 ms
- Total packets analyzed: 25105

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25105 | 0.001 ms | 0.635 ms | 18.202 ms | 165.50% | 8.87% | 14.95% | 15.407 |
| Video | 23114 | 0.001 ms | 0.345 ms | 5.137 ms | 92.33% | 9.61% | 7.67% | 6.642 |
| Audio | 1991 | 0.002 ms | 4.001 ms | 18.202 ms | 16.38% | 0.25% | 0.05% | 1.441 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23114 | 0.020 ms | 4.849 ms | 0 |
| Audio | 1991 | 0.055 ms | 14.204 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 13.2ms, max 17.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9870.0ms, video=9875.3ms (frame 495), diff=5.3ms
- 🟢 Pop #2 [R]: audio=10847.0ms, video=10832.9ms (frame 543), diff=14.1ms
- 🟢 Pop #3 [L]: audio=11803.0ms, video=11790.5ms (frame 591), diff=12.5ms
- 🟢 Pop #4 [R]: audio=12764.0ms, video=12748.1ms (frame 639), diff=15.9ms
- 🟢 Pop #5 [L]: audio=13721.0ms, video=13705.7ms (frame 687), diff=15.3ms
- 🟢 Pop #6 [R]: audio=14676.0ms, video=14663.3ms (frame 735), diff=12.7ms
- 🟢 Pop #7 [L]: audio=15638.0ms, video=15620.9ms (frame 783), diff=17.1ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (397 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.440–17.380).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 12.500 | 0.000 | 0.000 | 12.500–12.500 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 495 at 00:09.9 of the 19.2 s video above.
