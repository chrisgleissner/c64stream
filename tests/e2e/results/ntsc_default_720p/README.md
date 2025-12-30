# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2025-12-30 11:30:50 UTC

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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 27 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 30.4% | 50.95% | 49.75% | 59.1% |
| RAM | 5874.04 MB | 5943.55 MB | 5926.14 MB | 5955.46 MB |
| GPU | 2.25% | 3.04% | 4.44% | 8.64% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 7.9ms, max 9.2ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8116.0ms, video=8106.8ms (frame 485), diff=9.2ms
- 🟢 Pop #2 [R]: audio=8918.0ms, video=8909.2ms (frame 533), diff=8.8ms
- 🟢 Pop #3 [L]: audio=9718.0ms, video=9711.5ms (frame 581), diff=6.5ms
- 🟢 Pop #4 [R]: audio=10521.0ms, video=10513.8ms (frame 629), diff=7.2ms
- 🟢 Pop #5 [L]: audio=11324.0ms, video=11316.2ms (frame 677), diff=7.8ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 181 frames (3.0s) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 30/2/2/2 | 29/1/1/1 | 0 | 0 |
| After settling | 11/2/2/181 | 12/1/1/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.722–15.696).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 52 | 11.306 | 0.900 | 2.925 | 9.762–12.687 |
| 2 | 17 | 8.154 | 0.299 | 0.936 | 7.756–8.692 |
| 3 | 1 | 15.696 | 0.000 | 0.000 | 15.696–15.696 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 16.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 485 at 00:08.1 of the 16.2 s video above.
