# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2025-12-30 13:51:08 UTC

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
| CPU | 59.8% | 62.9% | 63.9% | 71.2% |
| RAM | 6752.87 MB | 6767.13 MB | 6780.06 MB | 6856.23 MB |
| GPU | 30.02% | 36.35% | 38.29% | 45.41% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 19.1ms, max 21.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2729.0ms, video=2707.9ms (frame 162), diff=21.1ms
- 🟢 Pop #2 [R]: audio=3529.0ms, video=3510.2ms (frame 210), diff=18.8ms
- 🟢 Pop #3 [L]: audio=4332.0ms, video=4312.5ms (frame 258), diff=19.5ms
- 🟢 Pop #4 [R]: audio=5134.0ms, video=5114.8ms (frame 306), diff=19.2ms
- 🟢 Pop #5 [L]: audio=5934.0ms, video=5917.2ms (frame 354), diff=16.8ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 181 frames (3.0s) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 3/2/2/2 | 3/1/1/1 | 0 | 0 |
| After settling | 2/5/93/181 | 2/4/4/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.323–10.313).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 3 | 7.115 | 0.137 | 0.318 | 6.987–7.305 |
| 2 | 3 | 5.962 | 0.111 | 0.251 | 5.867–6.118 |
| 3 | 1 | 10.313 | 0.000 | 0.000 | 10.313–10.313 |

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
- Taken from frame 162 at 00:02.7 of the 10.8 s video above.
