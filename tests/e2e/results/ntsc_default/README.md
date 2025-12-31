# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2025-12-31 14:00:28 UTC

## Test configuration

- Format: NTSC
- Frames: 480
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
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.9% | 51.7% | 52.46% | 55.1% |
| RAM | 4421.27 MB | 4463.83 MB | 4459.91 MB | 4473.31 MB |
| GPU | 0.0% | 0.68% | 0.66% | 1.48% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 18.6ms, max 22.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2697.0ms, video=2674.4ms (frame 160), diff=22.6ms
- 🟢 Pop #2 [R]: audio=3497.0ms, video=3476.7ms (frame 208), diff=20.3ms
- 🟢 Pop #3 [L]: audio=4297.0ms, video=4279.1ms (frame 256), diff=17.9ms
- 🟢 Pop #4 [R]: audio=5102.0ms, video=5081.4ms (frame 304), diff=20.6ms
- 🟢 Pop #5 [L]: audio=5902.0ms, video=5883.7ms (frame 352), diff=18.3ms
- 🟢 Pop #6 [R]: audio=6702.0ms, video=6686.1ms (frame 400), diff=15.9ms
- 🟢 Pop #7 [L]: audio=7508.0ms, video=7488.4ms (frame 448), diff=19.6ms
- 🟢 Pop #8 [R]: audio=8308.0ms, video=8290.7ms (frame 496), diff=17.3ms
- 🟢 Pop #9 [L]: audio=9108.0ms, video=9093.0ms (frame 544), diff=15.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.290–13.289).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 4.413 | 0.000 | 0.000 | 4.413–4.413 |

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
- Taken from frame 160 at 00:02.7 of the 13.8 s video above.
