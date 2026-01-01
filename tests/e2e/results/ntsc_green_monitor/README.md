# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2026-01-01 09:25:40 UTC

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
| CPU | 90.9% | 91.5% | 91.56% | 92.2% |
| RAM | 5385.52 MB | 5420.91 MB | 5418.65 MB | 5428.08 MB |
| GPU | 27.37% | 37.16% | 36.95% | 44.34% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality

| Stream | Packets | Jitter (median) | Jitter (max) |
|--------|---------|-----------------|--------------|
| Video | 17999 | 0.001 ms | 11.918 ms |
| Audio | 1249 | 0.972 ms | 9.643 ms |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 5.0ms, max 18.2ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2812.0ms, video=2808.1ms (frame 168), diff=3.9ms
- 🟢 Pop #2 [R]: audio=3612.0ms, video=3593.8ms (frame 215), diff=18.2ms
- 🟢 Pop #3 [L]: audio=4412.0ms, video=4412.8ms (frame 264), diff=0.8ms
- 🟢 Pop #4 [R]: audio=5217.0ms, video=5215.1ms (frame 312), diff=1.9ms
- 🟢 Pop #5 [L]: audio=6017.0ms, video=6017.5ms (frame 360), diff=0.5ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (431 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 11/2/2/126 | 14/1/1/1 | 1 | 0 |
| After settling | 0/0/0/0 | 28/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.201–7.388).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 29 | 5.709 | 0.977 | 3.192 | 4.129–7.321 |

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
- Taken from frame 168 at 00:02.8 of the 10.9 s video above.
