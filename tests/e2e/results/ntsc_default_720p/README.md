# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2025-12-31 11:22:11 UTC

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
- RAM: 31Gi total, 21Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 27 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 41.0% | 48.15% | 47.1% | 50.5% |
| RAM | 5701.75 MB | 5718.69 MB | 5718.15 MB | 5729.67 MB |
| GPU | 27.73% | 44.1% | 43.07% | 51.1% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 8.6ms, max 10.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2702.0ms, video=2691.1ms (frame 161), diff=10.9ms
- 🟢 Pop #2 [R]: audio=3502.0ms, video=3493.5ms (frame 209), diff=8.5ms
- 🟢 Pop #3 [L]: audio=4302.0ms, video=4295.8ms (frame 257), diff=6.2ms
- 🟢 Pop #4 [R]: audio=5108.0ms, video=5098.1ms (frame 305), diff=9.9ms
- 🟢 Pop #5 [L]: audio=5908.0ms, video=5900.4ms (frame 353), diff=7.6ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 180 frames (3.0s) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 1/180/180/180 | 1/5/5/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.307–10.280).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 4.447 | 0.017 | 0.033 | 4.430–4.463 |
| 2 | 1 | 7.288 | 0.000 | 0.000 | 7.288–7.288 |
| 3 | 1 | 10.280 | 0.000 | 0.000 | 10.280–10.280 |

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
