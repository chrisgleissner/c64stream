# C64 Stream E2E Test Report

Generated: 2025-12-29 16:27:58 UTC

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
| CPU | 37.8% | 42.25% | 42.31% | 45.7% |
| RAM | 5860.65 MB | 5945.18 MB | 5940.41 MB | 5968.0 MB |
| GPU | 83.87% | 86.58% | 88.38% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 3.2ms, max 6.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7945.0ms, video=7939.7ms (frame 475), diff=5.3ms
- 🟢 Pop #2 [R]: audio=8748.0ms, video=8742.0ms (frame 523), diff=6.0ms
- 🟢 Pop #3 [L]: audio=9548.0ms, video=9544.3ms (frame 571), diff=3.7ms
- 🟢 Pop #4 [R]: audio=10350.0ms, video=10346.7ms (frame 619), diff=3.3ms
- 🟢 Pop #5 [L]: audio=11153.0ms, video=11149.0ms (frame 667), diff=4.0ms
- 🟢 Pop #6 [R]: audio=11953.0ms, video=11951.3ms (frame 715), diff=1.7ms
- 🟢 Pop #7 [L]: audio=12756.0ms, video=12753.7ms (frame 763), diff=2.3ms
- 🟢 Pop #8 [R]: audio=13558.0ms, video=13556.0ms (frame 811), diff=2.0ms
- 🟢 Pop #9 [L]: audio=14358.0ms, video=14358.3ms (frame 859), diff=0.3ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 1/2/2/2 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.572–18.571).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 15.562 | 0.000 | 0.000 | 15.562–15.562 |

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
- Taken from frame 476 at 00:07.9 of the 19.1 s video above.
