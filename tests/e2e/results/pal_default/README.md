# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2026-01-01 16:51:00 UTC

## Test configuration

- Format: PAL
- Frames: 400
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

During the test's processing window (7.6s, 16 of 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 46.7% | 60.85% | 58.82% | 78.5% |
| RAM | 5967.69 MB | 6018.96 MB | 6013.8 MB | 6054.04 MB |
| GPU | 18.99% | 31.04% | 32.52% | 43.94% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 27199 | 0.002 ms | 14.205 ms | 0 |
| Audio | 1992 | 0.386 ms | 8.078 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 3.8ms, max 9.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=5028.0ms, video=5027.4ms (frame 252), diff=0.6ms
- 🟢 Pop #2 [L]: audio=5057.0ms, video=5047.4ms (frame 253), diff=9.6ms
- 🟢 Pop #3 [R]: audio=6006.0ms, video=6005.0ms (frame 301), diff=1.0ms
- 🟢 Pop #4 [L]: audio=6966.0ms, video=6962.6ms (frame 349), diff=3.4ms
- 🟢 Pop #5 [R]: audio=7924.0ms, video=7920.2ms (frame 397), diff=3.8ms
- 🟢 Pop #6 [L]: audio=8881.0ms, video=8877.8ms (frame 445), diff=3.2ms
- 🟢 Pop #7 [R]: audio=9841.0ms, video=9835.4ms (frame 493), diff=5.6ms
- 🟢 Pop #8 [L]: audio=10796.0ms, video=10793.0ms (frame 541), diff=3.0ms

- Channels: LLRLRLRL
- 🔁 Channel alternation: MISMATCH

### Frame Progression

- 🟢 Frame sequence verified (401 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 3/2/2/2 | 3/1/1/1 | 0 | 0 |
| After settling | 4/2/2/2 | 3/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 4.589–15.521).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 7 | 9.559 | 0.082 | 0.240 | 9.436–9.676 |
| 2 | 4 | 5.062 | 0.060 | 0.159 | 4.988–5.147 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 16.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 253 at 00:05.1 of the 16.0 s video above.
