# C64 Stream E2E Test Report

Generated: 2025-12-26 20:23:28 UTC

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
| CPU | 45.2% | 49.8% | 50.81% | 59.2% |
| RAM | 5486.57 MB | 5497.57 MB | 5497.23 MB | 5503.43 MB |
| GPU | 37.91% | 47.62% | 46.87% | 52.99% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 15.0ms, max 24.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8700.0ms, video=8700.0ms (frame 435), diff=0.0ms
- 🟢 Pop #2 [R]: audio=9721.0ms, video=9700.0ms (frame 485), diff=21.0ms
- 🟢 Pop #3 [L]: audio=10724.0ms, video=10700.0ms (frame 535), diff=24.0ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 151 frames (3.0s)

| Metric | Count | Min | Median | Max |
|--------|-------|-----|--------|-----|
| Repeated frames | 8 runs | 2 | 2.0 | 151 |
| Skipped frames | 7 skips | 1 | 1.0 | 2 |

- ↩️ Back steps: 0 (frame counter went backwards)

- ⚡ Severe jumps: 0 (large sequence discontinuity)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 435 at 00:08.7 of the 20.0 s video above.
