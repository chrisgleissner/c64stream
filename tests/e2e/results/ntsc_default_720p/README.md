# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2025-12-30 13:51:52 UTC

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
| CPU | 38.5% | 47.3% | 48.71% | 64.2% |
| RAM | 6409.88 MB | 6538.98 MB | 6507.1 MB | 6552.25 MB |
| GPU | 27.01% | 32.21% | 34.01% | 44.72% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 21.8ms, max 31.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2849.0ms, video=2841.6ms (frame 170), diff=7.4ms
- 🟢 Pop #2 [R]: audio=3670.0ms, video=3660.6ms (frame 219), diff=9.4ms
- 🟢 Pop #3 [L]: audio=4494.0ms, video=4462.9ms (frame 267), diff=31.1ms
- 🟢 Pop #4 [R]: audio=5297.0ms, video=5265.3ms (frame 315), diff=31.7ms
- 🟢 Pop #5 [L]: audio=6097.0ms, video=6067.6ms (frame 363), diff=29.4ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 180 frames (3.0s) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 5/2/2/3 | 6/1/1/2 | 0 | 0 |
| After settling | 1/180/180/180 | 1/5/5/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.474–10.447).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 10 | 4.401 | 0.268 | 0.668 | 4.062–4.730 |
| 2 | 1 | 7.455 | 0.000 | 0.000 | 7.455–7.455 |
| 3 | 1 | 10.447 | 0.000 | 0.000 | 10.447–10.447 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 10.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 171 at 00:02.8 of the 10.9 s video above.
