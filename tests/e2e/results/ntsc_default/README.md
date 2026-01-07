# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-07 11:46:40 UTC

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

- ✅ UDP Packet Reception: 30803/30803 packets (28741 video, 1981 audio)
- ✅ Network Timing: span=8022.6ms, video_mean=278.7us, audio_mean=4005.2us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.1s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 65.9% | 80.75% | 77.42% | 91.6% |
| RAM | 6330.01 MB | 6469.99 MB | 6454.21 MB | 6499.39 MB |
| GPU | 46.4% | 52.61% | 52.33% | 57.15% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8022.565 ms
- Total packets analyzed: 30731

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30731 | 0.001 ms | 0.519 ms | 15.272 ms | 217.57% | 0.06% | 32.66% | 1238.750 |
| Video | 28740 | 0.001 ms | 0.279 ms | 9.625 ms | 216.49% | 0.07% | 28.04% | 649.500 |
| Audio | 1979 | 0.003 ms | 4.005 ms | 15.272 ms | 30.68% | 2.78% | 0.66% | 1.797 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28740 | 0.001 ms | 9.621 ms | 0 |
| Audio | 1979 | 0.725 ms | 11.063 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 21.7ms, max 24.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11054.0ms, video=11032.0ms (frame 660), diff=22.0ms
- 🟢 Pop #2 [R]: audio=11857.0ms, video=11834.3ms (frame 708), diff=22.7ms
- 🟢 Pop #3 [L]: audio=12657.0ms, video=12636.6ms (frame 756), diff=20.4ms
- 🟢 Pop #4 [R]: audio=13462.0ms, video=13439.0ms (frame 804), diff=23.0ms
- 🟢 Pop #5 [L]: audio=14262.0ms, video=14241.3ms (frame 852), diff=20.7ms
- 🟢 Pop #6 [R]: audio=15065.0ms, video=15043.6ms (frame 900), diff=21.4ms
- 🟢 Pop #7 [L]: audio=15870.0ms, video=15846.0ms (frame 948), diff=24.0ms
- 🟢 Pop #8 [R]: audio=16670.0ms, video=16648.3ms (frame 996), diff=21.7ms
- 🟢 Pop #9 [L]: audio=17470.0ms, video=17450.6ms (frame 1044), diff=19.4ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 28/2/2/3 | 26/1/1/2 | 0 | 0 |
| After settling | 29/2/2/2 | 32/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.664–21.646).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 72 | 14.784 | 1.989 | 6.886 | 10.882–17.768 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 660 at 00:11.0 of the 22.1 s video above.
