# C64 Stream E2E Test Report

## Scenario: PAL Default

- Generated: 2026-01-23 12:56:30 UTC
- Git Branch: doc/improvements
- Git ID: bd15461
- Environment: local

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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 962G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 29194, Received 29132, Missing 62 (0.21%)
- ✅ Network Timing: span=7962.4ms, video_mean=335.6us, audio_mean=4001.4us
- ✅ Frame Processing: 400 frames processed
- ✅ Video Recording: 10.4 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (17.8s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 41.7% | 46.25% | 45.71% | 52.9% |
| RAM | 4760.66 MB | 4856.5 MB | 4851.92 MB | 4881.68 MB |
| GPU | 0% | 28% | 21.88% | 60% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 7962.431 ms
- Total packets analyzed: 25716

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25716 | 0.001 ms | 0.619 ms | 6.565 ms | 165.94% | 7.34% | 13.59% | 14.709 |
| Video | 23727 | 0.001 ms | 0.336 ms | 3.455 ms | 85.00% | 7.95% | 6.44% | 6.502 |
| Audio | 1989 | 1.873 ms | 4.001 ms | 6.565 ms | 13.16% | 0.15% | 0.00% | 1.409 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23727 | 0.017 ms | 3.166 ms | 0 |
| Audio | 1989 | 0.038 ms | 2.566 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 8.6ms, max 9.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9825.0ms, video=9815.5ms (frame 492), diff=9.5ms
- 🟢 Pop #2 [R]: audio=10782.0ms, video=10773.1ms (frame 540), diff=8.9ms
- 🟢 Pop #3 [L]: audio=11738.0ms, video=11730.7ms (frame 588), diff=7.3ms
- 🟢 Pop #4 [R]: audio=12697.0ms, video=12688.3ms (frame 636), diff=8.7ms
- 🟢 Pop #5 [L]: audio=13655.0ms, video=13645.9ms (frame 684), diff=9.1ms
- 🟢 Pop #6 [R]: audio=14611.0ms, video=14603.5ms (frame 732), diff=7.5ms
- 🟢 Pop #7 [L]: audio=15572.0ms, video=15581.0ms (frame 781), diff=9.0ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (400 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.400–20.360).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 16.390 | 0.010 | 0.020 | 16.380–16.400 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 20.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 493 at 00:09.8 of the 20.9 s video above.
