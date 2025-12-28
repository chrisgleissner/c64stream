# C64 Stream E2E Test Report

Generated: 2025-12-28 20:15:07 UTC

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
| CPU | 86.1% | 95.4% | 94.88% | 96.0% |
| RAM | 6698.39 MB | 6711.58 MB | 6710.56 MB | 6720.91 MB |
| GPU | 26.02% | 26.67% | 26.84% | 27.94% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 25.5ms, max 33.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8105.0ms, video=8073.4ms (frame 483), diff=31.6ms
- 🟢 Pop #2 [R]: audio=8926.0ms, video=8892.5ms (frame 532), diff=33.5ms
- 🟢 Pop #3 [L]: audio=9726.0ms, video=9711.5ms (frame 581), diff=14.5ms
- 🟢 Pop #4 [R]: audio=10529.0ms, video=10497.1ms (frame 628), diff=31.9ms
- 🟢 Pop #5 [L]: audio=11332.0ms, video=11316.2ms (frame 677), diff=15.8ms
- 🟢 Pop #6 [R]: audio=12132.0ms, video=12101.8ms (frame 724), diff=30.2ms
- 🟢 Pop #7 [L]: audio=12934.0ms, video=12904.1ms (frame 772), diff=29.9ms
- 🟢 Pop #8 [R]: audio=13737.0ms, video=13723.1ms (frame 821), diff=13.9ms
- 🟢 Pop #9 [L]: audio=14537.0ms, video=14508.7ms (frame 868), diff=28.3ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 5/2/13/421 | 0/0/0/0 | 2 | 0 |
| After settling | 12/2/2/3 | 10/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.134–16.047).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 18 | 7.919 | 0.130 | 0.418 | 7.706–8.124 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.3 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 483 at 00:08.0 of the 20.3 s video above.
