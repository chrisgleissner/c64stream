# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2025-12-31 14:08:52 UTC

## Test configuration

- Format: PAL
- Frames: 400
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
| CPU | 40.7% | 45.05% | 48.23% | 67.1% |
| RAM | 4375.22 MB | 4729.96 MB | 4627.44 MB | 4735.49 MB |
| GPU | 0.0% | 0.44% | 0.41% | 0.67% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 14.4ms, max 19.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2806.0ms, video=2813.0ms (frame 141), diff=7.0ms
- 🟢 Pop #2 [R]: audio=3764.0ms, video=3770.6ms (frame 189), diff=6.6ms
- 🟢 Pop #3 [L]: audio=4745.0ms, video=4728.2ms (frame 237), diff=16.8ms
- 🟢 Pop #4 [R]: audio=5702.0ms, video=5685.8ms (frame 285), diff=16.2ms
- 🟢 Pop #5 [L]: audio=6660.0ms, video=6643.4ms (frame 333), diff=16.6ms
- 🟢 Pop #6 [R]: audio=7620.0ms, video=7601.0ms (frame 381), diff=19.0ms
- 🟢 Pop #7 [L]: audio=8577.0ms, video=8558.6ms (frame 429), diff=18.4ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (401 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 8/2/2/3 | 5/1/2/2 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.354–13.307).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 3 | 4.628 | 0.016 | 0.040 | 4.608–4.648 |
| 2 | 1 | 4.010 | 0.000 | 0.000 | 4.010–4.010 |

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
- Taken from frame 141 at 00:02.8 of the 13.8 s video above.
