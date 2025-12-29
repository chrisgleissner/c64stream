# C64 Stream E2E Test Report

Generated: 2025-12-29 17:26:14 UTC

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
- Disk (/): 916G total, 496G available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 64.2% | 71.05% | 70.49% | 74.0% |
| RAM | 6005.47 MB | 6088.46 MB | 6090.0 MB | 6169.77 MB |
| GPU | 80.65% | 96.77% | 93.35% | 96.77% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 6.9ms, max 9.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7897.0ms, video=7906.3ms (frame 473), diff=9.3ms
- 🟢 Pop #2 [R]: audio=8718.0ms, video=8725.3ms (frame 522), diff=7.3ms
- 🟢 Pop #3 [L]: audio=9518.0ms, video=9510.9ms (frame 569), diff=7.1ms
- 🟢 Pop #4 [R]: audio=10321.0ms, video=10313.2ms (frame 617), diff=7.8ms
- 🟢 Pop #5 [L]: audio=11124.0ms, video=11132.3ms (frame 666), diff=8.3ms
- 🟢 Pop #6 [R]: audio=11924.0ms, video=11917.9ms (frame 713), diff=6.1ms
- 🟢 Pop #7 [L]: audio=12726.0ms, video=12720.2ms (frame 761), diff=5.8ms
- 🟢 Pop #8 [R]: audio=13529.0ms, video=13522.5ms (frame 809), diff=6.5ms
- 🟢 Pop #9 [L]: audio=14329.0ms, video=14324.9ms (frame 857), diff=4.1ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/3/7/365 | 0/0/0/0 | 5 | 0 |
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
- Taken from frame 474 at 00:07.9 of the 19.0 s video above.
