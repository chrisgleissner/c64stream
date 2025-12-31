# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2025-12-31 00:40:22 UTC

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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.7s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 95.0% | 95.3% | 95.51% | 96.9% |
| RAM | 4315.28 MB | 4518.93 MB | 4490.32 MB | 4575.86 MB |
| GPU | 26.04% | 26.94% | 30.25% | 41.05% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 271.9ms, max 778.2ms

#### Sync Details

- • Pop #1 [L]: audio=2732.0ms, video=3510.2ms (frame 210), diff=778.2ms
- 🟢 Pop #2 [R]: audio=3553.0ms, video=3526.9ms (frame 211), diff=26.1ms
- 🟢 Pop #3 [L]: audio=4356.0ms, video=4329.2ms (frame 259), diff=26.8ms
- • Pop #4 [R]: audio=5158.0ms, video=5917.2ms (frame 354), diff=759.2ms
- 🟢 Pop #5 [L]: audio=5958.0ms, video=5933.9ms (frame 355), diff=24.1ms
- 🟢 Pop #6 [R]: audio=6761.0ms, video=6752.9ms (frame 404), diff=8.1ms
- • Pop #7 [L]: audio=7564.0ms, video=8340.9ms (frame 499), diff=776.9ms
- 🟢 Pop #8 [R]: audio=8364.0ms, video=8357.6ms (frame 500), diff=6.4ms
- 🟡 Pop #9 [L]: audio=9185.0ms, video=9143.2ms (frame 547), diff=41.8ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=133, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 52/2/2/128 | 51/1/1/2 | 0 | 0 |
| After settling | 108/2/2/7 | 101/1/1/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.217–10.330).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 152 | 6.109 | 1.233 | 4.195 | 4.012–8.207 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 13.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 211 at 00:03.5 of the 13.9 s video above.
