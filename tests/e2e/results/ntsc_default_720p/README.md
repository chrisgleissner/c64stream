# C64 Stream E2E Test Report

Generated: 2025-12-29 10:11:11 UTC

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
| CPU | 32.1% | 49.6% | 51.88% | 93.5% |
| RAM | 6822.1 MB | 6869.32 MB | 7143.65 MB | 7805.71 MB |
| GPU | 0.37% | 3.08% | 2.55% | 6.08% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 30.6ms, max 34.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8225.0ms, video=8190.4ms (frame 490), diff=34.6ms
- 🟢 Pop #2 [R]: audio=9025.0ms, video=8992.7ms (frame 538), diff=32.3ms
- 🟢 Pop #3 [L]: audio=9825.0ms, video=9795.1ms (frame 586), diff=29.9ms
- 🟢 Pop #4 [R]: audio=10630.0ms, video=10597.4ms (frame 634), diff=32.6ms
- 🟢 Pop #5 [L]: audio=11430.0ms, video=11399.7ms (frame 682), diff=30.3ms
- 🟢 Pop #6 [R]: audio=12230.0ms, video=12202.1ms (frame 730), diff=27.9ms
- 🟢 Pop #7 [L]: audio=13036.0ms, video=13004.4ms (frame 778), diff=31.6ms
- 🟢 Pop #8 [R]: audio=13836.0ms, video=13806.7ms (frame 826), diff=29.3ms
- 🟢 Pop #9 [L]: audio=14636.0ms, video=14609.0ms (frame 874), diff=27.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 10/2/2/2 | 8/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.806–18.821).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 7 | 14.088 | 0.137 | 0.368 | 13.907–14.275 |
| 2 | 3 | 15.757 | 0.034 | 0.084 | 15.712–15.796 |
| 3 | 1 | 15.010 | 0.000 | 0.000 | 15.010–15.010 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 23.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 490 at 00:08.2 of the 23.0 s video above.
