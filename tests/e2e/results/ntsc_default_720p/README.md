# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

- Generated: 2026-01-17 17:01:41 UTC
- Git Branch: fix/improve-keyboard-mappings
- Git ID: 5886543
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
- Disk (/): 1.8T total, 1022G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30760, Missing 43 (0.14%)
- ✅ Network Timing: span=8011.6ms, video_mean=313.6us, audio_mean=4005.0us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.3s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 43% | 48.1% | 50.18% | 68.5% |
| RAM | 4716.68 MB | 4779.55 MB | 4785.48 MB | 4869.99 MB |
| GPU | 17% | 21% | 29.34% | 44% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8011.562 ms
- Total packets analyzed: 27548

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 27548 | 0.001 ms | 0.582 ms | 8.802 ms | 170.89% | 8.09% | 13.43% | 15.125 |
| Video | 25548 | 0.001 ms | 0.314 ms | 6.229 ms | 76.75% | 8.71% | 6.69% | 5.193 |
| Audio | 2000 | 0.003 ms | 4.005 ms | 8.802 ms | 11.94% | 0.45% | 0.10% | 1.342 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 25548 | 0.020 ms | 5.949 ms | 0 |
| Audio | 2000 | 0.046 ms | 4.799 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 8.5ms, max 16.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9823.0ms, video=9828.5ms (frame 588), diff=5.5ms
- 🟢 Pop #2 [R]: audio=10623.0ms, video=10630.8ms (frame 636), diff=7.8ms
- 🟢 Pop #3 [L]: audio=11428.0ms, video=11433.2ms (frame 684), diff=5.2ms
- 🟢 Pop #4 [R]: audio=12228.0ms, video=12235.5ms (frame 732), diff=7.5ms
- 🟢 Pop #5 [L]: audio=13030.0ms, video=13037.8ms (frame 780), diff=7.8ms
- 🟢 Pop #6 [R]: audio=13834.0ms, video=13840.1ms (frame 828), diff=6.1ms
- 🟢 Pop #7 [L]: audio=14636.0ms, video=14642.5ms (frame 876), diff=6.5ms
- 🟢 Pop #8 [R]: audio=15458.0ms, video=15444.8ms (frame 924), diff=13.2ms
- 🟢 Pop #9 [L]: audio=16264.0ms, video=16247.1ms (frame 972), diff=16.9ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 2/2/2/2 | 2/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.444–20.443).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 3 | 14.854 | 0.052 | 0.117 | 14.810–14.927 |
| 2 | 1 | 9.912 | 0.000 | 0.000 | 9.912–9.912 |

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
