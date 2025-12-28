# C64 Stream E2E Test Report

Generated: 2025-12-28 15:56:34 UTC

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

During the test's processing window (7.6s, 16 of 36 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 46.8% | 52.35% | 53.42% | 62.2% |
| RAM | 6154.66 MB | 6167.43 MB | 6175.88 MB | 6232.38 MB |
| GPU | 26.24% | 32.34% | 33.58% | 41.32% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 32.3ms, max 52.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8286.0ms, video=8279.3ms (frame 415), diff=6.7ms
- 🟢 Pop #2 [R]: audio=9244.0ms, video=9236.9ms (frame 463), diff=7.1ms
- 🟢 Pop #3 [L]: audio=10225.0ms, video=10194.5ms (frame 511), diff=30.5ms
- 🟢 Pop #4 [R]: audio=11182.0ms, video=11152.1ms (frame 559), diff=29.9ms
- 🟡 Pop #5 [L]: audio=12140.0ms, video=12089.8ms (frame 606), diff=50.2ms
- 🟡 Pop #6 [R]: audio=13100.0ms, video=13047.4ms (frame 654), diff=52.6ms
- 🟡 Pop #7 [L]: audio=14054.0ms, video=14005.0ms (frame 702), diff=49.0ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (401 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 4/2/2/2 | 3/1/1/2 | 0 | 0 |
| After settling | 9/2/2/5 | 9/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.820–18.753).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 14 | 12.050 | 0.318 | 1.057 | 11.651–12.708 |
| 2 | 2 | 14.973 | 0.030 | 0.059 | 14.943–15.002 |
| 3 | 2 | 9.975 | 0.020 | 0.040 | 9.955–9.995 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 23.8 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 415 at 00:08.3 of the 23.8 s video above.
