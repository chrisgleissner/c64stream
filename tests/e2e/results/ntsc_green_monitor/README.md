# C64 Stream E2E Test Report

Generated: 2025-12-28 20:09:57 UTC

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
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 91.5% | 92.2% | 93.31% | 96.5% |
| RAM | 6891.52 MB | 6953.46 MB | 7035.51 MB | 7374.9 MB |
| GPU | 26.98% | 29.34% | 30.74% | 41.27% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 47.4ms, max 57.3ms

#### Sync Details

- 🟡 Pop #1 [L]: audio=8212.0ms, video=8173.7ms (frame 489), diff=38.3ms
- 🟡 Pop #2 [R]: audio=9033.0ms, video=8992.7ms (frame 538), diff=40.3ms
- 🟢 Pop #3 [L]: audio=9833.0ms, video=9811.8ms (frame 587), diff=21.2ms
- 🟡 Pop #4 [R]: audio=10638.0ms, video=10580.7ms (frame 633), diff=57.3ms
- 🟡 Pop #5 [L]: audio=11438.0ms, video=11383.0ms (frame 681), diff=55.0ms
- 🟡 Pop #6 [R]: audio=12238.0ms, video=12185.3ms (frame 729), diff=52.7ms
- 🟡 Pop #7 [L]: audio=13044.0ms, video=12987.7ms (frame 777), diff=56.3ms
- 🟡 Pop #8 [R]: audio=13844.0ms, video=13790.0ms (frame 825), diff=54.0ms
- 🟡 Pop #9 [L]: audio=14644.0ms, video=14592.3ms (frame 873), diff=51.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 50/2/2/4 | 51/1/1/3 | 0 | 2 |
| After settling | 41/2/2/3 | 41/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.789–16.147).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 161 | 11.527 | 2.422 | 7.956 | 7.823–15.779 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.4 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 489 at 00:08.2 of the 20.4 s video above.
