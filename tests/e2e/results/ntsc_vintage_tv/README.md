# C64 Stream E2E Test Report

Generated: 2025-12-29 16:34:26 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
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
- Disk (/): 916G total, 497G available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 65.2% | 71.35% | 70.5% | 73.8% |
| RAM | 6165.75 MB | 6198.7 MB | 6201.03 MB | 6253.66 MB |
| GPU | 80.65% | 86.55% | 85.62% | 90.32% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 7.1ms, max 10.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7852.0ms, video=7856.1ms (frame 470), diff=4.1ms
- 🟢 Pop #2 [R]: audio=8654.0ms, video=8658.4ms (frame 518), diff=4.4ms
- 🟢 Pop #3 [L]: audio=9454.0ms, video=9460.8ms (frame 566), diff=6.8ms
- 🟢 Pop #4 [R]: audio=10257.0ms, video=10263.1ms (frame 614), diff=6.1ms
- 🟢 Pop #5 [L]: audio=11059.0ms, video=11065.4ms (frame 662), diff=6.4ms
- 🟢 Pop #6 [R]: audio=11860.0ms, video=11867.7ms (frame 710), diff=7.7ms
- 🟢 Pop #7 [L]: audio=12662.0ms, video=12670.1ms (frame 758), diff=8.1ms
- 🟢 Pop #8 [R]: audio=13462.0ms, video=13472.4ms (frame 806), diff=10.4ms
- 🟢 Pop #9 [L]: audio=14265.0ms, video=14274.7ms (frame 854), diff=9.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/3/7/362 | 0/0/0/0 | 5 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 471 at 00:07.8 of the 19.0 s video above.
