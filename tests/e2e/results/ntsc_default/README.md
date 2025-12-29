# C64 Stream E2E Test Report

Generated: 2025-12-29 19:18:10 UTC

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

- OS: Ubuntu 24.04.3 LTS (kernel 6.1.147)
- OBS: 30.0.2.1-3build1
- CPU: Intel(R) Xeon(R) Processor (4 cores)
- RAM: 15Gi total, 15Gi available
- Disk (/): 126G total, 109G available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (4 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 73.2% | 76.0% | 75.97% | 80.0% |
| RAM | 818.93 MB | 830.25 MB | 829.39 MB | 832.25 MB |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 9.3ms, max 21.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7222.0ms, video=7220.9ms (frame 432), diff=1.1ms
- 🟢 Pop #2 [R]: audio=8022.0ms, video=8023.3ms (frame 480), diff=1.3ms
- 🟢 Pop #3 [L]: audio=8822.0ms, video=8842.3ms (frame 529), diff=20.3ms
- 🟢 Pop #4 [R]: audio=9628.0ms, video=9627.9ms (frame 576), diff=0.1ms
- 🟢 Pop #5 [L]: audio=10428.0ms, video=10447.0ms (frame 625), diff=19.0ms
- 🟢 Pop #6 [R]: audio=11228.0ms, video=11249.3ms (frame 673), diff=21.3ms
- 🟢 Pop #7 [L]: audio=12033.0ms, video=12051.6ms (frame 721), diff=18.6ms
- 🟢 Pop #8 [R]: audio=12854.0ms, video=12853.9ms (frame 769), diff=0.1ms
- 🟢 Pop #9 [L]: audio=13654.0ms, video=13656.3ms (frame 817), diff=2.3ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 17/2/2/2 | 16/1/1/1 | 0 | 0 |
| After settling | 4/2/2/2 | 4/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 6.853–17.868).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 25 | 8.969 | 0.553 | 1.722 | 8.190–9.912 |
| 2 | 6 | 11.700 | 0.257 | 0.618 | 11.333–11.951 |
| 3 | 2 | 14.066 | 0.008 | 0.017 | 14.057–14.074 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 18.4 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 433 at 00:07.2 of the 18.4 s video above.
