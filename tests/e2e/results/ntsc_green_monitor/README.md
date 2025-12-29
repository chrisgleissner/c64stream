# C64 Stream E2E Test Report

Generated: 2025-12-29 16:58:42 UTC

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
- Disk (/): 916G total, 496G available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 48.6% | 63.35% | 61.48% | 67.9% |
| RAM | 6125.39 MB | 6164.74 MB | 6164.69 MB | 6199.3 MB |
| GPU | 87.1% | 94.1% | 93.15% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 16.3ms, max 32.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7921.0ms, video=7923.0ms (frame 474), diff=2.0ms
- 🟢 Pop #2 [R]: audio=8721.0ms, video=8725.3ms (frame 522), diff=4.3ms
- 🟢 Pop #3 [L]: audio=9495.0ms, video=9527.6ms (frame 570), diff=32.6ms
- 🟢 Pop #4 [R]: audio=10326.0ms, video=10330.0ms (frame 618), diff=4.0ms
- 🟢 Pop #5 [L]: audio=11126.0ms, video=11132.3ms (frame 666), diff=6.3ms
- 🟢 Pop #6 [R]: audio=11926.0ms, video=11951.3ms (frame 715), diff=25.3ms
- 🟢 Pop #7 [L]: audio=12732.0ms, video=12753.7ms (frame 763), diff=21.7ms
- 🟢 Pop #8 [R]: audio=13532.0ms, video=13556.0ms (frame 811), diff=24.0ms
- 🟢 Pop #9 [L]: audio=14332.0ms, video=14358.3ms (frame 859), diff=26.3ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/2/2/2 | 7/1/1/1 | 0 | 0 |
| After settling | 9/2/2/2 | 8/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.555–15.913).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 26 | 11.755 | 0.617 | 2.073 | 10.631–12.704 |
| 2 | 4 | 9.770 | 0.049 | 0.134 | 9.695–9.829 |

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
- Taken from frame 475 at 00:07.9 of the 19.1 s video above.
