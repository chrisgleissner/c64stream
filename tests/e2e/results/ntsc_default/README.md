# C64 Stream E2E Test Report

Generated: 2025-12-27 23:44:24 UTC

## Test configuration

- Format: NTSC
- Frames: 30
- Duration: 0.5 seconds
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

During the test's processing window (0.0s, 1 of 21 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 91.1% | 91.1% | 91.1% | 91.1% |
| RAM | 6210.58 MB | 6210.58 MB | 6210.58 MB | 6210.58 MB |
| GPU | 69.31% | 69.31% | 69.31% | 69.31% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 1800 video, 125 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ❌ Poor synchronization (0.0%): avg offset 0.0ms, max 0.0ms

#### Sync Details


- Channels: 
- 🔁 Channel alternation: MISMATCH

### Frame Progression

- 🟢 Frame sequence verified (208 frames analyzed, 0 colors)

| Metric | Count | Min | Median | Max |
|--------|-------|-----|--------|-----|
| Repeated frames | 5 runs | 2 | 2 | 3 |
| Skipped frames | 4 skips | 1 | 1 | 2 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

- ↩️ Back steps: 0 (frame counter went backwards)

- ⚡ Severe jumps: 0 (large sequence discontinuity)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 15.3 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 476 at 00:07.9 of the 15.3 s video above.
