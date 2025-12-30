# C64 Stream E2E Test Report

Generated: 2025-12-29 23:43:45 UTC

## Test configuration

- Format: PAL
- Frames: 250
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
- RAM: 62Gi total, 54Gi available
- Disk (/): 916G total, 495G available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 27 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 21.0% | 30.45% | 29.9% | 40.4% |
| RAM | 6295.59 MB | 6396.03 MB | 6395.81 MB | 6523.16 MB |
| GPU | 100.0% | 100.0% | 100.0% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 11.4ms, max 12.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7988.0ms, video=8000.0ms (frame 401), diff=12.0ms
- 🟢 Pop #2 [R]: audio=8945.0ms, video=8957.6ms (frame 449), diff=12.6ms
- 🟢 Pop #3 [L]: audio=9905.0ms, video=9915.2ms (frame 497), diff=10.2ms
- 🟢 Pop #4 [R]: audio=10862.0ms, video=10872.8ms (frame 545), diff=10.8ms

- Channels: LRLR
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 151 frames (3.0s) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 1/151/151/151 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.561–15.501).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 12.509 | 0.000 | 0.000 | 12.509–12.509 |

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
- Taken from frame 402 at 00:08.0 of the 16.0 s video above.
