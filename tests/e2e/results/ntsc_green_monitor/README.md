# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2026-01-01 12:31:29 UTC

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
| CPU | 88.8% | 91.85% | 91.7% | 93.0% |
| RAM | 6971.75 MB | 7005.77 MB | 7003.58 MB | 7014.72 MB |
| GPU | 24.21% | 28.06% | 27.88% | 29.34% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17999 | 0.001 ms | 11.047 ms | 0 |
| Audio | 1250 | 0.980 ms | 9.650 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 8.2ms, max 10.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2886.0ms, video=2891.7ms (frame 173), diff=5.7ms
- 🟢 Pop #2 [R]: audio=3686.0ms, video=3694.0ms (frame 221), diff=8.0ms
- 🟢 Pop #3 [L]: audio=4489.0ms, video=4479.7ms (frame 268), diff=9.3ms
- 🟢 Pop #4 [R]: audio=5292.0ms, video=5282.0ms (frame 316), diff=10.0ms
- 🟢 Pop #5 [L]: audio=6092.0ms, video=6084.3ms (frame 364), diff=7.7ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (435 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 13/2/2/130 | 18/1/1/1 | 1 | 0 |
| After settling | 0/0/0/0 | 35/1/1/2 | 1 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.201–7.455).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 36 | 5.729 | 0.966 | 3.310 | 4.095–7.405 |

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
