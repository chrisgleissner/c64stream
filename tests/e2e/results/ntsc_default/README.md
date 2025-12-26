# C64 Stream E2E Test Report

Generated: 2025-12-26 20:21:21 UTC

## Test configuration

- Format: NTSC
- Frames: 300
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
| CPU | 54.4% | 57.8% | 57.85% | 61.9% |
| RAM | 5124.18 MB | 5143.09 MB | 5144.49 MB | 5169.68 MB |
| GPU | 41.34% | 49.71% | 48.2% | 50.87% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 32.2ms, max 43.3ms

#### Sync Details

- 🟡 Pop #1 [L]: audio=8660.0ms, video=8616.7ms (frame 517), diff=43.3ms
- 🟢 Pop #2 [R]: audio=9660.0ms, video=9633.3ms (frame 578), diff=26.7ms
- 🟢 Pop #3 [L]: audio=10660.0ms, video=10633.3ms (frame 638), diff=26.7ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 181 frames (3.0s)

| Metric | Count | Min | Median | Max |
|--------|-------|-----|--------|-----|
| Repeated frames | 11 runs | 2 | 2.0 | 181 |
| Skipped frames | 9 skips | 1 | 1.0 | 1 |

- ↩️ Back steps: 0 (frame counter went backwards)

- ⚡ Severe jumps: 0 (large sequence discontinuity)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 517 at 00:08.6 of the 20.9 s video above.
