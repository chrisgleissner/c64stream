# C64 Stream E2E Test Report

Generated: 2025-12-28 20:05:04 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 60.4% | 66.55% | 66.3% | 70.9% |
| RAM | 6710.4 MB | 6725.26 MB | 6724.14 MB | 6730.36 MB |
| GPU | 28.41% | 38.43% | 36.19% | 41.7% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 27.9ms, max 30.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8068.0ms, video=8040.0ms (frame 481), diff=28.0ms
- 🟢 Pop #2 [R]: audio=8889.0ms, video=8859.0ms (frame 530), diff=30.0ms
- 🟢 Pop #3 [L]: audio=9689.0ms, video=9661.4ms (frame 578), diff=27.6ms
- 🟢 Pop #4 [R]: audio=10494.0ms, video=10463.7ms (frame 626), diff=30.3ms
- 🟢 Pop #5 [L]: audio=11294.0ms, video=11266.0ms (frame 674), diff=28.0ms
- 🟢 Pop #6 [R]: audio=12094.0ms, video=12068.3ms (frame 722), diff=25.7ms
- 🟢 Pop #7 [L]: audio=12900.0ms, video=12870.7ms (frame 770), diff=29.3ms
- 🟢 Pop #8 [R]: audio=13700.0ms, video=13673.0ms (frame 818), diff=27.0ms
- 🟢 Pop #9 [L]: audio=14500.0ms, video=14475.3ms (frame 866), diff=24.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 10/2/2/2 | 9/1/1/1 | 0 | 0 |
| After settling | 2/2/2/3 | 1/2/2/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.672–18.671).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 17 | 8.140 | 0.245 | 0.752 | 7.773–8.525 |
| 2 | 2 | 14.625 | 0.016 | 0.033 | 14.609–14.642 |
| 3 | 1 | 15.662 | 0.000 | 0.000 | 15.662–15.662 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 22.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 481 at 00:08.0 of the 22.9 s video above.
