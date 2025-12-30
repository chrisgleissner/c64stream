# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2025-12-30 17:25:11 UTC

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
| CPU | 37.7% | 45.2% | 44.96% | 49.5% |
| RAM | 4653.67 MB | 4661.49 MB | 4660.71 MB | 4667.85 MB |
| GPU | 21.77% | 36.13% | 33.12% | 39.95% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 9.7ms, max 11.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2753.0ms, video=2741.3ms (frame 164), diff=11.7ms
- 🟢 Pop #2 [R]: audio=3553.0ms, video=3543.6ms (frame 212), diff=9.4ms
- 🟢 Pop #3 [L]: audio=4356.0ms, video=4345.9ms (frame 260), diff=10.1ms
- 🟢 Pop #4 [R]: audio=5158.0ms, video=5148.3ms (frame 308), diff=9.7ms
- 🟢 Pop #5 [L]: audio=5958.0ms, video=5950.6ms (frame 356), diff=7.4ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 180 frames (3.0s) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 1/180/180/180 | 1/5/5/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.357–10.330).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 7.338 | 0.000 | 0.000 | 7.338–7.338 |
| 2 | 1 | 10.330 | 0.000 | 0.000 | 10.330–10.330 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 10.8 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 164 at 00:02.7 of the 10.8 s video above.
