# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2026-01-07 15:06:27 UTC

## Test configuration

- Format: PAL
- Frames: 400
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
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 29194/29194 packets (27150 video, 1970 audio)
- ✅ Network Timing: span=7979.8ms, video_mean=293.4us, audio_mean=3999.5us
- ✅ Frame Processing: 2392 frames processed
- ✅ Video Recording: 10.9 MB
- ✅ Content Integrity: 22.0s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 45.8% | 53.1% | 56.69% | 81.6% |
| RAM | 8071.81 MB | 8096.31 MB | 8096.17 MB | 8113.37 MB |
| GPU | 22.2% | 49.5% | 43.41% | 52.01% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 7979.758 ms
- Total packets analyzed: 29122

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 29122 | 0.001 ms | 0.544 ms | 15.122 ms | 202.34% | 9.39% | 34.61% | 909.000 |
| Video | 27148 | 0.001 ms | 0.293 ms | 11.865 ms | 190.72% | 0.12% | 31.55% | 531.250 |
| Audio | 1969 | 0.007 ms | 4.000 ms | 15.122 ms | 22.35% | 1.12% | 0.20% | 1.452 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 27148 | 0.001 ms | 11.861 ms | 0 |
| Audio | 1969 | 0.367 ms | 10.871 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 10.6ms, max 16.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11006.0ms, video=11012.5ms (frame 552), diff=6.5ms
- 🟢 Pop #2 [R]: audio=11961.0ms, video=11970.1ms (frame 600), diff=9.1ms
- 🟢 Pop #3 [L]: audio=12921.0ms, video=12927.7ms (frame 648), diff=6.7ms
- 🟢 Pop #4 [R]: audio=13878.0ms, video=13885.3ms (frame 696), diff=7.3ms
- 🟢 Pop #5 [L]: audio=14857.0ms, video=14842.9ms (frame 744), diff=14.1ms
- 🟢 Pop #6 [R]: audio=15817.0ms, video=15800.5ms (frame 792), diff=16.5ms
- 🟢 Pop #7 [L]: audio=16772.0ms, video=16758.1ms (frame 840), diff=13.9ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (400 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 6/2/2/2 | 6/1/1/1 | 0 | 0 |
| After settling | 1/3/3/3 | 2/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.554–21.486).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 7 | 14.552 | 0.190 | 0.459 | 14.344–14.803 |
| 2 | 4 | 10.698 | 0.030 | 0.080 | 10.653–10.733 |

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
- Taken from frame 552 at 00:11.0 of the 22.0 s video above.
