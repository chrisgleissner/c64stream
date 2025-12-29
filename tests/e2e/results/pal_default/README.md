# C64 Stream E2E Test Report

Generated: 2025-12-29 16:35:10 UTC

## Test configuration

- Format: PAL
- Frames: 400
- Duration: 8.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 30.0.2.1-3build1
- CPU: Intel(R) Core(TM) i5-14600K (20 cores)
- RAM: 62Gi total, 53Gi available
- Disk (/): 916G total, 497G available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 28.3% | 33.95% | 33.93% | 39.3% |
| RAM | 5947.96 MB | 6039.55 MB | 6035.62 MB | 6117.69 MB |
| GPU | 86.0% | 92.48% | 92.2% | 95.68% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 11.8ms, max 15.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8028.0ms, video=8039.9ms (frame 403), diff=11.9ms
- 🟢 Pop #2 [R]: audio=8982.0ms, video=8997.5ms (frame 451), diff=15.5ms
- 🟢 Pop #3 [L]: audio=9945.0ms, video=9955.1ms (frame 499), diff=10.1ms
- 🟢 Pop #4 [R]: audio=10900.0ms, video=10912.7ms (frame 547), diff=12.7ms
- 🟢 Pop #5 [L]: audio=11857.0ms, video=11870.3ms (frame 595), diff=13.3ms
- 🟢 Pop #6 [R]: audio=12817.0ms, video=12827.9ms (frame 643), diff=10.9ms
- 🟢 Pop #7 [L]: audio=13774.0ms, video=13765.6ms (frame 690), diff=8.4ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (401 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 10/2/2/5 | 10/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.601–18.534).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 11 | 13.586 | 0.231 | 0.798 | 13.287–14.085 |
| 2 | 2 | 14.793 | 0.070 | 0.140 | 14.723–14.863 |
| 3 | 1 | 15.521 | 0.000 | 0.000 | 15.521–15.521 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 404 at 00:08.1 of the 19.0 s video above.
