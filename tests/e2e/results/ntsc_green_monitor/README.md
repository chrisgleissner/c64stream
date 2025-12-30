# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2025-12-30 17:28:06 UTC

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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 27 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 91.1% | 91.5% | 91.55% | 92.4% |
| RAM | 4737.31 MB | 4764.63 MB | 4762.42 MB | 4770.66 MB |
| GPU | 24.86% | 30.95% | 31.92% | 39.91% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 17.7ms, max 22.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2646.0ms, video=2641.0ms (frame 158), diff=5.0ms
- 🟢 Pop #2 [R]: audio=3449.0ms, video=3426.6ms (frame 205), diff=22.4ms
- 🟢 Pop #3 [L]: audio=4249.0ms, video=4228.9ms (frame 253), diff=20.1ms
- 🟢 Pop #4 [R]: audio=5052.0ms, video=5031.3ms (frame 301), diff=20.7ms
- 🟢 Pop #5 [L]: audio=5854.0ms, video=5833.6ms (frame 349), diff=20.4ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (298 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 37/2/2/3 | 38/1/1/1 | 0 | 0 |
| After settling | 9/2/2/3 | 11/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.240–7.204).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 46 | 5.669 | 0.873 | 3.076 | 4.028–7.104 |

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
- Taken from frame 158 at 00:02.6 of the 10.7 s video above.
