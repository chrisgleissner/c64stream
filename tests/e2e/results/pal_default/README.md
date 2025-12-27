# C64 Stream E2E Test Report

Generated: 2025-12-27 23:42:08 UTC

## Test configuration

- Format: PAL
- Frames: 30
- Duration: 0.6 seconds
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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (0.6s, 2 of 21 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 55.4% | 64.25% | 64.25% | 73.1% |
| RAM | 5890.33 MB | 5938.51 MB | 5938.51 MB | 5986.68 MB |
| GPU | 27.91% | 32.62% | 32.62% | 37.34% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 2040 video, 149 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ❌ Poor synchronization (0.0%): avg offset 0.0ms, max 0.0ms

#### Sync Details


- Channels: 
- 🔁 Channel alternation: MISMATCH

### Frame Progression

- 🟢 Frame sequence verified (178 frames analyzed, 0 colors)

| Metric | Count | Min | Median | Max |
|--------|-------|-----|--------|-----|
| Repeated frames | 0 runs | 0 | 0 | 1 |
| Skipped frames | 0 skips | 0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

- ↩️ Back steps: 0 (frame counter went backwards)

- ⚡ Severe jumps: 0 (large sequence discontinuity)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 16.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 403 at 00:08.1 of the 16.2 s video above.
