# C64 Stream E2E Test Report

Generated: 2025-12-27 11:52:57 UTC

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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 30 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 45.3% | 52.05% | 57.67% | 82.4% |
| RAM | 6106.01 MB | 6172.03 MB | 6360.72 MB | 6827.6 MB |
| GPU | 34.44% | 36.36% | 37.32% | 48.4% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 25.0ms, max 25.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8745.0ms, video=8720.0ms (frame 436), diff=25.0ms
- 🟢 Pop #2 [R]: audio=9745.0ms, video=9720.0ms (frame 486), diff=25.0ms
- 🟢 Pop #3 [L]: audio=10745.0ms, video=10720.0ms (frame 536), diff=25.0ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (398 frames analyzed, 16 colors)

| Metric | Count | Min | Median | Max |
|--------|-------|-----|--------|-----|
| Repeated frames | 16 runs | 2 | 2 | 3 |
| Skipped frames | 17 skips | 1 | 1 | 2 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

- ↩️ Back steps: 0 (frame counter went backwards)

- ⚡ Severe jumps: 0 (large sequence discontinuity)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 436 at 00:08.7 of the 20.0 s video above.
