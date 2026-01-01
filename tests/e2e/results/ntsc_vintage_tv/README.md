# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2026-01-01 12:35:44 UTC

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
| CPU | 93.6% | 94.9% | 94.93% | 95.8% |
| RAM | 7186.68 MB | 7216.65 MB | 7219.01 MB | 7236.84 MB |
| GPU | 24.56% | 43.27% | 38.5% | 43.47% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17999 | 0.001 ms | 29.762 ms | 0 |
| Audio | 1250 | 1.042 ms | 24.096 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 7.8ms, max 23.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2814.0ms, video=2808.1ms (frame 168), diff=5.9ms
- 🟢 Pop #2 [R]: audio=3614.0ms, video=3610.5ms (frame 216), diff=3.5ms
- 🟢 Pop #3 [L]: audio=4414.0ms, video=4412.8ms (frame 264), diff=1.2ms
- 🟢 Pop #4 [R]: audio=5220.0ms, video=5215.1ms (frame 312), diff=4.9ms
- 🟢 Pop #5 [L]: audio=6041.0ms, video=6017.5ms (frame 360), diff=23.5ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=98, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 30/2/2/132 | 51/1/1/2 | 0 | 0 |
| After settling | 0/0/0/0 | 91/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.217–7.388).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 97 | 5.681 | 0.976 | 3.343 | 4.012–7.355 |

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
- Taken from frame 169 at 00:02.8 of the 10.9 s video above.
