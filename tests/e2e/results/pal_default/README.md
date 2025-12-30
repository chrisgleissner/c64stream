# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2025-12-30 11:19:02 UTC

## Test configuration

- Format: PAL
- Frames: 400
- Duration: 8.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 48.0% | 51.55% | 55.66% | 77.8% |
| RAM | 5939.93 MB | 6023.74 MB | 6017.11 MB | 6046.98 MB |
| GPU | 27.83% | 30.02% | 33.49% | 48.21% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 8.3ms, max 10.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8046.0ms, video=8039.9ms (frame 403), diff=6.1ms
- 🟢 Pop #2 [R]: audio=9004.0ms, video=8997.5ms (frame 451), diff=6.5ms
- 🟢 Pop #3 [L]: audio=9964.0ms, video=9955.1ms (frame 499), diff=8.9ms
- 🟢 Pop #4 [R]: audio=10921.0ms, video=10912.7ms (frame 547), diff=8.3ms
- 🟢 Pop #5 [L]: audio=11878.0ms, video=11870.3ms (frame 595), diff=7.7ms
- 🟢 Pop #6 [R]: audio=12838.0ms, video=12827.9ms (frame 643), diff=10.1ms
- 🟢 Pop #7 [L]: audio=13796.0ms, video=13785.5ms (frame 691), diff=10.5ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (401 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 5/2/2/2 | 5/1/1/1 | 0 | 0 |
| After settling | 1/4/4/4 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.581–18.514).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 8 | 8.506 | 0.161 | 0.419 | 8.279–8.698 |
| 2 | 1 | 15.521 | 0.000 | 0.000 | 15.521–15.521 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 403 at 00:08.1 of the 19.0 s video above.
