# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-01 09:17:20 UTC

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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 27 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 57.1% | 59.95% | 61.47% | 74.1% |
| RAM | 5418.48 MB | 5444.06 MB | 5443.53 MB | 5457.48 MB |
| GPU | 26.02% | 27.88% | 29.16% | 39.19% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality

| Stream | Packets | Jitter (median) | Jitter (max) |
|--------|---------|-----------------|--------------|
| Video | 17999 | 0.001 ms | 5.199 ms |
| Audio | 1249 | 0.592 ms | 24.551 ms |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 18.7ms, max 22.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2849.0ms, video=2841.6ms (frame 170), diff=7.4ms
- 🟢 Pop #2 [R]: audio=3649.0ms, video=3627.2ms (frame 217), diff=21.8ms
- 🟢 Pop #3 [L]: audio=4452.0ms, video=4429.5ms (frame 265), diff=22.5ms
- 🟢 Pop #4 [R]: audio=5254.0ms, video=5231.8ms (frame 313), diff=22.2ms
- 🟢 Pop #5 [L]: audio=6054.0ms, video=6034.2ms (frame 361), diff=19.8ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 13/2/2/2 | 13/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 1/5/5/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.440–10.414).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 10.414 | 0.000 | 0.000 | 10.414–10.414 |

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
- Taken from frame 170 at 00:02.8 of the 10.9 s video above.
