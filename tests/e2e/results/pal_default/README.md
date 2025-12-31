# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2025-12-31 00:41:19 UTC

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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 49.4% | 51.05% | 53.93% | 69.7% |
| RAM | 4462.9 MB | 4571.65 MB | 4567.93 MB | 4619.69 MB |
| GPU | 24.2% | 29.03% | 29.65% | 38.19% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 21.9ms, max 38.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2769.0ms, video=2753.1ms (frame 138), diff=15.9ms
- 🟢 Pop #2 [R]: audio=3724.0ms, video=3710.7ms (frame 186), diff=13.3ms
- 🟢 Pop #3 [L]: audio=4686.0ms, video=4668.3ms (frame 234), diff=17.7ms
- 🟢 Pop #4 [R]: audio=5641.0ms, video=5625.9ms (frame 282), diff=15.1ms
- 🟢 Pop #5 [L]: audio=6598.0ms, video=6583.5ms (frame 330), diff=14.5ms
- 🟡 Pop #6 [R]: audio=7580.0ms, video=7541.1ms (frame 378), diff=38.9ms
- 🟡 Pop #7 [L]: audio=8537.0ms, video=8498.8ms (frame 426), diff=38.2ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (401 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 7/2/2/4 | 5/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.294–13.247).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 4 | 9.890 | 0.199 | 0.478 | 9.756–10.234 |
| 2 | 2 | 6.683 | 0.020 | 0.040 | 6.663–6.703 |
| 3 | 2 | 7.262 | 0.020 | 0.040 | 7.242–7.282 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 13.7 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 138 at 00:02.8 of the 13.7 s video above.
