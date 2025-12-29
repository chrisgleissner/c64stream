# C64 Stream E2E Test Report

Generated: 2025-12-29 17:19:48 UTC

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
| CPU | 37.3% | 41.85% | 41.93% | 45.9% |
| RAM | 5979.54 MB | 6038.4 MB | 6050.24 MB | 6151.39 MB |
| GPU | 78.52% | 84.97% | 83.41% | 88.19% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 5.6ms, max 15.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7941.0ms, video=7956.4ms (frame 476), diff=15.4ms
- 🟢 Pop #2 [R]: audio=8766.0ms, video=8758.7ms (frame 524), diff=7.3ms
- 🟢 Pop #3 [L]: audio=9566.0ms, video=9561.1ms (frame 572), diff=4.9ms
- 🟢 Pop #4 [R]: audio=10369.0ms, video=10363.4ms (frame 620), diff=5.6ms
- 🟢 Pop #5 [L]: audio=11172.0ms, video=11165.7ms (frame 668), diff=6.3ms
- 🟢 Pop #6 [R]: audio=11972.0ms, video=11968.0ms (frame 716), diff=4.0ms
- 🟢 Pop #7 [L]: audio=12774.0ms, video=12770.4ms (frame 764), diff=3.6ms
- 🟢 Pop #8 [R]: audio=13574.0ms, video=13572.7ms (frame 812), diff=1.3ms
- 🟢 Pop #9 [L]: audio=14377.0ms, video=14375.0ms (frame 860), diff=2.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 3/2/2/2 | 2/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.589–18.587).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 3 | 15.512 | 0.063 | 0.151 | 15.428–15.579 |

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
- Taken from frame 477 at 00:08.0 of the 19.1 s video above.
