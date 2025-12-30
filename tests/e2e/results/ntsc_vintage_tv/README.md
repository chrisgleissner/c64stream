# C64 Stream E2E Test Report

Generated: 2025-12-29 23:43:01 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 30.0.2.1-3build1
- CPU: Intel(R) Core(TM) i5-14600K (20 cores)
- RAM: 62Gi total, 53Gi available
- Disk (/): 916G total, 495G available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 27 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 67.3% | 71.3% | 70.86% | 74.5% |
| RAM | 6298.97 MB | 6371.33 MB | 6398.85 MB | 6519.96 MB |
| GPU | 100.0% | 100.0% | 100.0% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 4.9ms, max 8.2ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7886.0ms, video=7889.5ms (frame 472), diff=3.5ms
- 🟢 Pop #2 [R]: audio=8686.0ms, video=8691.9ms (frame 520), diff=5.9ms
- 🟢 Pop #3 [L]: audio=9486.0ms, video=9494.2ms (frame 568), diff=8.2ms
- 🟢 Pop #4 [R]: audio=10292.0ms, video=10296.5ms (frame 616), diff=4.5ms
- 🟢 Pop #5 [L]: audio=11113.0ms, video=11115.6ms (frame 665), diff=2.6ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/3/7/364 | 0/0/0/0 | 5 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 16.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 473 at 00:07.9 of the 16.0 s video above.
