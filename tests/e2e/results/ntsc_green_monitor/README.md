# C64 Stream E2E Test Report

Generated: 2025-12-29 16:31:06 UTC

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
- RAM: 62Gi total, 52Gi available
- Disk (/): 916G total, 497G available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 51.6% | 61.25% | 61.13% | 68.6% |
| RAM | 5873.52 MB | 5958.77 MB | 5950.91 MB | 5982.25 MB |
| GPU | 86.0% | 93.0% | 91.52% | 95.68% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 6.8ms, max 8.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7950.0ms, video=7956.4ms (frame 476), diff=6.4ms
- 🟢 Pop #2 [R]: audio=8751.0ms, video=8758.7ms (frame 524), diff=7.7ms
- 🟢 Pop #3 [L]: audio=9553.0ms, video=9561.1ms (frame 572), diff=8.1ms
- 🟢 Pop #4 [R]: audio=10356.0ms, video=10363.4ms (frame 620), diff=7.4ms
- 🟢 Pop #5 [L]: audio=11156.0ms, video=11149.0ms (frame 667), diff=7.0ms
- 🟢 Pop #6 [R]: audio=11958.0ms, video=11951.3ms (frame 715), diff=6.7ms
- 🟢 Pop #7 [L]: audio=12761.0ms, video=12753.7ms (frame 763), diff=7.3ms
- 🟢 Pop #8 [R]: audio=13561.0ms, video=13556.0ms (frame 811), diff=5.0ms
- 🟢 Pop #9 [L]: audio=14364.0ms, video=14358.3ms (frame 859), diff=5.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 15/2/2/2 | 14/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.572–15.930).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 18 | 15.044 | 0.304 | 1.053 | 14.509–15.562 |
| 2 | 5 | 13.466 | 0.302 | 0.869 | 13.055–13.924 |
| 3 | 1 | 11.617 | 0.000 | 0.000 | 11.617–11.617 |

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
