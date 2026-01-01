# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2026-01-01 12:26:14 UTC

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
| CPU | 38.5% | 44.05% | 44.72% | 50.0% |
| RAM | 6789.32 MB | 6812.13 MB | 6809.57 MB | 6823.9 MB |
| GPU | 21.19% | 42.62% | 38.43% | 43.8% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17999 | 0.001 ms | 31.141 ms | 0 |
| Audio | 1249 | 0.335 ms | 25.687 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 10.7ms, max 14.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2886.0ms, video=2891.7ms (frame 173), diff=5.7ms
- 🟢 Pop #2 [R]: audio=3686.0ms, video=3694.0ms (frame 221), diff=8.0ms
- 🟢 Pop #3 [L]: audio=4510.0ms, video=4496.4ms (frame 269), diff=13.6ms
- 🟢 Pop #4 [R]: audio=5313.0ms, video=5298.7ms (frame 317), diff=14.3ms
- 🟢 Pop #5 [L]: audio=6113.0ms, video=6101.0ms (frame 365), diff=12.0ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 5/2/2/3 | 6/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 6/1/1/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.507–10.480).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 11 | 6.580 | 0.328 | 1.204 | 6.201–7.405 |
| 2 | 5 | 4.450 | 0.127 | 0.301 | 4.279–4.580 |
| 3 | 1 | 10.480 | 0.000 | 0.000 | 10.480–10.480 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 11.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 173 at 00:02.9 of the 11.0 s video above.
