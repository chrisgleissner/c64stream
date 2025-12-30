# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2025-12-30 13:32:25 UTC

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
| CPU | 61.3% | 64.2% | 64.0% | 67.8% |
| RAM | 6505.61 MB | 6539.94 MB | 6537.52 MB | 6548.14 MB |
| GPU | 32.11% | 42.2% | 44.33% | 75.39% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 16.6ms, max 18.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2710.0ms, video=2691.1ms (frame 161), diff=18.9ms
- 🟢 Pop #2 [R]: audio=3510.0ms, video=3493.5ms (frame 209), diff=16.5ms
- 🟢 Pop #3 [L]: audio=4310.0ms, video=4295.8ms (frame 257), diff=14.2ms
- 🟢 Pop #4 [R]: audio=5116.0ms, video=5098.1ms (frame 305), diff=17.9ms
- 🟢 Pop #5 [L]: audio=5916.0ms, video=5900.4ms (frame 353), diff=15.6ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 181 frames (3.0s) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 1/181/181/181 | 1/5/5/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.307–10.297).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 7.288 | 0.000 | 0.000 | 7.288–7.288 |
| 2 | 1 | 10.297 | 0.000 | 0.000 | 10.297–10.297 |

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
- Taken from frame 161 at 00:02.7 of the 10.8 s video above.
