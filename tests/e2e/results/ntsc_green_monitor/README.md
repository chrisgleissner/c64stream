# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2025-12-31 12:30:44 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
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

During the test's processing window (4.6s, 10 of 27 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 91.4% | 91.8% | 92.05% | 93.8% |
| RAM | 3969.68 MB | 4004.21 MB | 4006.55 MB | 4032.9 MB |
| GPU | 27.14% | 29.26% | 32.54% | 43.57% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 16.6ms, max 28.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2633.0ms, video=2624.3ms (frame 157), diff=8.7ms
- 🟢 Pop #2 [R]: audio=3454.0ms, video=3426.6ms (frame 205), diff=27.4ms
- 🟢 Pop #3 [L]: audio=4254.0ms, video=4245.6ms (frame 254), diff=8.4ms
- 🟢 Pop #4 [R]: audio=5060.0ms, video=5031.3ms (frame 301), diff=28.7ms
- 🟢 Pop #5 [L]: audio=5860.0ms, video=5850.3ms (frame 350), diff=9.7ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (420 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 10/2/2/116 | 17/1/1/1 | 1 | 0 |
| After settling | 0/0/0/0 | 25/1/1/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.201–7.204).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 27 | 5.608 | 0.941 | 3.075 | 4.012–7.087 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 10.7 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 157 at 00:02.6 of the 10.7 s video above.
