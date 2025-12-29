# C64 Stream E2E Test Report

Generated: 2025-12-29 19:18:54 UTC

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
| CPU | 27.6% | 28.6% | 28.78% | 29.9% |
| RAM | 698.2 MB | 708.85 MB | 708.5 MB | 710.96 MB |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 890.5ms, max 3203.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7201.0ms, video=7199.7ms (frame 216), diff=1.3ms
- 🟢 Pop #2 [R]: audio=8001.0ms, video=7999.7ms (frame 240), diff=1.3ms
- 🟢 Pop #3 [L]: audio=8804.0ms, video=8799.7ms (frame 264), diff=4.3ms
- 🟢 Pop #4 [R]: audio=9602.0ms, video=9599.7ms (frame 288), diff=2.3ms
- 🟢 Pop #5 [L]: audio=10428.0ms, video=10433.0ms (frame 313), diff=5.0ms
- • Pop #6 [R]: audio=11230.0ms, video=10433.0ms (frame 313), diff=797.0ms
- • Pop #7 [L]: audio=12033.0ms, video=10433.0ms (frame 313), diff=1600.0ms
- • Pop #8 [R]: audio=12833.0ms, video=10433.0ms (frame 313), diff=2400.0ms
- • Pop #9 [L]: audio=13636.0ms, video=10433.0ms (frame 313), diff=3203.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=118, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 118/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 117/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 6.833–17.867).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 235 | 10.816 | 2.279 | 7.933 | 6.867–14.800 |

### Video

- Download: [c64_recording.mkv](c64_recording.mkv)
- Duration: 18.4 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 217 at 00:03.6 of the 18.4 s video above.
