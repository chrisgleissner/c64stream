# C64 Stream E2E Test Report

Generated: 2025-12-29 09:34:05 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
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

During the test's processing window (4.6s, 10 of 27 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 64.5% | 74.85% | 74.47% | 80.2% |
| RAM | 6849.0 MB | 6860.19 MB | 6858.75 MB | 6865.52 MB |
| GPU | 30.34% | 32.24% | 36.44% | 46.68% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 41.6ms, max 43.9ms

#### Sync Details

- 🟡 Pop #1 [L]: audio=8134.0ms, video=8090.1ms (frame 484), diff=43.9ms
- 🟡 Pop #2 [R]: audio=8934.0ms, video=8892.5ms (frame 532), diff=41.5ms
- 🟡 Pop #3 [L]: audio=9734.0ms, video=9694.8ms (frame 580), diff=39.2ms
- 🟡 Pop #4 [R]: audio=10540.0ms, video=10497.1ms (frame 628), diff=42.9ms
- 🟡 Pop #5 [L]: audio=11340.0ms, video=11299.4ms (frame 676), diff=40.6ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 181 frames (3.0s) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 1/181/181/181 | 0/0/0/0 | 0 | 1 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.706–15.696).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 7.756 | 0.017 | 0.034 | 7.739–7.773 |
| 2 | 1 | 12.687 | 0.000 | 0.000 | 12.687–12.687 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 484 at 00:08.1 of the 19.9 s video above.
