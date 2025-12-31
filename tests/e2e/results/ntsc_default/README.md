# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2025-12-31 12:28:20 UTC

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
| CPU | 59.6% | 61.8% | 61.7% | 63.9% |
| RAM | 4020.32 MB | 4053.67 MB | 4051.52 MB | 4066.79 MB |
| GPU | 26.73% | 44.39% | 38.87% | 45.94% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 24.0ms, max 37.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2676.0ms, video=2657.7ms (frame 159), diff=18.3ms
- 🟢 Pop #2 [R]: audio=3476.0ms, video=3460.0ms (frame 207), diff=16.0ms
- 🟢 Pop #3 [L]: audio=4276.0ms, video=4262.4ms (frame 255), diff=13.6ms
- 🟡 Pop #4 [R]: audio=5102.0ms, video=5064.7ms (frame 303), diff=37.3ms
- 🟢 Pop #5 [L]: audio=5902.0ms, video=5867.0ms (frame 351), diff=35.0ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/3/3/3 | 2/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 1/5/5/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.273–10.263).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 3 | 4.575 | 0.021 | 0.050 | 4.547–4.597 |
| 2 | 1 | 10.263 | 0.000 | 0.000 | 10.263–10.263 |

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
- Taken from frame 159 at 00:02.6 of the 10.7 s video above.
