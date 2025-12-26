# C64 Stream E2E Test Report

Generated: 2025-12-26 20:34:04 UTC

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
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 30 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 46.1% | 47.8% | 51.3% | 81.8% |
| RAM | 5383.73 MB | 5409.32 MB | 5406.3 MB | 5437.69 MB |
| GPU | 42.43% | 49.7% | 48.97% | 52.56% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 22.0ms, max 30.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8708.0ms, video=8700.0ms (frame 435), diff=8.0ms
- 🟢 Pop #2 [R]: audio=9708.0ms, video=9680.0ms (frame 484), diff=28.0ms
- 🟢 Pop #3 [L]: audio=10710.0ms, video=10680.0ms (frame 534), diff=30.0ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (347 frames analyzed, 16 colors)

| Metric | Count | Min | Median | Max |
|--------|-------|-----|--------|-----|
| Repeated frames | 5 runs | 2 | 2 | 3 |
| Skipped frames | 5 skips | 1 | 1 | 2 |

- ↩️ Back steps: 0 (frame counter went backwards)

- ⚡ Severe jumps: 0 (large sequence discontinuity)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 435 at 00:08.7 of the 20.0 s video above.
