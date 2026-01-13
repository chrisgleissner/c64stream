# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2026-01-13 15:23:22 UTC

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

- ✅ UDP Packet Reception: 29166 packets (27173 video, 1993 audio)
- ✅ Network Timing: span=7971.4ms, video_mean=346.5us, audio_mean=4001.2us
- ✅ Frame Processing: 397 frames processed
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 54.3% | 58.3% | 58.96% | 70.2% |
| RAM | 5687.38 MB | 5772.05 MB | 5767.04 MB | 5825.19 MB |
| GPU | 30.43% | 89.83% | 69.68% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 7971.369 ms
- Total packets analyzed: 24994

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 24994 | 0.001 ms | 0.638 ms | 11.450 ms | 165.01% | 9.20% | 15.14% | 15.595 |
| Video | 23002 | 0.001 ms | 0.347 ms | 7.229 ms | 94.05% | 9.99% | 7.84% | 6.713 |
| Audio | 1992 | 0.007 ms | 4.001 ms | 11.450 ms | 15.40% | 0.50% | 0.05% | 1.439 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23002 | 0.020 ms | 6.940 ms | 0 |
| Audio | 1992 | 0.058 ms | 7.452 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 11.7ms, max 13.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9809.0ms, video=9795.5ms (frame 491), diff=13.5ms
- 🟢 Pop #2 [R]: audio=10764.0ms, video=10753.1ms (frame 539), diff=10.9ms
- 🟢 Pop #3 [L]: audio=11720.0ms, video=11710.7ms (frame 587), diff=9.3ms
- 🟢 Pop #4 [R]: audio=12681.0ms, video=12668.3ms (frame 635), diff=12.7ms
- 🟢 Pop #5 [L]: audio=13638.0ms, video=13625.9ms (frame 683), diff=12.1ms
- 🟢 Pop #6 [R]: audio=14593.0ms, video=14583.5ms (frame 731), diff=9.5ms
- 🟢 Pop #7 [L]: audio=15555.0ms, video=15541.1ms (frame 779), diff=13.9ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (397 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/3/3/3 | 1/2/2/2 | 0 | 0 |
| After settling | 0/0/0/0 | 1/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.360–17.300).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 11.960 | 0.020 | 0.040 | 11.940–11.980 |
| 2 | 1 | 16.980 | 0.000 | 0.000 | 16.980–16.980 |

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
- Taken from frame 491 at 00:09.8 of the 19.1 s video above.
