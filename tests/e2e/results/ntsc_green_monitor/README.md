# C64 Stream E2E Test Report

Generated: 2025-12-29 23:58:42 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
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
- Disk (/): 916G total, 495G available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 27 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 59.6% | 63.25% | 63.23% | 67.3% |
| RAM | 6225.52 MB | 6247.98 MB | 6247.86 MB | 6276.22 MB |
| GPU | 100.0% | 100.0% | 100.0% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 7.7ms, max 22.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7854.0ms, video=7856.1ms (frame 470), diff=2.1ms
- 🟢 Pop #2 [R]: audio=8654.0ms, video=8658.4ms (frame 518), diff=4.4ms
- 🟢 Pop #3 [L]: audio=9454.0ms, video=9460.8ms (frame 566), diff=6.8ms
- 🟢 Pop #4 [R]: audio=10260.0ms, video=10263.1ms (frame 614), diff=3.1ms
- 🟢 Pop #5 [L]: audio=11060.0ms, video=11082.1ms (frame 663), diff=22.1ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (299 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 20/2/2/2 | 19/1/1/1 | 0 | 0 |
| After settling | 3/2/2/2 | 3/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.488–12.469).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 24 | 10.356 | 0.364 | 1.304 | 9.594–10.898 |
| 2 | 4 | 12.373 | 0.064 | 0.151 | 12.302–12.453 |
| 3 | 2 | 8.851 | 0.092 | 0.184 | 8.759–8.943 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 16.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 471 at 00:07.8 of the 16.0 s video above.
