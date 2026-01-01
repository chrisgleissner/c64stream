# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2026-01-01 16:50:01 UTC

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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.7s, 16 of 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 94.8% | 95.55% | 95.64% | 97.0% |
| RAM | 6285.43 MB | 6363.66 MB | 6361.12 MB | 6418.89 MB |
| GPU | 28.38% | 44.85% | 41.73% | 49.15% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28798 | 0.001 ms | 23.755 ms | 0 |
| Audio | 2002 | 1.023 ms | 23.816 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 92.5ms, max 772.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=5030.0ms, video=5031.3ms (frame 301), diff=1.3ms
- • Pop #2 [R]: audio=5830.0ms, video=6602.5ms (frame 395), diff=772.5ms
- 🟢 Pop #3 [L]: audio=6630.0ms, video=6619.2ms (frame 396), diff=10.8ms
- 🟢 Pop #4 [R]: audio=7436.0ms, video=7421.5ms (frame 444), diff=14.5ms
- 🟢 Pop #5 [L]: audio=8236.0ms, video=8240.6ms (frame 493), diff=4.6ms
- 🟢 Pop #6 [R]: audio=9036.0ms, video=9042.9ms (frame 541), diff=6.9ms
- 🟢 Pop #7 [L]: audio=9841.0ms, video=9845.2ms (frame 589), diff=4.2ms
- 🟢 Pop #8 [R]: audio=10641.0ms, video=10630.8ms (frame 636), diff=10.2ms
- 🟢 Pop #9 [L]: audio=11441.0ms, video=11433.2ms (frame 684), diff=7.8ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=118, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/264/264/264 | 0/0/0/0 | 0 | 0 |
| After settling | 97/2/2/4 | 83/1/1/4 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.217–12.603).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 136 | 6.410 | 1.053 | 3.543 | 4.664–8.207 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 16.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 301 at 00:05.0 of the 16.1 s video above.
