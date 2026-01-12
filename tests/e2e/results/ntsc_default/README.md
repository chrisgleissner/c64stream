# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-12 23:25:35 UTC

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
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 30740/30803 packets (28741 video, 1999 audio, minor loss)
- ✅ Network Timing: span=8007.1ms, video_mean=342.2us, audio_mean=4004.4us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.2s duration

### Resource Usage

During the test's processing window (18.6s, 38 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 48.7% | 56.5% | 57.91% | 85.3% |
| RAM | 4490.65 MB | 4537.59 MB | 4535.26 MB | 4562.07 MB |
| GPU | 2.67% | 9.73% | 10.7% | 26.64% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8007.139 ms
- Total packets analyzed: 25399

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25399 | 0.001 ms | 0.630 ms | 10.203 ms | 169.80% | 10.22% | 15.74% | 16.621 |
| Video | 23401 | 0.001 ms | 0.342 ms | 9.666 ms | 109.44% | 11.08% | 8.60% | 8.157 |
| Audio | 1998 | 0.003 ms | 4.004 ms | 10.203 ms | 18.68% | 0.75% | 0.10% | 1.530 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23401 | 0.021 ms | 9.386 ms | 0 |
| Audio | 1998 | 0.122 ms | 6.203 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 16.7ms, max 20.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11047.0ms, video=11048.7ms (frame 661), diff=1.7ms
- 🟢 Pop #2 [R]: audio=11868.0ms, video=11851.0ms (frame 709), diff=17.0ms
- 🟢 Pop #3 [L]: audio=12674.0ms, video=12653.4ms (frame 757), diff=20.6ms
- 🟢 Pop #4 [R]: audio=13473.0ms, video=13455.7ms (frame 805), diff=17.3ms
- 🟢 Pop #5 [L]: audio=14276.0ms, video=14258.0ms (frame 853), diff=18.0ms
- 🟢 Pop #6 [R]: audio=15080.0ms, video=15060.3ms (frame 901), diff=19.7ms
- 🟢 Pop #7 [L]: audio=15881.0ms, video=15862.7ms (frame 949), diff=18.3ms
- 🟢 Pop #8 [R]: audio=16682.0ms, video=16665.0ms (frame 997), diff=17.0ms
- 🟢 Pop #9 [L]: audio=17488.0ms, video=17467.3ms (frame 1045), diff=20.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 2/2/2/2 | 2/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.664–21.663).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 16.681 | 0.017 | 0.033 | 16.665–16.698 |

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
- Taken from frame 661 at 00:11.0 of the 22.2 s video above.
