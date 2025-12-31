# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2025-12-31 00:31:56 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
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
| CPU | 90.8% | 91.65% | 91.61% | 92.4% |
| RAM | 4290.66 MB | 4328.67 MB | 4326.03 MB | 4340.92 MB |
| GPU | 27.04% | 40.98% | 37.04% | 41.88% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 20.0ms, max 24.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2716.0ms, video=2691.1ms (frame 161), diff=24.9ms
- 🟢 Pop #2 [R]: audio=3516.0ms, video=3493.5ms (frame 209), diff=22.5ms
- 🟢 Pop #3 [L]: audio=4316.0ms, video=4295.8ms (frame 257), diff=20.2ms
- 🟢 Pop #4 [R]: audio=5118.0ms, video=5098.1ms (frame 305), diff=19.9ms
- 🟢 Pop #5 [L]: audio=5921.0ms, video=5900.4ms (frame 353), diff=20.6ms
- 🟢 Pop #6 [R]: audio=6721.0ms, video=6702.8ms (frame 401), diff=18.2ms
- 🟢 Pop #7 [L]: audio=7524.0ms, video=7505.1ms (frame 449), diff=18.9ms
- 🟢 Pop #8 [R]: audio=8326.0ms, video=8307.4ms (frame 497), diff=18.6ms
- 🟢 Pop #9 [L]: audio=9126.0ms, video=9109.8ms (frame 545), diff=16.2ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 16/2/2/119 | 14/1/1/1 | 1 | 0 |
| After settling | 30/2/2/2 | 33/1/1/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.201–10.664).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 63 | 6.352 | 1.097 | 3.878 | 4.279–8.157 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 13.8 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 161 at 00:02.7 of the 13.8 s video above.
