# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

- Generated: 2026-01-23 12:32:48 UTC
- Git Branch: doc/improvements
- Git ID: bd15461
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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 962G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30786, Missing 17 (0.06%)
- ✅ Network Timing: span=8018.2ms, video_mean=321.0us, audio_mean=4005.0us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.1s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 40.7% | 48.75% | 49.72% | 57.8% |
| RAM | 4456.93 MB | 4606.02 MB | 4595.77 MB | 4670.32 MB |
| GPU | 0% | 0% | 6.76% | 62% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8018.183 ms
- Total packets analyzed: 26983

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 26983 | 0.001 ms | 0.594 ms | 6.302 ms | 171.84% | 9.65% | 13.14% | 15.334 |
| Video | 24981 | 0.001 ms | 0.321 ms | 3.124 ms | 93.40% | 10.40% | 6.25% | 6.872 |
| Audio | 2002 | 1.810 ms | 4.005 ms | 6.302 ms | 15.19% | 0.35% | 0.00% | 1.465 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 24981 | 0.032 ms | 2.836 ms | 0 |
| Audio | 2002 | 0.055 ms | 2.301 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 12.3ms, max 13.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9841.0ms, video=9828.5ms (frame 588), diff=12.5ms
- 🟢 Pop #2 [R]: audio=10643.0ms, video=10630.8ms (frame 636), diff=12.2ms
- 🟢 Pop #3 [L]: audio=11447.0ms, video=11433.2ms (frame 684), diff=13.8ms
- 🟢 Pop #4 [R]: audio=12247.0ms, video=12235.5ms (frame 732), diff=11.5ms
- 🟢 Pop #5 [L]: audio=13049.0ms, video=13037.8ms (frame 780), diff=11.2ms
- 🟢 Pop #6 [R]: audio=13853.0ms, video=13840.1ms (frame 828), diff=12.9ms
- 🟢 Pop #7 [L]: audio=14655.0ms, video=14642.5ms (frame 876), diff=12.5ms
- 🟢 Pop #8 [R]: audio=15455.0ms, video=15444.8ms (frame 924), diff=10.2ms
- 🟢 Pop #9 [L]: audio=16261.0ms, video=16247.1ms (frame 972), diff=13.9ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.444–20.426).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 10.180 | 0.000 | 0.000 | 10.180–10.180 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.3 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 588 at 00:09.8 of the 21.3 s video above.
