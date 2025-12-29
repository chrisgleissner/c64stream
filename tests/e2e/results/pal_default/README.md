# C64 Stream E2E Test Report

Generated: 2025-12-29 10:20:10 UTC

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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 38.0% | 46.1% | 45.93% | 50.6% |
| RAM | 7107.23 MB | 7111.39 MB | 7112.81 MB | 7119.73 MB |
| GPU | 2.49% | 3.46% | 3.72% | 6.12% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 24.5ms, max 40.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8297.0ms, video=8279.3ms (frame 415), diff=17.7ms
- 🟡 Pop #2 [R]: audio=9276.0ms, video=9236.9ms (frame 463), diff=39.1ms
- 🟢 Pop #3 [L]: audio=10214.0ms, video=10194.5ms (frame 511), diff=19.5ms
- 🟢 Pop #4 [R]: audio=11172.0ms, video=11152.1ms (frame 559), diff=19.9ms
- 🟢 Pop #5 [L]: audio=12126.0ms, video=12109.7ms (frame 607), diff=16.3ms
- 🟢 Pop #6 [R]: audio=13086.0ms, video=13067.3ms (frame 655), diff=18.7ms
- 🟡 Pop #7 [L]: audio=14065.0ms, video=14024.9ms (frame 703), diff=40.1ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (401 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/3/3/3 | 1/2/2/2 | 0 | 0 |
| After settling | 2/2/3/4 | 1/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.820–18.753).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 8.978 | 0.020 | 0.040 | 8.958–8.998 |
| 2 | 1 | 13.985 | 0.000 | 0.000 | 13.985–13.985 |
| 3 | 1 | 15.761 | 0.000 | 0.000 | 15.761–15.761 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 23.8 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 415 at 00:08.3 of the 23.8 s video above.
