# C64 Stream E2E Test Report

## Scenario: NTSC Sharp Pixels

- Generated: 2026-01-23 12:52:55 UTC
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

- ✅ UDP Packet Reception: Expected 30803, Received 30759, Missing 44 (0.14%)
- ✅ Network Timing: span=8011.6ms, video_mean=364.4us, audio_mean=4005.0us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.7 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.3s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 62.1% | 64.9% | 65.56% | 80.2% |
| RAM | 4691.73 MB | 4846.46 MB | 4841.16 MB | 4919.65 MB |
| GPU | 0% | 3% | 10.2% | 39% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8011.571 ms
- Total packets analyzed: 23986

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 23986 | 0.001 ms | 0.668 ms | 7.223 ms | 167.71% | 13.08% | 18.10% | 17.662 |
| Video | 21987 | 0.001 ms | 0.364 ms | 4.372 ms | 120.99% | 14.25% | 10.68% | 9.025 |
| Audio | 1999 | 0.893 ms | 4.005 ms | 7.223 ms | 21.81% | 1.55% | 0.00% | 1.581 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 21987 | 0.024 ms | 4.092 ms | 0 |
| Audio | 1999 | 0.306 ms | 3.220 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 15.4ms, max 35.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9892.0ms, video=9878.6ms (frame 591), diff=13.4ms
- 🟢 Pop #2 [R]: audio=10693.0ms, video=10681.0ms (frame 639), diff=12.0ms
- 🟢 Pop #3 [L]: audio=11497.0ms, video=11483.3ms (frame 687), diff=13.7ms
- 🟢 Pop #4 [R]: audio=12298.0ms, video=12285.6ms (frame 735), diff=12.4ms
- 🟢 Pop #5 [L]: audio=13101.0ms, video=13088.0ms (frame 783), diff=13.0ms
- 🟢 Pop #6 [R]: audio=13905.0ms, video=13890.3ms (frame 831), diff=14.7ms
- 🟢 Pop #7 [L]: audio=14705.0ms, video=14692.6ms (frame 879), diff=12.4ms
- 🟢 Pop #8 [R]: audio=15506.0ms, video=15494.9ms (frame 927), diff=11.1ms
- 🟡 Pop #9 [L]: audio=16333.0ms, video=16297.3ms (frame 975), diff=35.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 1/3/3/3 | 1/2/2/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.494–20.493).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 15.545 | 0.017 | 0.034 | 15.528–15.562 |
| 2 | 1 | 10.547 | 0.000 | 0.000 | 10.547–10.547 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.6 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 591 at 00:09.9 of the 21.6 s video above.
