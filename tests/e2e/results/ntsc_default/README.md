# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2025-12-31 12:45:50 UTC

## Test configuration

- Format: NTSC
- Frames: 600
- Duration: 10.0 seconds
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

During the test's processing window (9.6s, 20 of 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 56.9% | 60.75% | 60.48% | 63.5% |
| RAM | 4088.82 MB | 4113.79 MB | 4111.63 MB | 4141.41 MB |
| GPU | 27.9% | 42.52% | 38.59% | 44.58% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 36000 video, 2504 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 5.9ms, max 21.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2649.0ms, video=2641.0ms (frame 158), diff=8.0ms
- 🟢 Pop #2 [R]: audio=3449.0ms, video=3443.3ms (frame 206), diff=5.7ms
- 🟢 Pop #3 [L]: audio=4252.0ms, video=4245.6ms (frame 254), diff=6.4ms
- 🟢 Pop #4 [R]: audio=5054.0ms, video=5048.0ms (frame 302), diff=6.0ms
- 🟢 Pop #5 [L]: audio=5854.0ms, video=5850.3ms (frame 350), diff=3.7ms
- 🟢 Pop #6 [R]: audio=6657.0ms, video=6635.9ms (frame 397), diff=21.1ms
- 🟢 Pop #7 [L]: audio=7460.0ms, video=7455.0ms (frame 446), diff=5.0ms
- 🟢 Pop #8 [R]: audio=8260.0ms, video=8257.3ms (frame 494), diff=2.7ms
- 🟢 Pop #9 [L]: audio=9062.0ms, video=9059.6ms (frame 542), diff=2.4ms
- 🟢 Pop #10 [R]: audio=9865.0ms, video=9861.9ms (frame 590), diff=3.1ms
- 🟢 Pop #11 [L]: audio=10665.0ms, video=10664.3ms (frame 638), diff=0.7ms

- Channels: LRLRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 12/2/2/2 | 12/1/1/1 | 0 | 0 |
| After settling | 16/2/2/2 | 16/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.257–15.244).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 21 | 9.158 | 0.704 | 2.273 | 7.756–10.029 |
| 2 | 6 | 5.296 | 0.292 | 0.718 | 4.898–5.616 |
| 3 | 6 | 6.764 | 0.106 | 0.267 | 6.636–6.903 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 15.8 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 158 at 00:02.6 of the 15.8 s video above.
