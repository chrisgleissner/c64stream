# C64 Stream E2E Test Report

Generated: 2025-12-29 10:10:19 UTC

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
| CPU | 48.8% | 54.05% | 53.99% | 57.5% |
| RAM | 6842.73 MB | 6852.46 MB | 6852.57 MB | 6858.98 MB |
| GPU | 1.61% | 3.4% | 3.52% | 5.96% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 36.2ms, max 48.9ms

#### Sync Details

- 🟡 Pop #1 [L]: audio=8238.0ms, video=8190.4ms (frame 490), diff=47.6ms
- 🟢 Pop #2 [R]: audio=9038.0ms, video=9009.5ms (frame 539), diff=28.5ms
- 🟢 Pop #3 [L]: audio=9838.0ms, video=9811.8ms (frame 587), diff=26.2ms
- 🟢 Pop #4 [R]: audio=10644.0ms, video=10614.1ms (frame 635), diff=29.9ms
- 🟢 Pop #5 [L]: audio=11444.0ms, video=11416.4ms (frame 683), diff=27.6ms
- 🟢 Pop #6 [R]: audio=12244.0ms, video=12218.8ms (frame 731), diff=25.2ms
- 🟡 Pop #7 [L]: audio=13070.0ms, video=13021.1ms (frame 779), diff=48.9ms
- 🟡 Pop #8 [R]: audio=13870.0ms, video=13823.4ms (frame 827), diff=46.6ms
- 🟡 Pop #9 [L]: audio=14671.0ms, video=14625.7ms (frame 875), diff=45.3ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 12/2/2/3 | 11/1/1/2 | 0 | 0 |
| After settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.806–18.821).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 17 | 8.339 | 0.269 | 0.869 | 7.940–8.809 |
| 2 | 2 | 12.971 | 0.017 | 0.034 | 12.954–12.988 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 23.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 490 at 00:08.2 of the 23.0 s video above.
