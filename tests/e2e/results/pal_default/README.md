# C64 Stream E2E Test Report

Generated: 2025-12-28 20:16:09 UTC

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
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 46.8% | 52.7% | 52.54% | 55.4% |
| RAM | 6699.8 MB | 6716.33 MB | 6716.23 MB | 6727.67 MB |
| GPU | 27.16% | 36.09% | 35.3% | 41.91% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 15.7ms, max 17.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8236.0ms, video=8219.5ms (frame 412), diff=16.5ms
- 🟢 Pop #2 [R]: audio=9190.0ms, video=9177.1ms (frame 460), diff=12.9ms
- 🟢 Pop #3 [L]: audio=10150.0ms, video=10134.7ms (frame 508), diff=15.3ms
- 🟢 Pop #4 [R]: audio=11108.0ms, video=11092.3ms (frame 556), diff=15.7ms
- 🟢 Pop #5 [L]: audio=12065.0ms, video=12049.9ms (frame 604), diff=15.1ms
- 🟢 Pop #6 [R]: audio=13025.0ms, video=13007.5ms (frame 652), diff=17.5ms
- 🟢 Pop #7 [L]: audio=13982.0ms, video=13965.1ms (frame 700), diff=16.9ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (398 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.761–15.681).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 14.963 | 0.020 | 0.040 | 14.943–14.983 |
| 2 | 2 | 9.955 | 0.020 | 0.040 | 9.935–9.975 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 18.7 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 412 at 00:08.2 of the 18.7 s video above.
