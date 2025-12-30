# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2025-12-30 13:40:34 UTC

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
| CPU | 59.7% | 61.05% | 62.1% | 70.7% |
| RAM | 6669.7 MB | 6702.53 MB | 6697.13 MB | 6707.37 MB |
| GPU | 28.87% | 37.71% | 37.09% | 41.97% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 11.9ms, max 14.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2689.0ms, video=2674.4ms (frame 160), diff=14.6ms
- 🟢 Pop #2 [R]: audio=3489.0ms, video=3476.7ms (frame 208), diff=12.3ms
- 🟢 Pop #3 [L]: audio=4289.0ms, video=4279.1ms (frame 256), diff=9.9ms
- 🟢 Pop #4 [R]: audio=5094.0ms, video=5081.4ms (frame 304), diff=12.6ms
- 🟢 Pop #5 [L]: audio=5894.0ms, video=5883.7ms (frame 352), diff=10.3ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 180 frames (3.0s) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 4/2/2/180 | 4/1/1/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.290–10.263).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 4 | 6.619 | 0.037 | 0.100 | 6.569–6.669 |
| 2 | 1 | 7.271 | 0.000 | 0.000 | 7.271–7.271 |
| 3 | 1 | 10.263 | 0.000 | 0.000 | 10.263–10.263 |

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
- Taken from frame 160 at 00:02.7 of the 10.8 s video above.
