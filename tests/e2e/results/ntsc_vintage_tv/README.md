# C64 Stream E2E Test Report

Generated: 2025-12-29 17:02:02 UTC

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
| CPU | 62.8% | 71.25% | 70.49% | 82.3% |
| RAM | 6034.55 MB | 6063.47 MB | 6061.93 MB | 6096.14 MB |
| GPU | 80.65% | 80.65% | 83.74% | 90.32% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 7.5ms, max 18.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8001.0ms, video=8006.6ms (frame 479), diff=5.6ms
- 🟢 Pop #2 [R]: audio=8801.0ms, video=8808.9ms (frame 527), diff=7.9ms
- 🟢 Pop #3 [L]: audio=9601.0ms, video=9594.5ms (frame 574), diff=6.5ms
- 🟢 Pop #4 [R]: audio=10406.0ms, video=10413.5ms (frame 623), diff=7.5ms
- 🟢 Pop #5 [L]: audio=11181.0ms, video=11199.1ms (frame 670), diff=18.1ms
- 🟢 Pop #6 [R]: audio=12006.0ms, video=12001.5ms (frame 718), diff=4.5ms
- 🟢 Pop #7 [L]: audio=12812.0ms, video=12803.8ms (frame 766), diff=8.2ms
- 🟢 Pop #8 [R]: audio=13612.0ms, video=13606.1ms (frame 814), diff=5.9ms
- 🟢 Pop #9 [L]: audio=14412.0ms, video=14408.5ms (frame 862), diff=3.5ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/3/7/370 | 0/0/0/0 | 5 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 479 at 00:08.0 of the 19.1 s video above.
