# C64 Stream E2E Test Report

Generated: 2025-12-28 15:44:11 UTC

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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 36 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.5% | 52.3% | 59.36% | 88.2% |
| RAM | 6411.87 MB | 6525.03 MB | 6578.53 MB | 6948.63 MB |
| GPU | 0.26% | 0.45% | 0.46% | 0.67% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 110.2ms, max 753.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8222.0ms, video=8207.1ms (frame 491), diff=14.9ms
- 🟢 Pop #2 [R]: audio=9044.0ms, video=9009.5ms (frame 539), diff=34.5ms
- • Pop #3 [L]: audio=9844.0ms, video=10597.4ms (frame 634), diff=753.4ms
- 🟢 Pop #4 [R]: audio=10649.0ms, video=10614.1ms (frame 635), diff=34.9ms
- 🟢 Pop #5 [L]: audio=11449.0ms, video=11416.4ms (frame 683), diff=32.6ms
- 🟢 Pop #6 [R]: audio=12249.0ms, video=12218.8ms (frame 731), diff=30.2ms
- 🟢 Pop #7 [L]: audio=13054.0ms, video=13021.1ms (frame 779), diff=32.9ms
- 🟢 Pop #8 [R]: audio=13854.0ms, video=13823.4ms (frame 827), diff=30.6ms
- 🟢 Pop #9 [L]: audio=14654.0ms, video=14625.7ms (frame 875), diff=28.3ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 10/2/2/3 | 10/1/1/3 | 0 | 0 |
| After settling | 2/2/2/2 | 1/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.823–18.821).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 17 | 9.050 | 0.544 | 1.437 | 8.458–9.895 |
| 2 | 2 | 14.960 | 0.017 | 0.034 | 14.943–14.977 |
| 3 | 1 | 15.813 | 0.000 | 0.000 | 15.813–15.813 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 23.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 491 at 00:08.2 of the 23.0 s video above.
