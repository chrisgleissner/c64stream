# C64 Stream E2E Test Report

## Scenario: NTSC Amber Monitor

- Generated: 2026-01-23 12:28:06 UTC
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

- ✅ UDP Packet Reception: Expected 30803, Received 30762, Missing 41 (0.13%)
- ✅ Network Timing: span=8011.0ms, video_mean=428.0us, audio_mean=4003.3us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.4 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.0s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 88.7% | 91.6% | 91.23% | 93.5% |
| RAM | 4684.81 MB | 4788.26 MB | 4778.61 MB | 4812.33 MB |
| GPU | 0% | 0% | 8.76% | 62% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8011.005 ms
- Total packets analyzed: 20717

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 20717 | 0.001 ms | 0.773 ms | 13.734 ms | 177.73% | 20.91% | 22.69% | 22.329 |
| Video | 18718 | 0.001 ms | 0.428 ms | 10.854 ms | 166.50% | 22.71% | 14.94% | 12.954 |
| Audio | 1999 | 0.001 ms | 4.003 ms | 13.734 ms | 45.07% | 10.41% | 3.55% | 2.605 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 18718 | 0.052 ms | 10.572 ms | 0 |
| Audio | 1999 | 0.609 ms | 9.733 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 18.2ms, max 21.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9782.0ms, video=9761.6ms (frame 584), diff=20.4ms
- 🟢 Pop #2 [R]: audio=10583.0ms, video=10564.0ms (frame 632), diff=19.0ms
- 🟢 Pop #3 [L]: audio=11388.0ms, video=11366.3ms (frame 680), diff=21.7ms
- 🟢 Pop #4 [R]: audio=12188.0ms, video=12168.6ms (frame 728), diff=19.4ms
- 🟢 Pop #5 [L]: audio=12990.0ms, video=12987.7ms (frame 777), diff=2.3ms
- 🟢 Pop #6 [R]: audio=13794.0ms, video=13773.3ms (frame 824), diff=20.7ms
- 🟢 Pop #7 [L]: audio=14596.0ms, video=14575.6ms (frame 872), diff=20.4ms
- 🟢 Pop #8 [R]: audio=15396.0ms, video=15377.9ms (frame 920), diff=18.1ms
- 🟢 Pop #9 [L]: audio=16202.0ms, video=16180.3ms (frame 968), diff=21.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=94, back_steps=25) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 41/2/2/3 | 82/1/1/5 | 25 | 0 |
| After settling | 5/2/2/2 | 77/1/1/3 | 25 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.377–17.735).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 191 | 12.993 | 2.368 | 7.939 | 9.411–17.350 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 584 at 00:09.8 of the 21.1 s video above.
