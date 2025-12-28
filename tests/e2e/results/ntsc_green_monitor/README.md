# C64 Stream E2E Test Report

Generated: 2025-12-28 18:32:37 UTC

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

During the test's processing window (7.6s, 16 of 36 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 91.9% | 92.65% | 92.99% | 94.8% |
| RAM | 6369.75 MB | 6421.75 MB | 6419.89 MB | 6442.36 MB |
| GPU | 27.19% | 40.44% | 38.45% | 45.63% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 33.1ms, max 38.2ms

#### Sync Details

- 🟡 Pop #1 [L]: audio=8262.0ms, video=8223.8ms (frame 492), diff=38.2ms
- 🟡 Pop #2 [R]: audio=9062.0ms, video=9026.2ms (frame 540), diff=35.8ms
- 🟢 Pop #3 [L]: audio=9862.0ms, video=9828.5ms (frame 588), diff=33.5ms
- 🟡 Pop #4 [R]: audio=10668.0ms, video=10630.8ms (frame 636), diff=37.2ms
- 🟢 Pop #5 [L]: audio=11468.0ms, video=11433.2ms (frame 684), diff=34.8ms
- 🟢 Pop #6 [R]: audio=12268.0ms, video=12252.2ms (frame 733), diff=15.8ms
- 🟡 Pop #7 [L]: audio=13073.0ms, video=13037.8ms (frame 780), diff=35.2ms
- 🟢 Pop #8 [R]: audio=13873.0ms, video=13840.1ms (frame 828), diff=32.9ms
- 🟢 Pop #9 [L]: audio=14694.0ms, video=14659.2ms (frame 877), diff=34.8ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 49/2/2/3 | 50/1/1/1 | 0 | 0 |
| After settling | 51/2/2/2 | 49/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.839–16.197).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 164 | 11.823 | 2.412 | 7.906 | 7.856–15.762 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 24.6 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 492 at 00:08.2 of the 24.6 s video above.
