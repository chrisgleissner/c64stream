# C64 Stream E2E Test Report

Generated: 2025-12-29 17:26:57 UTC

## Test configuration

- Format: PAL
- Frames: 400
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
| CPU | 32.2% | 33.6% | 33.62% | 34.6% |
| RAM | 6064.98 MB | 6118.6 MB | 6114.95 MB | 6163.34 MB |
| GPU | 81.74% | 90.32% | 88.51% | 93.55% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 6.5ms, max 28.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8057.0ms, video=8059.9ms (frame 404), diff=2.9ms
- 🟢 Pop #2 [R]: audio=9012.0ms, video=9017.5ms (frame 452), diff=5.5ms
- 🟢 Pop #3 [L]: audio=9974.0ms, video=9975.1ms (frame 500), diff=1.1ms
- 🟢 Pop #4 [R]: audio=10904.0ms, video=10932.7ms (frame 548), diff=28.7ms
- 🟢 Pop #5 [L]: audio=11886.0ms, video=11890.3ms (frame 596), diff=4.3ms
- 🟢 Pop #6 [R]: audio=12846.0ms, video=12847.9ms (frame 644), diff=1.9ms
- 🟢 Pop #7 [L]: audio=13804.0ms, video=13805.5ms (frame 692), diff=1.5ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (401 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 1/4/4/4 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.621–18.554).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 15.561 | 0.000 | 0.000 | 15.561–15.561 |

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
- Taken from frame 405 at 00:08.1 of the 19.1 s video above.
